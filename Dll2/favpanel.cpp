#include "pch.h"
#include "favpanel.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <ole2.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

namespace
{

constexpr int ROW_PADDING = 4;
constexpr int BTN_WIDTH = 32;
constexpr int BTN_HEIGHT = 24;
constexpr int BTN_GAP = 4;

// 两个按钮的含义放在悬浮提示里（图标自己画，见 DrawFolderIcon / DrawPinIcon）
constexpr auto BTN_TIP_ADDPATH = L"\u6536\u85cf\u4e00\u4e2a\u8def\u5f84\uff08\u624b\u52a8\u8f93\u5165\u6216\u7c98\u8d34\uff09";
constexpr auto BTN_TIP_ADDCURRENT = L"\u6536\u85cf\u5f53\u524d\u8d44\u6e90\u7ba1\u7406\u5668\u7a97\u53e3\u6240\u5728\u7684\u8def\u5f84";

constexpr UINT_PTR TOOL_ID_ADDPATH = 1;
constexpr UINT_PTR TOOL_ID_ADDCURRENT = 2;

struct FavPanelData
{
    HWND explorer = nullptr;
    std::vector<FavoriteEntry> favorites;
    int hoverIdx = -1;
    int selectedIdx = -1;
    int scrollOffset = 0;
    int dragSrcIdx = -1;
    int dropTargetIdx = -1;
    bool dragging = false;
    POINT dragStart = {};
    int hoverButton = -1;
};

std::map<HWND, HWND> g_panels; // explorer -> panel

FavPanelData *GetData(HWND hwnd)
{
    return reinterpret_cast<FavPanelData *>(GetPropW(hwnd, L"FavPanelData"));
}

bool IsDark()
{
    static std::wstring cachedTheme;
    static bool cachedResult = false;
    if (cachedTheme != g_favSettings.theme)
    {
        cachedResult = IsFavDarkTheme();
        cachedTheme = g_favSettings.theme;
    }
    return cachedResult;
}

std::wstring ElideText(HDC hdc, const std::wstring &text, int maxWidth)
{
    if (maxWidth <= 0 || !hdc || text.empty())
        return text;

    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
    if (sz.cx <= maxWidth)
        return text;

    int fit = 0;
    GetTextExtentExPointW(hdc, text.c_str(), (int)text.size(), maxWidth, &fit, nullptr, &sz);
    if (fit > 3)
        return text.substr(0, fit - 3) + L"...";
    if (fit > 0)
        return text.substr(0, fit);
    return text;
}

std::wstring LastDirName(const std::wstring &path)
{
    std::wstring p = path;
    if (!p.empty() && p.back() == L'\\')
        p.pop_back();
    size_t pos = p.rfind(L'\\');
    if (pos != std::wstring::npos && pos + 1 < p.size())
        return p.substr(pos + 1);
    return p;
}

std::wstring CommonPrefix(const std::vector<FavoriteEntry> &items)
{
    if (items.size() < 2)
        return L"";
    std::wstring prefix = items[0].path;
    for (size_t i = 1; i < items.size(); ++i)
    {
        size_t j = 0;
        while (j < prefix.size() && j < items[i].path.size() && prefix[j] == items[i].path[j])
            ++j;
        prefix.resize(j);
    }
    size_t pos = prefix.rfind(L'\\');
    if (pos == std::wstring::npos)
        return L"";
    return prefix.substr(0, pos + 1);
}

std::wstring StripPrefixKeepLevels(const std::wstring &path, const std::wstring &prefix)
{
    if (prefix.empty() || prefix.size() > path.size() || path.compare(0, prefix.size(), prefix) != 0)
        return path;
    std::wstring stripped = path.substr(prefix.size());
    if (stripped.find(L'\\') == std::wstring::npos)
        return path;
    return stripped;
}

int ItemHeight()
{
    return g_favItemHeight > 0 ? g_favItemHeight : 40;
}

RECT GetAddCurrentBtnRect(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int y = (g_favHeaderHeight - BTN_HEIGHT) / 2;
    int right = rc.right - 8;
    return {right - BTN_WIDTH, y, right, y + BTN_HEIGHT};
}

RECT GetAddPathBtnRect(HWND hwnd)
{
    RECT rc = GetAddCurrentBtnRect(hwnd);
    int right = rc.left - BTN_GAP;
    return {right - BTN_WIDTH, rc.top, right, rc.bottom};
}

int GetVisibleHeight(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int height = (rc.bottom - rc.top) - g_favHeaderHeight;
    return height > 0 ? height : 0;
}

void ClampScroll(HWND hwnd, FavPanelData *data)
{
    int total = (int)data->favorites.size() * ItemHeight();
    int maxScroll = total - GetVisibleHeight(hwnd);
    if (maxScroll < 0)
        maxScroll = 0;
    if (data->scrollOffset > maxScroll)
        data->scrollOffset = maxScroll;
    if (data->scrollOffset < 0)
        data->scrollOffset = 0;
}

int HitTestItem(HWND hwnd, FavPanelData *data, POINT pt)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (pt.x < 0 || pt.x >= rc.right || pt.y < g_favHeaderHeight || pt.y >= rc.bottom)
        return -1;
    int y = pt.y - g_favHeaderHeight + data->scrollOffset;
    int idx = y / ItemHeight();
    if (idx < 0 || idx >= (int)data->favorites.size())
        return -1;
    return idx;
}

// ── 拖放 ──

HRESULT BuildHDrop(const std::vector<std::wstring> &paths, STGMEDIUM *medium)
{
    size_t bytes = sizeof(DROPFILES) + sizeof(wchar_t);
    for (const auto &p : paths)
        bytes += (p.size() + 1) * sizeof(wchar_t);

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!mem)
        return E_OUTOFMEMORY;

    BYTE *base = static_cast<BYTE *>(GlobalLock(mem));
    if (!base)
    {
        GlobalFree(mem);
        return E_OUTOFMEMORY;
    }

    DROPFILES *drop = reinterpret_cast<DROPFILES *>(base);
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;

    BYTE *cursor = base + sizeof(DROPFILES);
    for (const auto &p : paths)
    {
        size_t len = (p.size() + 1) * sizeof(wchar_t);
        memcpy(cursor, p.c_str(), len);
        cursor += len;
    }
    *cursor = 0;
    GlobalUnlock(mem);

    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = mem;
    medium->pUnkForRelease = nullptr;
    return S_OK;
}

class FavDataObject : public IDataObject
{
public:
    explicit FavDataObject(const std::vector<std::wstring> &paths) : m_ref(1), m_paths(paths) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDataObject)
        {
            *ppv = static_cast<IDataObject *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0)
            delete this;
        return (ULONG)ref;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override
    {
        if (!format || !medium)
            return E_INVALIDARG;
        if (!(format->tymed & TYMED_HGLOBAL) || format->lindex != -1)
            return DV_E_LINDEX;
        if (format->cfFormat == CF_HDROP)
            return BuildHDrop(m_paths, medium);
        if (format->cfFormat == CF_UNICODETEXT)
        {
            std::wstring joined;
            for (size_t i = 0; i < m_paths.size(); ++i)
            {
                if (i)
                    joined += L"\r\n";
                joined += m_paths[i];
            }
            SIZE_T bytes = (joined.size() + 1) * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!mem)
                return E_OUTOFMEMORY;
            void *dst = GlobalLock(mem);
            if (!dst)
            {
                GlobalFree(mem);
                return E_OUTOFMEMORY;
            }
            memcpy(dst, joined.c_str(), bytes);
            GlobalUnlock(mem);
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = mem;
            medium->pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override { return DATA_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *format) override
    {
        if (!format)
            return E_INVALIDARG;
        if (!(format->tymed & TYMED_HGLOBAL) || format->lindex != -1)
            return DV_E_LINDEX;
        if (format->cfFormat == CF_HDROP || format->cfFormat == CF_UNICODETEXT)
            return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out) override
    {
        if (out)
            out->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (direction != DATADIR_GET)
            return E_NOTIMPL;

        FORMATETC formats[2] = {};
        formats[0].cfFormat = CF_HDROP;
        formats[0].dwAspect = DVASPECT_CONTENT;
        formats[0].lindex = -1;
        formats[0].tymed = TYMED_HGLOBAL;
        formats[1].cfFormat = CF_UNICODETEXT;
        formats[1].dwAspect = DVASPECT_CONTENT;
        formats[1].lindex = -1;
        formats[1].tymed = TYMED_HGLOBAL;
        return SHCreateStdEnumFmtEtc(2, formats, out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    LONG m_ref;
    std::vector<std::wstring> m_paths;
};

class FavDropSource : public IDropSource
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDropSource)
        {
            *ppv = static_cast<IDropSource *>(this);
            m_ref++;
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0)
            delete this;
        return (ULONG)ref;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override
    {
        if (escapePressed)
            return DRAGDROP_S_CANCEL;
        if (!(keyState & MK_LBUTTON))
            return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    LONG m_ref = 1;
};

void StartDragOut(HWND hwnd, int index)
{
    FavPanelData *data = GetData(hwnd);
    if (!data || index < 0 || index >= (int)data->favorites.size())
        return;

    std::vector<std::wstring> paths;
    paths.push_back(data->favorites[index].path);

    auto *dataObject = new FavDataObject(paths);
    auto *dropSource = new FavDropSource();
    DWORD effect = DROPEFFECT_NONE;
    DoDragDrop(dataObject, dropSource, DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);
    dataObject->Release();
    dropSource->Release();
}

// ── 输入对话框（用于"添加路径"和"修改备注"） ──

constexpr auto CLS_TEXT_PROMPT = L"PathHelperFavPrompt";

struct PromptState
{
    std::wstring title;
    std::wstring label;
    std::wstring value;
    bool confirmed = false;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
};

void DrawPromptButton(HDC hdc, const RECT &rc, const wchar_t *text, HFONT font, bool primary, FavThemeColors colors)
{
    HRGN rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SetDCBrushColor(hdc, primary ? colors.accent : colors.btnBg);
    FillRgn(hdc, rgn, (HBRUSH)GetStockObject(DC_BRUSH));
    DeleteObject(rgn);

    HPEN pen = CreatePen(PS_SOLID, 1, primary ? colors.accent : colors.btnBorder);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, primary ? RGB(255, 255, 255) : colors.btnText);
    RECT textRc = rc;
    DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

LRESULT CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PromptState *state = reinterpret_cast<PromptState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCT *cs = reinterpret_cast<CREATESTRUCT *>(lp);
        state = reinterpret_cast<PromptState *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        state->hwnd = hwnd;
        state->font = CreateFontW(-g_favItemFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        state->titleFont = CreateFontW(-g_favHeaderFontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                                    16, 52, 348, 26, hwnd, (HMENU)100, nullptr, nullptr);
        SendMessageW(edit, WM_SETFONT, (WPARAM)state->font, FALSE);
        SetWindowTextW(edit, state->value.c_str());

        HWND ok = CreateWindowExW(0, L"BUTTON", L"确定",
                                  WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
                                  228, 90, 65, 30, hwnd, (HMENU)IDOK, nullptr, nullptr);
        SendMessageW(ok, WM_SETFONT, (WPARAM)state->font, FALSE);

        HWND cancel = CreateWindowExW(0, L"BUTTON", L"取消",
                                      WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                      300, 90, 65, 30, hwnd, (HMENU)IDCANCEL, nullptr, nullptr);
        SendMessageW(cancel, WM_SETFONT, (WPARAM)state->font, FALSE);

        RECT rc;
        GetClientRect(hwnd, &rc);
        HRGN rgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, 8, 8);
        SetWindowRgn(hwnd, rgn, TRUE);
        return 0;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        FavThemeColors colors = GetFavThemeColors(IsDark());
        RECT rc;
        GetClientRect(hwnd, &rc);

        SetDCBrushColor(hdc, colors.bg);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));

        HFONT old = (HFONT)SelectObject(hdc, state->titleFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors.itemText);
        RECT titleRc = {16, 12, rc.right - 16, 38};
        DrawTextW(hdc, state->title.c_str(), -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, state->font);
        SetTextColor(hdc, colors.itemTextSecondary);
        RECT labelRc = {16, 38, 200, 52};
        DrawTextW(hdc, state->label.c_str(), -1, &labelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);

        HPEN pen = CreatePen(PS_SOLID, 1, colors.sep);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, 16, rc.bottom - 44, nullptr);
        LineTo(hdc, rc.right - 16, rc.bottom - 44);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wp;
        FavThemeColors colors = GetFavThemeColors(IsDark());
        SetBkColor(hdc, colors.bg);
        SetTextColor(hdc, colors.itemText);
        return (LRESULT)GetStockObject(DC_BRUSH);
    }
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
        if (dis->CtlType == ODT_BUTTON)
        {
            wchar_t text[32] = {};
            GetWindowTextW(dis->hwndItem, text, 32);
            DrawPromptButton(dis->hDC, dis->rcItem, text, state->font, dis->CtlID == IDOK,
                             GetFavThemeColors(IsDark()));
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (!state)
            break;
        if (LOWORD(wp) == IDOK)
        {
            wchar_t buf[2048] = {};
            GetDlgItemTextW(hwnd, 100, buf, 2048);
            state->value = buf;
            state->confirmed = true;
            DestroyWindow(hwnd);
        }
        else if (LOWORD(wp) == IDCANCEL)
        {
            state->confirmed = false;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        if (state)
        {
            state->hwnd = nullptr;
            if (state->font) { DeleteObject(state->font); state->font = nullptr; }
            if (state->titleFont) { DeleteObject(state->titleFont); state->titleFont = nullptr; }
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool PromptText(HWND owner, const std::wstring &title, const std::wstring &label,
                const std::wstring &initial, std::wstring &out)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc = PromptProc;
        wc.hInstance = g_hFavInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = CLS_TEXT_PROMPT;
        RegisterClassExW(&wc);
        registered = true;
    }

    PromptState state;
    state.title = title;
    state.label = label;
    state.value = initial;

    RECT ownerRc = {};
    GetWindowRect(owner, &ownerRc);
    int width = 396;
    int height = 134;

    HWND hwnd = CreateWindowExW(0, CLS_TEXT_PROMPT, L"", WS_POPUP,
                                ownerRc.left + (ownerRc.right - ownerRc.left - width) / 2,
                                ownerRc.top + (ownerRc.bottom - ownerRc.top - height) / 2,
                                width, height, owner, nullptr, g_hFavInst, &state);
    if (!hwnd)
        return false;

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);

    MSG msg;
    while (state.hwnd && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!state.hwnd || !IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    HWND parent = GetWindow(owner, GW_OWNER);
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    UNREFERENCED_PARAMETER(parent);

    if (state.confirmed)
        out = state.value;
    return state.confirmed;
}

// ── 读取资源管理器窗口当前目录 ──

// 通过 IShellWindows 找到该资源管理器窗口对应的 IShellBrowser。
//
// 注意：真实资源管理器窗口的 SHELLDLL_DefView 上 GWLP_USERDATA 是空的
// （只有通用文件对话框才会把 IShellView 放在那里），而且 Win11 带标签页的
// 资源管理器里 DefView 也不是 CabinetWClass 的直接子窗口，所以必须走这条路。
IShellBrowser *GetExplorerShellBrowser(HWND explorer)
{
    if (!explorer || !IsWindow(explorer))
        return nullptr;

    IShellWindows *shellWindows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&shellWindows))) ||
        !shellWindows)
        return nullptr;

    IShellBrowser *result = nullptr;
    long count = 0;
    if (SUCCEEDED(shellWindows->get_Count(&count)))
    {
        for (long i = 0; i < count && !result; ++i)
        {
            VARIANT index;
            VariantInit(&index);
            index.vt = VT_I4;
            index.lVal = i;

            IDispatch *dispatch = nullptr;
            if (FAILED(shellWindows->Item(index, &dispatch)) || !dispatch)
                continue;

            IWebBrowser2 *browser = nullptr;
            if (SUCCEEDED(dispatch->QueryInterface(IID_IWebBrowser2,
                                                   reinterpret_cast<void **>(&browser))) &&
                browser)
            {
                SHANDLE_PTR windowHandle = 0;
                if (SUCCEEDED(browser->get_HWND(&windowHandle)) &&
                    reinterpret_cast<HWND>(windowHandle) == explorer)
                {
                    IServiceProvider *provider = nullptr;
                    if (SUCCEEDED(browser->QueryInterface(IID_IServiceProvider,
                                                          reinterpret_cast<void **>(&provider))) &&
                        provider)
                    {
                        IShellBrowser *shellBrowser = nullptr;
                        if (SUCCEEDED(provider->QueryService(SID_STopLevelBrowser, IID_IShellBrowser,
                                                            reinterpret_cast<void **>(&shellBrowser))) &&
                            shellBrowser)
                            result = shellBrowser;
                        provider->Release();
                    }
                }
                browser->Release();
            }
            dispatch->Release();
        }
    }

    shellWindows->Release();
    return result;
}

std::wstring GetExplorerPath(HWND explorer)
{
    std::wstring result;

    IShellBrowser *shellBrowser = GetExplorerShellBrowser(explorer);
    if (!shellBrowser)
        return result;

    IShellView *shellView = nullptr;
    if (SUCCEEDED(shellBrowser->QueryActiveShellView(&shellView)) && shellView)
    {
        IFolderView2 *folderView = nullptr;
        if (SUCCEEDED(shellView->QueryInterface(IID_IFolderView2,
                                               reinterpret_cast<void **>(&folderView))) &&
            folderView)
        {
            IShellItem *folder = nullptr;
            if (SUCCEEDED(folderView->GetFolder(IID_IShellItem, reinterpret_cast<void **>(&folder))) && folder)
            {
                LPWSTR path = nullptr;
                if (SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    result = path;
                    CoTaskMemFree(path);
                }
                folder->Release();
            }
            folderView->Release();
        }
        shellView->Release();
    }

    shellBrowser->Release();
    return result;
}

// 用系统默认方式打开（文件夹 = 新的资源管理器窗口）
void OpenInNewWindow(const std::wstring &path)
{
    if (path.empty())
        return;
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// 让"这个"资源管理器窗口（收藏面板所属的那个）在当前窗口内导航过去
bool NavigateExplorerTo(HWND explorer, const std::wstring &path)
{
    if (!explorer || path.empty() || !IsWindow(explorer))
        return false;

    IShellBrowser *shellBrowser = GetExplorerShellBrowser(explorer);
    if (!shellBrowser)
        return false;

    PIDLIST_ABSOLUTE pidl = nullptr;
    bool navigated = false;
    if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl)
    {
        navigated = SUCCEEDED(shellBrowser->BrowseObject(pidl, SBSP_SAMEBROWSER | SBSP_ABSOLUTE));
        ILFree(pidl);
    }

    shellBrowser->Release();
    return navigated;
}

// 单击收藏项：优先在当前窗口内导航，失败才退回新窗口
void ActivateFavorite(HWND explorer, const std::wstring &path)
{
    if (!NavigateExplorerTo(explorer, path))
        OpenInNewWindow(path);
}

void RebuildList(HWND hwnd)
{
    FavPanelData *data = GetData(hwnd);
    if (!data)
        return;
    data->favorites = LoadFavorites();
    // 新增的收藏放在最前面，保持一致
    data->selectedIdx = -1;
    data->hoverIdx = -1;
    data->scrollOffset = 0;
    ClampScroll(hwnd, data);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void AddFavorite(HWND hwnd, const std::wstring &path, const std::wstring &note)
{
    if (path.empty())
        return;

    FavPanelData *data = GetData(hwnd);
    if (!data)
        return;

    std::wstring lowered = path;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::towlower);

    for (auto &entry : data->favorites)
    {
        std::wstring existing = entry.path;
        std::transform(existing.begin(), existing.end(), existing.begin(), ::towlower);
        if (existing == lowered)
        {
            entry.note = note;
            SaveFavorites(data->favorites);
            InvalidateRect(hwnd, nullptr, TRUE);
            return;
        }
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t ts[32];
    swprintf_s(ts, L"%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);

    FavoriteEntry entry;
    entry.path = path;
    entry.note = note;
    entry.timestamp = ts;
    data->favorites.insert(data->favorites.begin(), entry);
    SaveFavorites(data->favorites);
    InvalidateRect(hwnd, nullptr, TRUE);
}

// ── 按钮图标 ──
//
// 这两个按钮原来想用 emoji（U+1F4C1 / U+1F4CD），但实测在 explorer 进程里
// 这个面板的 GDI 上下文拿不到这些字形（会画成 .notdef 方块），所以改成
// 直接用 GDI 画，既不挑字体也不会跑版。

void DrawFolderIcon(HDC hdc, const RECT &rc, COLORREF color)
{
    const int w = 18;
    const int h = 14;
    int left = rc.left + ((rc.right - rc.left) - w) / 2;
    int top = rc.top + ((rc.bottom - rc.top) - h) / 2;

    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    // 文件夹的上沿（标签）
    RECT tab = {left + 1, top, left + 9, top + 4};
    FillRect(hdc, &tab, brush);
    // 主体
    Rectangle(hdc, left, top + 3, left + w, top + h);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawPinIcon(HDC hdc, const RECT &rc, COLORREF color, COLORREF holeColor)
{
    const int w = 13;
    const int head = 13;
    const int h = 18;
    int left = rc.left + ((rc.right - rc.left) - w) / 2;
    int top = rc.top + ((rc.bottom - rc.top) - h) / 2;

    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    Ellipse(hdc, left, top, left + w, top + head);
    POINT tip[3] = {{left + 3, top + head - 3}, {left + w - 3, top + head - 3}, {left + w / 2, top + h}};
    Polygon(hdc, tip, 3);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    // 中间的圆孔
    HBRUSH hole = CreateSolidBrush(holeColor);
    HPEN holePen = CreatePen(PS_SOLID, 1, holeColor);
    oldBrush = (HBRUSH)SelectObject(hdc, hole);
    oldPen = (HPEN)SelectObject(hdc, holePen);
    Ellipse(hdc, left + 4, top + 4, left + w - 4, top + head - 4);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hole);
    DeleteObject(holePen);
}

void DrawStarIcon(HDC hdc, int cx, int cy, int radius, COLORREF color)
{
    POINT pts[10] = {};
    for (int i = 0; i < 10; ++i)
    {
        double angle = -3.14159265 / 2.0 + i * 3.14159265 / 5.0;
        double r = (i % 2 == 0) ? radius : radius * 0.42;
        pts[i].x = cx + static_cast<LONG>(r * cos(angle));
        pts[i].y = cy + static_cast<LONG>(r * sin(angle));
    }

    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    Polygon(hdc, pts, 10);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawPanel(HDC hdc, const RECT &rc, FavPanelData *data)
{
    FavThemeColors colors = GetFavThemeColors(IsDark());

    SetDCBrushColor(hdc, colors.bg);
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));

    RECT headerRc = {0, 0, rc.right, g_favHeaderHeight};
    SetDCBrushColor(hdc, colors.headerBg);
    FillRect(hdc, &headerRc, (HBRUSH)GetStockObject(DC_BRUSH));

    // 按钮
    RECT btnAddPath = {0, 0, 0, 0};
    {
        int y = (g_favHeaderHeight - BTN_HEIGHT) / 2;
        int right = rc.right - 8;
        RECT addCurrent = {right - BTN_WIDTH, y, right, y + BTN_HEIGHT};
        int right2 = addCurrent.left - BTN_GAP;
        btnAddPath = {right2 - BTN_WIDTH, y, right2, y + BTN_HEIGHT};

        for (int i = 0; i < 2; ++i)
        {
            RECT r = (i == 0) ? btnAddPath : addCurrent;
            bool hovered = (data->hoverButton == i);
            SetDCBrushColor(hdc, hovered ? colors.btnHoverBg : colors.btnBg);
            FillRect(hdc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
            HPEN pen = CreatePen(PS_SOLID, 1, hovered ? colors.accent : colors.btnBorder);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, r.left, r.top, r.right, r.bottom, 6, 6);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(pen);

            COLORREF iconColor = hovered ? colors.accent : colors.btnText;
            COLORREF iconBg = hovered ? colors.btnHoverBg : colors.btnBg;
            if (i == 0)
                DrawFolderIcon(hdc, r, iconColor);
            else
                DrawPinIcon(hdc, r, iconColor, iconBg);
        }
    }

    // 标题（星星也自己画，不依赖字体里的符号字形）
    {
        int starRadius = g_favHeaderFontHeight / 2;
        if (starRadius < 5)
            starRadius = 5;
        int cx = 12 + starRadius;
        int cy = g_favHeaderHeight / 2;
        DrawStarIcon(hdc, cx, cy, starRadius, colors.accent);

        HFONT oldFont = (HFONT)SelectObject(hdc, g_hFavHeaderFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors.itemText);
        RECT titleRc = {cx + starRadius + 6, 0, btnAddPath.left - 6, g_favHeaderHeight};
        DrawTextW(hdc, L"收藏", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, oldFont);
    }

    {
        HPEN pen = CreatePen(PS_SOLID, 1, colors.sep);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, 8, g_favHeaderHeight - 1, nullptr);
        LineTo(hdc, rc.right - 8, g_favHeaderHeight - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    std::wstring prefix = g_favSettings.stripCommonPrefix ? CommonPrefix(data->favorites) : L"";
    int textAreaWidth = rc.right - 24 - 16;

    int itemHeight = ItemHeight();
    int firstVisible = data->scrollOffset / itemHeight;
    int lastVisible = (data->scrollOffset + (rc.bottom - g_favHeaderHeight)) / itemHeight + 1;
    if (lastVisible > (int)data->favorites.size())
        lastVisible = (int)data->favorites.size();

    for (int i = firstVisible; i < lastVisible; ++i)
    {
        int y = g_favHeaderHeight + i * itemHeight - data->scrollOffset;
        RECT itemRc = {0, y, rc.right, y + itemHeight};
        if (itemRc.bottom <= g_favHeaderHeight || itemRc.top >= rc.bottom)
            continue;

        bool selected = (i == data->selectedIdx);
        bool hovered = (i == data->hoverIdx);
        bool dragSrc = (data->dragging && i == data->dragSrcIdx);

        if (selected || hovered)
        {
            RECT hl = {ROW_PADDING, itemRc.top + 2, rc.right - ROW_PADDING, itemRc.bottom - 2};
            HRGN rgn = CreateRoundRectRgn(hl.left, hl.top, hl.right, hl.bottom, 8, 8);
            SetDCBrushColor(hdc, dragSrc ? colors.hoverBg : (selected ? colors.selBg : colors.hoverBg));
            FillRgn(hdc, rgn, (HBRUSH)GetStockObject(DC_BRUSH));
            DeleteObject(rgn);
        }

        const FavoriteEntry &entry = data->favorites[i];
        std::wstring line1 = entry.note.empty() ? LastDirName(entry.path) : entry.note;
        std::wstring line2 = StripPrefixKeepLevels(entry.path, prefix);

        int line1Top = itemRc.top + 2;
        int line1Bottom = line1Top + g_favItemFontHeight + 2;
        RECT line1Rc = {12, line1Top, rc.right - 12, line1Bottom};
        RECT line2Rc = {12, line1Bottom, rc.right - 12, itemRc.bottom - 2};

        if (g_hFavFontBold)
        {
            HFONT old = (HFONT)SelectObject(hdc, g_hFavFontBold);
            line1 = ElideText(hdc, line1, textAreaWidth);
            SelectObject(hdc, old);
        }
        if (g_hFavFontSecondary)
        {
            HFONT old = (HFONT)SelectObject(hdc, g_hFavFontSecondary);
            line2 = ElideText(hdc, line2, textAreaWidth);
            SelectObject(hdc, old);
        }

        HFONT oldFont = (HFONT)SelectObject(hdc, g_hFavFontBold);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, selected ? colors.selText : colors.itemTitle);
        DrawTextW(hdc, line1.c_str(), -1, &line1Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, g_hFavFontSecondary);
        SetTextColor(hdc, selected ? colors.selText : colors.itemTextSecondary);
        DrawTextW(hdc, line2.c_str(), -1, &line2Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);

        if (i < (int)data->favorites.size() - 1)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, colors.itemSep);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, itemRc.left + 8, itemRc.bottom - 1, nullptr);
            LineTo(hdc, itemRc.right - 8, itemRc.bottom - 1);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
    }

    if (data->favorites.empty())
    {
        HFONT oldFont = (HFONT)SelectObject(hdc, g_hFavFontSecondary);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors.itemTextSecondary);
        RECT hintRc = {12, g_favHeaderHeight + 10, rc.right - 12, g_favHeaderHeight + 10 + itemHeight};
        DrawTextW(hdc, L"还没有收藏，点击“＋ 当前”收藏当前文件夹", -1, &hintRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, oldFont);
    }

    // 拖拽插入指示线
    if (data->dragging && data->dropTargetIdx >= 0 && data->dropTargetIdx <= (int)data->favorites.size())
    {
        int lineY = g_favHeaderHeight + data->dropTargetIdx * itemHeight - data->scrollOffset;
        if (lineY > g_favHeaderHeight - 2 && lineY < rc.bottom + 2)
        {
            HPEN pen = CreatePen(PS_SOLID, 3, colors.accent);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, ROW_PADDING, lineY, nullptr);
            LineTo(hdc, rc.right - ROW_PADDING, lineY);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
    }

    // 滚动条
    int total = (int)data->favorites.size() * itemHeight;
    int visible = rc.bottom - g_favHeaderHeight;
    if (total > visible && visible > 0)
    {
        int thumbHeight = (std::max)(visible * visible / total, 24);
        int maxScroll = total - visible;
        int thumbTop = g_favHeaderHeight +
                       (int)((LONG64)data->scrollOffset * (visible - thumbHeight) / maxScroll);
        RECT thumb = {rc.right - 5, thumbTop, rc.right - 1, thumbTop + thumbHeight};
        HRGN rgn = CreateRoundRectRgn(thumb.left, thumb.top, thumb.right, thumb.bottom, 4, 4);
        SetDCBrushColor(hdc, colors.sep);
        FillRgn(hdc, rgn, (HBRUSH)GetStockObject(DC_BRUSH));
        DeleteObject(rgn);
    }

    HPEN borderPen = CreatePen(PS_SOLID, 1, colors.border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    RoundRect(hdc, 0, 0, rc.right - 1, rc.bottom - 1, FAV_CORNER_RADIUS, FAV_CORNER_RADIUS);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
}

void ApplyRegion(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    HRGN rgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, FAV_CORNER_RADIUS, FAV_CORNER_RADIUS);
    SetWindowRgn(hwnd, rgn, TRUE);
}

void ShowContextMenu(HWND hwnd, int index)
{
    FavPanelData *data = GetData(hwnd);
    if (!data || index < 0 || index >= (int)data->favorites.size())
        return;

    POINT pt;
    GetCursorPos(&pt);

    // 右键不会激活窗口，TrackPopupMenu 要求宿主在前台才能正常显示/消失
    SetForegroundWindow(hwnd);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1001, L"\u5728\u5f53\u524d\u7a97\u53e3\u6253\u5f00");
    AppendMenuW(menu, MF_STRING, 1005, L"\u5728\u65b0\u7a97\u53e3\u6253\u5f00");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1002, L"修改备注");
    AppendMenuW(menu, MF_STRING, 1003, L"复制路径");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1004, L"从收藏中移除");

    int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    if (cmd == 0)
        return;

    if (cmd == 1001)
    {
        ActivateFavorite(data->explorer, data->favorites[index].path);
    }
    else if (cmd == 1005)
    {
        OpenInNewWindow(data->favorites[index].path);
    }
    else if (cmd == 1002)
    {
        std::wstring note;
        if (PromptText(hwnd, L"修改备注", L"备注：", data->favorites[index].note, note))
        {
            data->favorites[index].note = note;
            SaveFavorites(data->favorites);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
    }
    else if (cmd == 1003)
    {
        if (OpenClipboard(hwnd))
        {
            EmptyClipboard();
            size_t bytes = (data->favorites[index].path.size() + 1) * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (mem)
            {
                void *dst = GlobalLock(mem);
                if (dst)
                {
                    memcpy(dst, data->favorites[index].path.c_str(), bytes);
                    GlobalUnlock(mem);
                    SetClipboardData(CF_UNICODETEXT, mem);
                }
                else
                {
                    GlobalFree(mem);
                }
            }
            CloseClipboard();
        }
    }
    else if (cmd == 1004)
    {
        data->favorites.erase(data->favorites.begin() + index);
        SaveFavorites(data->favorites);
        data->selectedIdx = -1;
        ClampScroll(hwnd, data);
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

// ── 双缓冲 ──
// 面板原来是直接画在窗口 DC 上的，鼠标划过列表项时整块重绘就会闪。
struct FavBackBuffer
{
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HBITMAP oldBmp = nullptr;
    int width = 0;
    int height = 0;
};

FavBackBuffer *GetFavBackBuffer(HWND hwnd, HDC refDC, int width, int height)
{
    if (width <= 0 || height <= 0)
        return nullptr;

    auto *bb = reinterpret_cast<FavBackBuffer *>(GetPropW(hwnd, L"FavBackBuffer"));
    if (!bb)
    {
        bb = new FavBackBuffer();
        SetPropW(hwnd, L"FavBackBuffer", reinterpret_cast<HANDLE>(bb));
    }

    if (bb->dc && bb->width == width && bb->height == height)
        return bb;

    if (bb->bmp)
    {
        if (bb->oldBmp)
            SelectObject(bb->dc, bb->oldBmp);
        DeleteObject(bb->bmp);
        bb->bmp = nullptr;
        bb->oldBmp = nullptr;
    }
    if (!bb->dc)
        bb->dc = CreateCompatibleDC(refDC);
    if (!bb->dc)
        return nullptr;

    bb->bmp = CreateCompatibleBitmap(refDC, width, height);
    if (!bb->bmp)
        return nullptr;

    bb->oldBmp = (HBITMAP)SelectObject(bb->dc, bb->bmp);
    bb->width = width;
    bb->height = height;
    return bb;
}

void DestroyFavBackBuffer(HWND hwnd)
{
    auto *bb = reinterpret_cast<FavBackBuffer *>(GetPropW(hwnd, L"FavBackBuffer"));
    if (!bb)
        return;

    if (bb->bmp)
    {
        if (bb->oldBmp)
            SelectObject(bb->dc, bb->oldBmp);
        DeleteObject(bb->bmp);
    }
    if (bb->dc)
        DeleteDC(bb->dc);

    RemovePropW(hwnd, L"FavBackBuffer");
    delete bb;
}

// ── 两个按钮的悬浮提示 ──

void FillButtonTool(TOOLINFOW &ti, HWND hwnd, UINT_PTR id, const RECT &rc, const wchar_t *text)
{
    ti = {};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = hwnd;
    ti.uId = id;
    ti.rect = rc;
    ti.lpszText = const_cast<LPWSTR>(text);
}

void UpdateButtonTooltips(HWND hwnd)
{
    HWND tip = reinterpret_cast<HWND>(GetPropW(hwnd, L"FavTooltip"));
    if (!tip || !IsWindow(tip))
        return;

    TOOLINFOW ti;
    FillButtonTool(ti, hwnd, TOOL_ID_ADDPATH, GetAddPathBtnRect(hwnd), BTN_TIP_ADDPATH);
    SendMessageW(tip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));

    FillButtonTool(ti, hwnd, TOOL_ID_ADDCURRENT, GetAddCurrentBtnRect(hwnd), BTN_TIP_ADDCURRENT);
    SendMessageW(tip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
}

void CreateButtonTooltips(HWND hwnd)
{
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               hwnd, nullptr, g_hFavInst, nullptr);
    if (!tip)
        return;

    SetPropW(hwnd, L"FavTooltip", reinterpret_cast<HANDLE>(tip));
    SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, 320);

    TOOLINFOW ti;
    FillButtonTool(ti, hwnd, TOOL_ID_ADDPATH, GetAddPathBtnRect(hwnd), BTN_TIP_ADDPATH);
    SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));

    FillButtonTool(ti, hwnd, TOOL_ID_ADDCURRENT, GetAddCurrentBtnRect(hwnd), BTN_TIP_ADDCURRENT);
    SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

LRESULT CALLBACK FavPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    FavPanelData *data = GetData(hwnd);

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCT *cs = reinterpret_cast<CREATESTRUCT *>(lp);
        auto *created = new FavPanelData();
        created->explorer = cs->hwndParent;
        SetPropW(hwnd, L"FavPanelData", reinterpret_cast<HANDLE>(created));
        EnsureFavFonts();
        created->favorites = LoadFavorites();
        CreateButtonTooltips(hwnd);
        data = created;
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // 画到内存 DC 再一次性贴上来，避免悬浮时整块重绘闪烁
        FavBackBuffer *bb = GetFavBackBuffer(hwnd, hdc, rc.right, rc.bottom);
        if (bb && data)
        {
            DrawPanel(bb->dc, rc, data);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, bb->dc, 0, 0, SRCCOPY);
        }
        else if (data)
        {
            DrawPanel(hdc, rc, data);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        ApplyRegion(hwnd);
        UpdateButtonTooltips(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE:
    {
        if (!data)
            break;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        if (data->dragSrcIdx >= 0)
        {
            int dx = pt.x - data->dragStart.x;
            int dy = pt.y - data->dragStart.y;
            if (!data->dragging && (dx * dx + dy * dy) > 25)
            {
                data->dragging = true;
                data->hoverIdx = -1;
            }
            if (data->dragging)
            {
                int visH = GetVisibleHeight(hwnd);
                if (pt.y >= 0 && pt.y < ItemHeight() && data->scrollOffset > 0)
                {
                    data->scrollOffset -= ItemHeight();
                    ClampScroll(hwnd, data);
                }
                else if (pt.y >= visH - ItemHeight() && pt.y < visH)
                {
                    data->scrollOffset += ItemHeight();
                    ClampScroll(hwnd, data);
                }

                int y = pt.y - g_favHeaderHeight + data->scrollOffset;
                int newDrop;
                if (pt.y < g_favHeaderHeight)
                    newDrop = 0;
                else if (pt.y >= visH + g_favHeaderHeight)
                    newDrop = (int)data->favorites.size();
                else
                {
                    int idx = y / ItemHeight();
                    int offset = y % ItemHeight();
                    newDrop = (offset >= ItemHeight() / 2) ? idx + 1 : idx;
                }
                if (newDrop < 0)
                    newDrop = 0;
                if (newDrop > (int)data->favorites.size())
                    newDrop = (int)data->favorites.size();
                data->dropTargetIdx = newDrop;
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            TRACKMOUSEEVENT tme = {sizeof(tme)};
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            break;
        }

        int hoverButton = -1;
        RECT addCurrent = GetAddCurrentBtnRect(hwnd);
        RECT addPath = GetAddPathBtnRect(hwnd);
        if (PtInRect(&addPath, pt))
            hoverButton = 0;
        else if (PtInRect(&addCurrent, pt))
            hoverButton = 1;

        int idx = HitTestItem(hwnd, data, pt);
        if (idx != data->hoverIdx || hoverButton != data->hoverButton)
        {
            data->hoverIdx = idx;
            data->hoverButton = hoverButton;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme = {sizeof(tme)};
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        break;
    }

    case WM_MOUSELEAVE:
        if (data)
        {
            data->hoverIdx = -1;
            data->hoverButton = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        if (!data)
            break;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        RECT addPath = GetAddPathBtnRect(hwnd);
        RECT addCurrent = GetAddCurrentBtnRect(hwnd);

        if (PtInRect(&addPath, pt))
        {
            std::wstring value;
            if (PromptText(hwnd, L"收藏路径", L"路径：", L"", value) && !value.empty())
                AddFavorite(hwnd, value, L"");
            return 0;
        }

        if (PtInRect(&addCurrent, pt))
        {
            std::wstring path = GetExplorerPath(data->explorer);
            if (!path.empty())
            {
                std::wstring note;
                if (PromptText(hwnd, L"收藏当前文件夹", L"备注：", LastDirName(path), note))
                    AddFavorite(hwnd, path, note);
            }
            return 0;
        }

        int idx = HitTestItem(hwnd, data, pt);
        data->selectedIdx = idx;
        if (idx >= 0)
        {
            data->dragSrcIdx = idx;
            data->dragging = false;
            data->dropTargetIdx = -1;
            data->dragStart = pt;
            SetCapture(hwnd);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!data)
            break;

        if (data->dragSrcIdx >= 0)
        {
            int src = data->dragSrcIdx;
            int drop = data->dropTargetIdx;
            bool wasDragging = data->dragging;

            data->dragSrcIdx = -1;
            data->dragging = false;
            data->dropTargetIdx = -1;

            if (wasDragging && drop >= 0)
            {
                if (src != drop && src != drop - 1)
                {
                    FavoriteEntry entry = std::move(data->favorites[src]);
                    data->favorites.erase(data->favorites.begin() + src);
                    int effectiveDrop = drop > src ? drop - 1 : drop;
                    data->favorites.insert(data->favorites.begin() + effectiveDrop, std::move(entry));
                    SaveFavorites(data->favorites);
                    data->selectedIdx = effectiveDrop;
                }
            }
            else if (src >= 0 && src < (int)data->favorites.size())
            {
                // 单击（未拖动）→ 在当前资源管理器窗口内导航
                ActivateFavorite(data->explorer, data->favorites[src].path);
            }

            if (GetCapture() == hwnd)
                ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (data)
        {
            data->dragSrcIdx = -1;
            data->dragging = false;
            data->dropTargetIdx = -1;
        }
        return 0;

    case WM_RBUTTONUP:
    {
        if (!data)
            break;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int idx = HitTestItem(hwnd, data, pt);
        if (idx >= 0)
            ShowContextMenu(hwnd, idx);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!data)
            break;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        data->scrollOffset -= delta * ItemHeight() / WHEEL_DELTA;
        ClampScroll(hwnd, data);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_FAV_REPOSITION:
    {
        HWND explorer = data ? data->explorer : nullptr;
        if (explorer && IsWindow(explorer))
            PositionFavPanel(explorer, hwnd);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (data)
        {
            if (data->explorer)
            {
                auto it = g_panels.find(data->explorer);
                if (it != g_panels.end() && it->second == hwnd)
                    g_panels.erase(it);
            }
            HWND tip = reinterpret_cast<HWND>(GetPropW(hwnd, L"FavTooltip"));
            if (tip)
            {
                RemovePropW(hwnd, L"FavTooltip");
                DestroyWindow(tip);
            }
            DestroyFavBackBuffer(hwnd);

            RemovePropW(hwnd, L"FavPanelData");
            delete data;
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void RegisterFavPanelClass()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = FavPanelProc;
    wc.hInstance = g_hFavInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = FAV_PANEL_CLASS;
    RegisterClassExW(&wc);
}

void PositionFavPanel(HWND explorer, HWND panel)
{
    RECT rcExplorer = {};
    if (!GetWindowRect(explorer, &rcExplorer))
        return;

    // 用 DWM 扩展边框去掉窗口阴影
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (dwm)
    {
        typedef HRESULT(WINAPI * P_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
        auto pDwmGetWindowAttribute =
            reinterpret_cast<P_DwmGetWindowAttribute>(GetProcAddress(dwm, "DwmGetWindowAttribute"));
        if (pDwmGetWindowAttribute)
        {
            RECT extended = {};
            const DWORD DWMWA_EXTENDED_FRAME_BOUNDS = 9;
            if (SUCCEEDED(pDwmGetWindowAttribute(explorer, DWMWA_EXTENDED_FRAME_BOUNDS, &extended, sizeof(extended))))
                rcExplorer = extended;
        }
    }

    HMONITOR monitor = MonitorFromWindow(explorer, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);

    int width = g_favSettings.panelWidth;
    int margin = g_favSettings.panelMargin;
    int height = rcExplorer.bottom - rcExplorer.top;
    if (height < 200)
        height = 200;

    int x = rcExplorer.right + margin;
    if (x + width > mi.rcWork.right)
    {
        x = rcExplorer.left - width - margin;
        if (x < mi.rcWork.left)
            x = mi.rcWork.right - width;
    }

    int y = rcExplorer.top;
    if (y < mi.rcWork.top)
        y = mi.rcWork.top;
    if (y + height > mi.rcWork.bottom)
        height = mi.rcWork.bottom - y;

    SetWindowPos(panel, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

HWND AttachFavPanel(HWND explorer)
{
    if (!explorer || !IsWindow(explorer))
        return nullptr;

    auto it = g_panels.find(explorer);
    if (it != g_panels.end() && IsWindow(it->second))
        return it->second;
    if (it != g_panels.end())
        g_panels.erase(it);

    if (!EnsureFavFonts())
        return nullptr;

    HWND panel = CreateWindowExW(WS_EX_TOOLWINDOW, FAV_PANEL_CLASS, L"", WS_POPUP,
                                 CW_USEDEFAULT, CW_USEDEFAULT, g_favSettings.panelWidth, 400,
                                 explorer, nullptr, g_hFavInst, nullptr);
    if (!panel)
        return nullptr;

    g_panels[explorer] = panel;
    PositionFavPanel(explorer, panel);
    ShowWindow(panel, SW_SHOWNA);
    return panel;
}

void DetachFavPanel(HWND explorer)
{
    auto it = g_panels.find(explorer);
    if (it == g_panels.end())
        return;
    HWND panel = it->second;
    g_panels.erase(it);
    if (panel && IsWindow(panel))
        DestroyWindow(panel);
}

void DetachAllFavPanels()
{
    auto copy = g_panels;
    for (const auto &kv : copy)
        DetachFavPanel(kv.first);
}

bool HasAnyFavPanel()
{
    return !g_panels.empty();
}

size_t FavPanelCount()
{
    return g_panels.size();
}

void RepositionAllFavPanels()
{
    for (const auto &kv : g_panels)
    {
        if (IsWindow(kv.first) && IsWindow(kv.second))
            PositionFavPanel(kv.first, kv.second);
    }
}

void RefreshAllFavPanels()
{
    for (const auto &kv : g_panels)
    {
        if (IsWindow(kv.second))
            RebuildList(kv.second);
    }
}

void RefreshFavSettings()
{
    LoadFavSettings(g_favSettings);
    ComputeFavMetrics();
}
