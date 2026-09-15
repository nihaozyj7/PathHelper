#include "pch.h"
#include "searchpanel.h"
#include "everything.h"
#include "settings.h"

#include <ole2.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace
{

constexpr auto CLS_SEARCH_PANEL = L"PathHelperSearchPanel";
constexpr UINT_PTR TIMER_DEBOUNCE = 1;
constexpr UINT DEBOUNCE_MS = 260;
constexpr DWORD MAX_RESULTS = 64;
constexpr int MIN_WIDTH = 460;
constexpr int MAX_VISIBLE_ITEMS = 8;
constexpr int HEADER_HEIGHT = 30;
constexpr int ROW_PADDING = 4;

// 缩放 / 拖动
constexpr int RESIZE_BORDER = 6;
constexpr int MIN_PANEL_WIDTH = 320;
constexpr int MIN_PANEL_HEIGHT = 96;
constexpr int MAX_PANEL_WIDTH = 1600;
constexpr int MAX_PANEL_HEIGHT = 1200;
constexpr int EDGE_LEFT = 1;
constexpr int EDGE_RIGHT = 2;
constexpr int EDGE_TOP = 4;
constexpr int EDGE_BOTTOM = 8;

constexpr int CLOSE_BTN_SIZE = 22;

struct SearchPanelData
{
    HWND dialog = nullptr;
    HWND ownerPanel = nullptr;
    std::wstring query;
    std::vector<EverythingResult> results;
    int hoverIdx = -1;
    int focusedIdx = -1;                 // 最后一次点中的项（Ctrl 加选的锚点）
    std::vector<unsigned char> selected; // 与 results 一一对应，非 0 表示已选中
    int scrollOffset = 0;
    bool querying = false;
    bool everythingMissing = false;
    bool queried = false;
    int dragSrcIdx = -1;
    POINT dragStart = {};
    bool registered = false;

    // 缩放 / 拖动 / 关闭
    int panelWidth = 0;   // 0 = 自动
    int panelHeight = 0;  // 0 = 随结果条数自动
    int userOffsetX = 0;  // 相对吸附位置的偏移（只在本次会话有效）
    int userOffsetY = 0;
    int moveStartOffsetX = 0;
    int moveStartOffsetY = 0;
    RECT dragStartRect = {};
    POINT dragStartScreen = {}; // 缩放/拖动用屏幕坐标（窗口一动客户区原点就变了）
    int resizeEdge = 0;
    bool moving = false;
    bool closePressed = false;
    bool hoverClose = false;
    bool openOnRelease = false;
    bool dismissed = false; // 本次关闭（下次新搜索或新对话框会重新出现）
    int lastOpenIdx = -1;
    DWORD lastOpenTick = 0;
};

std::map<HWND, HWND> g_searchPanels; // dialog -> overlay

SearchPanelData *GetPanelData(HWND hwnd)
{
    return reinterpret_cast<SearchPanelData *>(GetPropW(hwnd, L"SearchPanelData"));
}

int ItemHeight()
{
    return g_itemHeight > 0 ? g_itemHeight : 40;
}

// ── 选择集 ──

void SyncSelectionSize(SearchPanelData *data)
{
    if (data->selected.size() != data->results.size())
        data->selected.resize(data->results.size(), 0);
}

bool IsItemSelected(const SearchPanelData *data, int index)
{
    if (index < 0 || index >= static_cast<int>(data->selected.size()))
        return false;
    return data->selected[index] != 0;
}

int SelectedCount(const SearchPanelData *data)
{
    int count = 0;
    for (unsigned char flag : data->selected)
    {
        if (flag)
            ++count;
    }
    return count;
}

void ClearSelection(SearchPanelData *data)
{
    SyncSelectionSize(data);
    for (size_t i = 0; i < data->selected.size(); ++i)
        data->selected[i] = 0;
    data->focusedIdx = -1;
}

// 只选中这一项（普通单击）
void SelectOnlyItem(SearchPanelData *data, int index)
{
    SyncSelectionSize(data);
    for (size_t i = 0; i < data->selected.size(); ++i)
        data->selected[i] = 0;
    if (index >= 0 && index < static_cast<int>(data->selected.size()))
        data->selected[index] = 1;
    data->focusedIdx = index;
}

// Ctrl + 单击：选中/取消选中（多选）
void ToggleItemSelection(SearchPanelData *data, int index)
{
    if (index < 0 || index >= static_cast<int>(data->results.size()))
        return;

    SyncSelectionSize(data);
    data->selected[index] = data->selected[index] ? 0 : 1;
    data->focusedIdx = index;
}

// Shift + 单击：从锚点选到这里
void SelectRangeTo(SearchPanelData *data, int index)
{
    if (index < 0 || index >= static_cast<int>(data->results.size()))
        return;

    SyncSelectionSize(data);
    int anchor = data->focusedIdx;
    if (anchor < 0 || anchor >= static_cast<int>(data->results.size()))
        anchor = index;

    int from = (std::min)(anchor, index);
    int to = (std::max)(anchor, index);
    for (size_t i = 0; i < data->selected.size(); ++i)
        data->selected[i] = 0;
    for (int i = from; i <= to; ++i)
        data->selected[i] = 1;

    data->focusedIdx = index;
}

HWND FindDescendantByClass(HWND parent, const wchar_t *className)
{
    HWND found = FindWindowExW(parent, nullptr, className, nullptr);
    if (found)
        return found;

    HWND child = FindWindowExW(parent, nullptr, nullptr, nullptr);
    while (child)
    {
        found = FindDescendantByClass(child, className);
        if (found)
            return found;
        child = FindWindowExW(parent, child, nullptr, nullptr);
    }
    return nullptr;
}

std::wstring ElideText(HDC hdc, const std::wstring &text, int maxWidth)
{
    if (maxWidth <= 0 || !hdc)
        return text;

    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &sz);
    if (sz.cx <= maxWidth)
        return text;

    int fit = 0;
    GetTextExtentExPointW(hdc, text.c_str(), static_cast<int>(text.size()), maxWidth, &fit, nullptr, &sz);
    if (fit > 3)
        return text.substr(0, fit - 3) + L"...";
    if (fit > 0)
        return text.substr(0, fit);
    return text;
}

std::wstring FileNameOf(const std::wstring &path)
{
    std::wstring p = path;
    if (!p.empty() && p.back() == L'\\')
        p.pop_back();
    size_t pos = p.rfind(L'\\');
    if (pos != std::wstring::npos && pos + 1 < p.size())
        return p.substr(pos + 1);
    return p;
}

std::wstring DirectoryOf(const std::wstring &path)
{
    std::wstring p = path;
    if (!p.empty() && p.back() == L'\\')
        p.pop_back();
    size_t pos = p.rfind(L'\\');
    if (pos == std::wstring::npos || pos == 0)
        return p;
    return p.substr(0, pos);
}

// ── 拖放：IDataObject（CF_HDROP / CF_UNICODETEXT）+ IDropSource ──

HRESULT BuildHDropMedium(const std::vector<std::wstring> &paths, STGMEDIUM *medium)
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

class ResultDataObject : public IDataObject
{
public:
    explicit ResultDataObject(const std::vector<std::wstring> &paths)
        : m_ref(1), m_paths(paths)
    {
    }

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

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_ref));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0)
            delete this;
        return static_cast<ULONG>(ref);
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override
    {
        if (!format || !medium)
            return E_INVALIDARG;
        if (!(format->tymed & TYMED_HGLOBAL) || format->lindex != -1)
            return DV_E_LINDEX;

        if (format->cfFormat == CF_HDROP)
            return BuildHDropMedium(m_paths, medium);

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

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override
    {
        return DATA_E_FORMATETC;
    }

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

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override
    {
        return E_NOTIMPL;
    }

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

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    LONG m_ref;
    std::vector<std::wstring> m_paths;
};

class ResultDropSource : public IDropSource
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

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_ref));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0)
            delete this;
        return static_cast<ULONG>(ref);
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override
    {
        if (escapePressed)
            return DRAGDROP_S_CANCEL;
        if (!(keyState & MK_LBUTTON))
            return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
    {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    LONG m_ref = 1;
};

void StartResultDrag(HWND hwnd, int index)
{
    SearchPanelData *data = GetPanelData(hwnd);
    if (!data || index < 0 || index >= static_cast<int>(data->results.size()))
        return;

    std::vector<std::wstring> paths;
    // 拖动选中的项 → 整个选择集一起拖；否则只拖这一项
    if (IsItemSelected(data, index))
    {
        for (size_t i = 0; i < data->results.size(); ++i)
        {
            if (IsItemSelected(data, static_cast<int>(i)) && !data->results[i].fullPath.empty())
                paths.push_back(data->results[i].fullPath);
        }
    }
    if (paths.empty())
        paths.push_back(data->results[index].fullPath);

    auto *dataObject = new ResultDataObject(paths);
    auto *dropSource = new ResultDropSource();

    DWORD effect = DROPEFFECT_NONE;
    DoDragDrop(dataObject, dropSource, DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);

    dataObject->Release();
    dropSource->Release();
}

// 交给文件对话框自己处理：文件夹 → 导航过去；文件 → 跳到所在目录并填入文件名
void ActivateResult(HWND hwnd, const EverythingResult &result)
{
    SearchPanelData *data = GetPanelData(hwnd);
    if (!data || !data->dialog || !IsWindow(data->dialog) || result.fullPath.empty())
        return;

    auto *pathCopy = new std::wstring(result.fullPath);
    if (!PostMessageW(data->dialog, WM_NAVIGATE_RESULT, 0, reinterpret_cast<LPARAM>(pathCopy)))
        delete pathCopy;
}

// 交给系统 shell 用默认程序打开
void OpenResultWithShell(const EverythingResult &result)
{
    if (result.fullPath.empty())
        return;

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = result.fullPath.c_str();
    info.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&info);
}

// 单击 / 双击的默认动作：
//   文件夹 → 在当前对话框里导航过去
//   文件   → 交给系统默认程序打开
void OpenResultDefault(HWND hwnd, const EverythingResult &result)
{
    if (result.isFolder)
        ActivateResult(hwnd, result);
    else
        OpenResultWithShell(result);
}

void CopyTextToClipboard(HWND hwnd, const std::wstring &text)
{
    if (text.empty() || !OpenClipboard(hwnd))
        return;

    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem)
    {
        void *dst = GlobalLock(mem);
        if (dst)
        {
            memcpy(dst, text.c_str(), bytes);
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

int GetVisibleListHeight(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int height = (rc.bottom - rc.top) - HEADER_HEIGHT;
    return height > 0 ? height : 0;
}

void ClampScroll(HWND hwnd, SearchPanelData *data)
{
    int total = static_cast<int>(data->results.size()) * ItemHeight();
    int visible = GetVisibleListHeight(hwnd);
    int maxScroll = total - visible;
    if (maxScroll < 0)
        maxScroll = 0;
    if (data->scrollOffset > maxScroll)
        data->scrollOffset = maxScroll;
    if (data->scrollOffset < 0)
        data->scrollOffset = 0;
}

RECT GetCloseButtonRectIn(const RECT &clientRc)
{
    int top = (HEADER_HEIGHT - CLOSE_BTN_SIZE) / 2;
    int right = clientRc.right - 8;
    return {right - CLOSE_BTN_SIZE, top, right, top + CLOSE_BTN_SIZE};
}

RECT GetCloseButtonRect(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    return GetCloseButtonRectIn(rc);
}

// 返回边框命中位（0 表示不在边框上）
int HitTestBorder(HWND hwnd, POINT pt)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (pt.x < 0 || pt.y < 0 || pt.x >= rc.right || pt.y >= rc.bottom)
        return 0;

    int edge = 0;
    if (pt.x < RESIZE_BORDER)
        edge |= EDGE_LEFT;
    else if (pt.x >= rc.right - RESIZE_BORDER)
        edge |= EDGE_RIGHT;
    if (pt.y < RESIZE_BORDER)
        edge |= EDGE_TOP;
    else if (pt.y >= rc.bottom - RESIZE_BORDER)
        edge |= EDGE_BOTTOM;
    return edge;
}

int HitTestItem(HWND hwnd, SearchPanelData *data, POINT pt)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (pt.x < 0 || pt.x >= rc.right || pt.y < HEADER_HEIGHT || pt.y >= rc.bottom)
        return -1;

    int y = pt.y - HEADER_HEIGHT + data->scrollOffset;
    int idx = y / ItemHeight();
    if (idx < 0 || idx >= static_cast<int>(data->results.size()))
        return -1;
    return idx;
}

// 同时计算位置与尺寸；只有真的发生变化时才动窗口，
// 避免每次查询都重设窗口区域 + 重绘，这是之前闪烁的主要原因。
void UpdatePanelGeometry(HWND hwnd)
{
    SearchPanelData *data = GetPanelData(hwnd);
    if (!data || !data->dialog || !IsWindow(data->dialog))
        return;

    HMONITOR monitor = MonitorFromWindow(data->dialog, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    bool haveMonitor = GetMonitorInfoW(monitor, &mi) != FALSE;
    int workWidth = haveMonitor ? (mi.rcWork.right - mi.rcWork.left) : 1280;
    int workHeight = haveMonitor ? (mi.rcWork.bottom - mi.rcWork.top) : 800;

    // 高度：用户拖过就用拖出来的高度，否则随结果条数自动
    int height = data->panelHeight;
    if (height <= 0)
    {
        int count = static_cast<int>(data->results.size());
        if (count <= 0)
            count = 1; // 占位提示行
        if (count > MAX_VISIBLE_ITEMS)
            count = MAX_VISIBLE_ITEMS;
        height = HEADER_HEIGHT + count * ItemHeight() + ROW_PADDING;
    }
    if (height < MIN_PANEL_HEIGHT)
        height = MIN_PANEL_HEIGHT;
    if (height > workHeight)
        height = workHeight;

    HWND anchor = FindDescendantByClass(data->dialog, L"UniversalSearchBand");
    if (!anchor)
        anchor = FindDescendantByClass(data->dialog, L"Search Box");
    if (!anchor)
        anchor = FindDescendantByClass(data->dialog, L"SearchEditBoxWrapperClass");

    RECT anchorRc = {};
    if (anchor)
    {
        GetWindowRect(anchor, &anchorRc);
    }
    else
    {
        RECT fallback = {};
        GetWindowRect(data->dialog, &fallback);
        anchorRc.left = fallback.right - MIN_WIDTH - 60;
        anchorRc.right = fallback.right - 20;
        anchorRc.top = fallback.top + 30;
        anchorRc.bottom = fallback.top + 70;
    }

    RECT dialogRc = {};
    GetWindowRect(data->dialog, &dialogRc);
    int dialogWidth = dialogRc.right - dialogRc.left;

    // 宽度：用户拖过就用拖出来的宽度，否则按搜索框/对话框算
    int width = data->panelWidth;
    if (width <= 0)
    {
        width = MIN_WIDTH;
        int anchorWidth = anchorRc.right - anchorRc.left;
        if (anchorWidth + 40 > width)
            width = anchorWidth + 40;
        if (dialogWidth > 240 && width > dialogWidth)
            width = dialogWidth;
    }
    if (width < MIN_PANEL_WIDTH)
        width = MIN_PANEL_WIDTH;
    if (width > workWidth)
        width = workWidth;
    if (width > MAX_PANEL_WIDTH)
        width = MAX_PANEL_WIDTH;

    int x = anchorRc.right - width + data->userOffsetX;
    int y = anchorRc.bottom + 4 + data->userOffsetY;

    if (haveMonitor)
    {
        if (data->userOffsetX != 0 || data->userOffsetY != 0)
        {
            // 用户自己挪过：允许挪出屏幕，但至少留一块能抓回来
            const int keepVisible = 60;
            if (x + width < mi.rcWork.left + keepVisible)
                x = mi.rcWork.left + keepVisible - width;
            if (x > mi.rcWork.right - keepVisible)
                x = mi.rcWork.right - keepVisible;
            if (y + height < mi.rcWork.top + keepVisible)
                y = mi.rcWork.top + keepVisible - height;
            if (y > mi.rcWork.bottom - keepVisible)
                y = mi.rcWork.bottom - keepVisible;
        }
        else
        {
            if (x + width > mi.rcWork.right)
                x = mi.rcWork.right - width;
            if (x < mi.rcWork.left)
                x = mi.rcWork.left;
            if (y + height > mi.rcWork.bottom)
                y = mi.rcWork.bottom - height;
            if (y < mi.rcWork.top)
                y = mi.rcWork.top;
        }
    }

    RECT cur = {};
    GetWindowRect(hwnd, &cur);
    bool sizeChanged = (cur.right - cur.left != width) || (cur.bottom - cur.top != height);
    bool posChanged = (cur.left != x) || (cur.top != y);
    if (!sizeChanged && !posChanged)
        return;

    if (sizeChanged)
    {
        HRGN rgn = CreateRoundRectRgn(0, 0, width, height, CORNER_RADIUS, CORNER_RADIUS);
        SetWindowRgn(hwnd, rgn, FALSE);
    }

    // SWP_NOREDRAW：重绘时机由我们自己控制，避免系统先擦除再绘制导致的闪烁
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);

    if (sizeChanged)
        InvalidateRect(hwnd, nullptr, FALSE);
}

void EnsurePanelVisible(HWND hwnd)
{
    SearchPanelData *data = GetPanelData(hwnd);
    if (data && data->dismissed)
        return; // 用户点了关闭，本次不再自动弹出来

    if (!IsWindowVisible(hwnd))
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
}

void IssueEverythingQuery(HWND hwnd)
{
    SearchPanelData *data = GetPanelData(hwnd);
    if (!data)
        return;

    if (data->query.empty())
    {
        data->results.clear();
        data->querying = false;
        data->queried = false;
        ShowWindow(hwnd, SW_HIDE);
        return;
    }

    // 保留上一次的结果直到新结果返回，避免每次按键面板高度/内容跳变
    ClearSelection(data);
    data->hoverIdx = -1;
    data->queried = false;

    if (!EverythingEnsureAvailable())
    {
        data->everythingMissing = true;
        data->querying = false;
        data->results.clear();
        UpdatePanelGeometry(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        EnsurePanelVisible(hwnd);
        return;
    }

    data->everythingMissing = false;
    data->querying = true;
    UpdatePanelGeometry(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    EnsurePanelVisible(hwnd);

    if (!EverythingQueryAsync(hwnd, data->query, MAX_RESULTS))
    {
        data->querying = false;
        data->queried = true;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void OnEverythingReply(HWND hwnd, COPYDATASTRUCT *cds)
{
    SearchPanelData *data = GetPanelData(hwnd);
    if (!data)
        return;

    std::vector<EverythingResult> results;
    if (!EverythingHandleReply(cds, results))
        return;

    data->results = std::move(results);
    data->querying = false;
    data->queried = true;
    data->scrollOffset = 0;
    ClearSelection(data);

    ClampScroll(hwnd, data);
    UpdatePanelGeometry(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    EnsurePanelVisible(hwnd);
}

void DrawPanel(HDC hdc, const RECT &rc, SearchPanelData *data)
{
    bool isDark = IsDarkCached();
    ThemeColors colors = GetThemeColors(isDark);

    SetDCBrushColor(hdc, colors.bg);
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));

    RECT headerRc = {0, 0, rc.right, HEADER_HEIGHT};
    SetDCBrushColor(hdc, colors.headerBg);
    FillRect(hdc, &headerRc, (HBRUSH)GetStockObject(DC_BRUSH));

    {
        HPEN pen = CreatePen(PS_SOLID, 1, colors.sep);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, 8, HEADER_HEIGHT - 1, nullptr);
        LineTo(hdc, rc.right - 8, HEADER_HEIGHT - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    // 标题栏：左边状态，右边关闭按钮
    std::wstring status;
    if (data->everythingMissing)
        status = L"未检测到 Everything";
    else if (data->querying)
        status = L"搜索中...";
    else if (data->queried)
    {
        status = std::to_wstring(data->results.size()) + L" 个结果";
        int chosen = SelectedCount(data);
        if (chosen > 1)
            status += L" · 已选 " + std::to_wstring(chosen) + L" 项";
    }
    else
    {
        status = L"Everything 搜索";
    }

    RECT closeRc = GetCloseButtonRectIn(rc);

    RECT statusRc = {12, 0, closeRc.left - 10, HEADER_HEIGHT};
    DWriteDrawText(hdc, statusRc, status.c_str(), g_pHeaderTextFormat, g_hHeaderFont,
                   colors.itemText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    {
        HRGN rgn = CreateRoundRectRgn(closeRc.left, closeRc.top, closeRc.right, closeRc.bottom, 5, 5);
        COLORREF bg = colors.headerBg;
        if (data->closePressed)
            bg = colors.selBg;
        else if (data->hoverClose)
            bg = colors.btnHoverBg;
        SetDCBrushColor(hdc, bg);
        FillRgn(hdc, rgn, (HBRUSH)GetStockObject(DC_BRUSH));
        DeleteObject(rgn);

        HFONT oldFont = (HFONT)SelectObject(hdc, g_hItemFont ? g_hItemFont : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, (data->hoverClose || data->closePressed) ? colors.accent : colors.itemTextSecondary);
        RECT tr = closeRc;
        DrawTextW(hdc, L"\u2715", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
    }

    if (data->results.empty())
    {
        std::wstring hint;
        if (data->everythingMissing)
            hint = L"未检测到 Everything，请先安装并运行 Everything";
        else if (data->querying)
            hint = L"正在向 Everything 查询...";
        else
            hint = L"没有匹配的结果";

        RECT hintRc = {12, HEADER_HEIGHT + 2, rc.right - 12, HEADER_HEIGHT + ItemHeight()};
        DWriteDrawText(hdc, hintRc, hint.c_str(), g_pItemTextFormatSecondary, g_hItemFontSecondary,
                       colors.itemTextSecondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {
        int itemHeight = ItemHeight();
        int listHeight = rc.bottom - HEADER_HEIGHT;
        int firstVisible = data->scrollOffset / itemHeight;
        int lastVisible = (data->scrollOffset + listHeight) / itemHeight + 1;
        if (lastVisible > static_cast<int>(data->results.size()))
            lastVisible = static_cast<int>(data->results.size());

        int textAreaWidth = rc.right - 40 - 14;
        int selectedCount = SelectedCount(data);

        for (int i = firstVisible; i < lastVisible; ++i)
        {
            int y = HEADER_HEIGHT + i * itemHeight - data->scrollOffset;
            RECT itemRc = {0, y, rc.right, y + itemHeight};
            if (itemRc.bottom <= HEADER_HEIGHT || itemRc.top >= rc.bottom)
                continue;

            bool selected = IsItemSelected(data, i);
            bool hovered = (i == data->hoverIdx);

            if (selected || hovered)
            {
                RECT hl = {ROW_PADDING, itemRc.top + 2, rc.right - ROW_PADDING, itemRc.bottom - 2};
                HRGN rgn = CreateRoundRectRgn(hl.left, hl.top, hl.right, hl.bottom, 8, 8);
                SetDCBrushColor(hdc, selected ? colors.selBg : colors.hoverBg);
                FillRgn(hdc, rgn, (HBRUSH)GetStockObject(DC_BRUSH));
                DeleteObject(rgn);

                // 多选时给"锚点项"描个边，方便看出 Ctrl 点击的位置
                if (selected && i == data->focusedIdx && selectedCount > 1)
                {
                    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    HPEN pen = CreatePen(PS_SOLID, 1, colors.accent);
                    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
                    RoundRect(hdc, hl.left, hl.top, hl.right, hl.bottom, 8, 8);
                    SelectObject(hdc, oldPen);
                    SelectObject(hdc, oldBrush);
                    DeleteObject(pen);
                }
            }

            const EverythingResult &item = data->results[i];

            RECT iconRc = {12, itemRc.top, 12 + 24, itemRc.bottom};
            DWriteDrawText(hdc, iconRc, item.isFolder ? L"\U0001F4C1" : L"\U0001F4C4",
                           g_pHeaderTextFormat, g_hHeaderFont,
                           selected ? colors.selText : colors.accent,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            int line1Top = itemRc.top + 2;
            int line1Bottom = line1Top + g_itemFontPixelHeight + 2;
            RECT line1Rc = {40, line1Top, rc.right - 12, line1Bottom};
            RECT line2Rc = {40, line1Bottom, rc.right - 12, itemRc.bottom - 2};

            std::wstring name = FileNameOf(item.fullPath);
            std::wstring dir = DirectoryOf(item.fullPath);

            if (g_hItemFontBold)
            {
                HFONT old = (HFONT)SelectObject(hdc, g_hItemFontBold);
                name = ElideText(hdc, name, textAreaWidth);
                SelectObject(hdc, old);
            }
            if (g_hItemFontSecondary)
            {
                HFONT old = (HFONT)SelectObject(hdc, g_hItemFontSecondary);
                dir = ElideText(hdc, dir, textAreaWidth);
                SelectObject(hdc, old);
            }

            DWriteDrawText(hdc, line1Rc, name.c_str(), g_pItemTextFormatBold, g_hItemFontBold,
                           selected ? colors.selText : colors.itemTitle,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            DWriteDrawText(hdc, line2Rc, dir.c_str(), g_pItemTextFormatSecondary, g_hItemFontSecondary,
                           selected ? colors.selText : colors.itemTextSecondary,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        int total = static_cast<int>(data->results.size()) * itemHeight;
        int visible = rc.bottom - HEADER_HEIGHT;
        if (total > visible && visible > 0)
        {
            int thumbHeight = (std::max)(visible * visible / total, 24);
            int maxScroll = total - visible;
            int thumbTop = HEADER_HEIGHT +
                           static_cast<int>(static_cast<LONG64>(data->scrollOffset) *
                                            (visible - thumbHeight) / maxScroll);
            RECT thumb = {rc.right - 5, thumbTop, rc.right - 1, thumbTop + thumbHeight};
            HRGN rgn = CreateRoundRectRgn(thumb.left, thumb.top, thumb.right, thumb.bottom, 4, 4);
            SetDCBrushColor(hdc, colors.sep);
            FillRgn(hdc, rgn, (HBRUSH)GetStockObject(DC_BRUSH));
            DeleteObject(rgn);
        }
    }

    HPEN borderPen = CreatePen(PS_SOLID, 1, colors.border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    RoundRect(hdc, 0, 0, rc.right - 1, rc.bottom - 1, CORNER_RADIUS, CORNER_RADIUS);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
}

// ── 双缓冲 ──
// 之前每帧直接画在窗口 DC 上，配合窗口区域变化就会出现明显的闪烁。
struct SearchBackBuffer
{
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HBITMAP oldBmp = nullptr;
    int width = 0;
    int height = 0;
};

SearchBackBuffer *GetSearchBackBuffer(HWND hwnd, HDC refDC, int width, int height)
{
    if (width <= 0 || height <= 0)
        return nullptr;

    auto *bb = reinterpret_cast<SearchBackBuffer *>(GetPropW(hwnd, L"SearchBackBuffer"));
    if (!bb)
    {
        bb = new SearchBackBuffer();
        SetPropW(hwnd, L"SearchBackBuffer", reinterpret_cast<HANDLE>(bb));
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

    bb->oldBmp = static_cast<HBITMAP>(SelectObject(bb->dc, bb->bmp));
    bb->width = width;
    bb->height = height;
    return bb;
}

void DestroySearchBackBuffer(HWND hwnd)
{
    auto *bb = reinterpret_cast<SearchBackBuffer *>(GetPropW(hwnd, L"SearchBackBuffer"));
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
    delete bb;
    RemovePropW(hwnd, L"SearchBackBuffer");
}

// ── 自绘右键菜单 ──
//
// TrackPopupMenu 要求宿主窗口在前台，而 Everything 结果浮层是
// WS_EX_NOACTIVATE（不能成为前台窗口），实测菜单根本弹不出来。
// 所以这里用一个自己的小弹窗当菜单，行为完全可控。

constexpr auto CLS_SEARCH_MENU = L"PathHelperSearchMenu";
constexpr int MENU_ITEM_HEIGHT = 28;
constexpr int MENU_WIDTH = 176;
constexpr int MENU_PADDING = 4;
constexpr UINT_PTR TIMER_MENU_WATCH = 2;

struct SearchMenuData
{
    HWND overlay = nullptr;
    int resultIndex = -1; // 这次右键点中的是哪一条结果
    std::vector<std::wstring> items;
    std::vector<int> commands;
    int itemHeight = MENU_ITEM_HEIGHT;
    int hover = -1;
    bool dismissed = false;
};

SearchMenuData *GetMenuData(HWND hwnd)
{
    return reinterpret_cast<SearchMenuData *>(GetPropW(hwnd, L"SearchMenuData"));
}

void DrawSearchMenu(HWND hwnd, HDC hdc, const RECT &rc, SearchMenuData *data)
{
    ThemeColors colors = GetThemeColors(IsDarkCached());

    SetDCBrushColor(hdc, colors.bg);
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));

    for (size_t i = 0; i < data->items.size(); ++i)
    {
        int top = MENU_PADDING + static_cast<int>(i) * data->itemHeight;
        RECT itemRc = {MENU_PADDING, top, rc.right - MENU_PADDING, top + data->itemHeight};
        if (static_cast<int>(i) == data->hover)
        {
            HRGN rgn = CreateRoundRectRgn(itemRc.left, itemRc.top, itemRc.right, itemRc.bottom, 6, 6);
            SetDCBrushColor(hdc, colors.selBg);
            FillRgn(hdc, rgn, (HBRUSH)GetStockObject(DC_BRUSH));
            DeleteObject(rgn);
        }

        HFONT oldFont = (HFONT)SelectObject(hdc, g_hItemFont ? g_hItemFont : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, static_cast<int>(i) == data->hover ? colors.selText : colors.itemText);
        RECT textRc = {itemRc.left + 10, itemRc.top, itemRc.right - 8, itemRc.bottom};
        DrawTextW(hdc, data->items[i].c_str(), -1, &textRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, oldFont);
    }

    HPEN borderPen = CreatePen(PS_SOLID, 1, colors.border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    RoundRect(hdc, 0, 0, rc.right - 1, rc.bottom - 1, 6, 6);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
}

void DismissSearchMenu(HWND hwnd)
{
    SearchMenuData *data = GetMenuData(hwnd);
    if (data)
        data->dismissed = true;

    if (GetCapture() == hwnd)
        ReleaseCapture();

    DestroyWindow(hwnd);
}

int HitTestMenu(HWND hwnd, SearchMenuData *data, POINT pt)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (pt.x < MENU_PADDING || pt.x >= rc.right - MENU_PADDING)
        return -1;

    int index = (pt.y - MENU_PADDING) / data->itemHeight;
    if (index < 0 || index >= static_cast<int>(data->items.size()))
        return -1;
    return index;
}

LRESULT CALLBACK SearchMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SearchMenuData *data = GetMenuData(hwnd);

    switch (msg)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (data)
            DrawSearchMenu(hwnd, hdc, rc, data);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!data)
            break;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int index = HitTestMenu(hwnd, data, pt);
        if (index != data->hover)
        {
            data->hover = index;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN:
        return 0;

    case WM_LBUTTONUP:
    {
        if (!data)
            break;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int index = HitTestMenu(hwnd, data, pt);
        if (index >= 0 && index < static_cast<int>(data->commands.size()))
        {
            HWND overlay = data->overlay;
            int command = data->commands[index];
            int resultIndex = data->resultIndex;
            DismissSearchMenu(hwnd);
            if (overlay && IsWindow(overlay))
                PostMessageW(overlay, WM_SEARCH_MENU_COMMAND, command, static_cast<LPARAM>(resultIndex));
        }
        else
        {
            DismissSearchMenu(hwnd);
        }
        return 0;
    }

    case WM_RBUTTONUP:
        DismissSearchMenu(hwnd);
        return 0;

    case WM_MOUSEWHEEL:
        return 0;

    case WM_CAPTURECHANGED:
        if (data && !data->dismissed)
            DismissSearchMenu(hwnd);
        return 0;

    case WM_TIMER:
        if (wp == TIMER_MENU_WATCH)
        {
            // 鼠标离开菜单和它所属浮层就收起来（点别处也能自动关掉）
            if (data)
            {
                POINT pt;
                GetCursorPos(&pt);
                RECT rc;
                GetWindowRect(hwnd, &rc);
                if (!PtInRect(&rc, pt))
                {
                    HWND under = WindowFromPoint(pt);
                    if (under != data->overlay && GetParent(under) != data->overlay)
                        DismissSearchMenu(hwnd);
                }
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        if (data)
        {
            if (data->overlay && IsWindow(data->overlay))
                RemovePropW(data->overlay, L"SearchMenu");
            KillTimer(hwnd, TIMER_MENU_WATCH);
            RemovePropW(hwnd, L"SearchMenuData");
            delete data;
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RegisterSearchMenuClass()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = SearchMenuProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLS_SEARCH_MENU;
    RegisterClassExW(&wc);
}

// 在屏幕坐标 pt 处弹出菜单；命令 id 与显示文本一一对应
void ShowSearchMenu(HWND overlay, POINT pt, int resultIndex,
                    const std::vector<std::wstring> &texts,
                    const std::vector<int> &commands)
{
    if (texts.empty() || texts.size() != commands.size())
        return;

    SearchPanelData *panel = GetPanelData(overlay);
    HWND owner = (panel && panel->dialog && IsWindow(panel->dialog)) ? panel->dialog : overlay;

    // 已经有一个菜单开着就先收掉
    HWND existing = reinterpret_cast<HWND>(GetPropW(overlay, L"SearchMenu"));
    if (existing && IsWindow(existing))
        DismissSearchMenu(existing);

    RegisterSearchMenuClass();

    int itemHeight = (g_itemHeight > 0 ? g_itemHeight : 40) * 4 / 5;
    if (itemHeight < 24)
        itemHeight = 24;
    if (itemHeight > 34)
        itemHeight = 34;

    int height = MENU_PADDING * 2 + static_cast<int>(texts.size()) * itemHeight;

    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfoW(monitor, &mi))
    {
        if (pt.x + MENU_WIDTH > mi.rcWork.right)
            pt.x = mi.rcWork.right - MENU_WIDTH;
        if (pt.x < mi.rcWork.left)
            pt.x = mi.rcWork.left;
        if (pt.y + height > mi.rcWork.bottom)
            pt.y = mi.rcWork.bottom - height;
        if (pt.y < mi.rcWork.top)
            pt.y = mi.rcWork.top;
    }

    HWND menu = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, CLS_SEARCH_MENU, L"",
                                WS_POPUP, pt.x, pt.y, MENU_WIDTH, height,
                                owner, nullptr, g_hInst, nullptr);
    if (!menu)
        return;

    auto *data = new SearchMenuData();
    data->overlay = overlay;
    data->resultIndex = resultIndex;
    data->items = texts;
    data->commands = commands;
    data->itemHeight = itemHeight;
    SetPropW(menu, L"SearchMenuData", reinterpret_cast<HANDLE>(data));
    SetPropW(overlay, L"SearchMenu", reinterpret_cast<HANDLE>(menu));

    HRGN rgn = CreateRoundRectRgn(0, 0, MENU_WIDTH, height, 6, 6);
    SetWindowRgn(menu, rgn, FALSE);

    ShowWindow(menu, SW_SHOWNOACTIVATE);
    SetCapture(menu);
    SetTimer(menu, TIMER_MENU_WATCH, 250, nullptr);
}

LRESULT CALLBACK SearchPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SearchPanelData *data = GetPanelData(hwnd);

    switch (msg)
    {
    case WM_COPYDATA:
        if (data)
        {
            OnEverythingReply(hwnd, reinterpret_cast<COPYDATASTRUCT *>(lp));
            return TRUE;
        }
        break;

    case WM_SEARCH_MENU_COMMAND:
    {
        if (!data)
            break;

        int index = static_cast<int>(lp);
        if (index < 0 || index >= static_cast<int>(data->results.size()))
            break;

        if (wp == 2001)
        {
            ActivateResult(hwnd, data->results[index]);
        }
        else if (wp == 2002)
        {
            OpenResultWithShell(data->results[index]);
        }
        else if (wp == 2003)
        {
            // 多选时把所有选中项的完整路径一起复制（每行一条）
            std::wstring text;
            for (size_t i = 0; i < data->results.size(); ++i)
            {
                if (!IsItemSelected(data, static_cast<int>(i)))
                    continue;
                if (!text.empty())
                    text += L"\r\n";
                text += data->results[i].fullPath;
            }
            if (text.empty())
                text = data->results[index].fullPath;
            CopyTextToClipboard(hwnd, text);
        }
        return 0;
    }

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;

        SearchBackBuffer *backBuffer = GetSearchBackBuffer(hwnd, hdc, cx, cy);
        if (!backBuffer)
        {
            if (data)
                DrawPanel(hdc, rc, data);
            EndPaint(hwnd, &ps);
            return 0;
        }

        HDC memDC = backBuffer->dc;
        if (data)
        {
            DrawPanel(memDC, rc, data);
        }
        else
        {
            SetDCBrushColor(memDC, GetThemeColors(IsDarkCached()).bg);
            FillRect(memDC, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
        }

        BitBlt(hdc, 0, 0, cx, cy, memDC, 0, 0, SRCCOPY);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == TIMER_DEBOUNCE)
        {
            KillTimer(hwnd, TIMER_DEBOUNCE);
            IssueEverythingQuery(hwnd);
            return 0;
        }
        break;

    case WM_SETCURSOR:
    {
        // 边框上换成缩放光标（自己处理边框，所以不能靠 HT 命中码）
        if (LOWORD(lp) == HTCLIENT && data)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            int edge = HitTestBorder(hwnd, pt);
            LPCWSTR cursorId = nullptr;
            if ((edge & (EDGE_LEFT | EDGE_RIGHT)) && (edge & (EDGE_TOP | EDGE_BOTTOM)))
                cursorId = (edge & EDGE_LEFT) ? IDC_SIZENESW : IDC_SIZENWSE;
            else if (edge & (EDGE_LEFT | EDGE_RIGHT))
                cursorId = IDC_SIZEWE;
            else if (edge & (EDGE_TOP | EDGE_BOTTOM))
                cursorId = IDC_SIZENS;

            if (cursorId)
            {
                SetCursor(LoadCursorW(nullptr, cursorId));
                return TRUE;
            }
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        if (!data)
            break;

        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        POINT ptScreen = pt;
        ClientToScreen(hwnd, &ptScreen);

        // ① 正在缩放
        if (data->resizeEdge)
        {
            int dx = ptScreen.x - data->dragStartScreen.x;
            int dy = ptScreen.y - data->dragStartScreen.y;
            int startW = data->dragStartRect.right - data->dragStartRect.left;
            int startH = data->dragStartRect.bottom - data->dragStartRect.top;

            int newW = startW;
            if (data->resizeEdge & EDGE_LEFT)
                newW = startW - dx;
            else if (data->resizeEdge & EDGE_RIGHT)
                newW = startW + dx;
            if (newW < MIN_PANEL_WIDTH)
                newW = MIN_PANEL_WIDTH;
            if (newW > MAX_PANEL_WIDTH)
                newW = MAX_PANEL_WIDTH;

            int newH = startH;
            if (data->resizeEdge & EDGE_TOP)
                newH = startH - dy;
            else if (data->resizeEdge & EDGE_BOTTOM)
                newH = startH + dy;
            if (newH < MIN_PANEL_HEIGHT)
                newH = MIN_PANEL_HEIGHT;
            if (newH > MAX_PANEL_HEIGHT)
                newH = MAX_PANEL_HEIGHT;

            // 拖右边/下边时让左边/上边保持不动
            if (data->resizeEdge & EDGE_RIGHT)
                data->userOffsetX = data->moveStartOffsetX + (newW - startW);
            if (data->resizeEdge & EDGE_TOP)
                data->userOffsetY = data->moveStartOffsetY + (startH - newH);

            data->panelWidth = newW;
            data->panelHeight = newH;
            UpdatePanelGeometry(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // ② 正在拖动（只在本次会话生效）
        if (data->moving)
        {
            data->userOffsetX = data->moveStartOffsetX + (ptScreen.x - data->dragStartScreen.x);
            data->userOffsetY = data->moveStartOffsetY + (ptScreen.y - data->dragStartScreen.y);
            UpdatePanelGeometry(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // ③ 按住列表项拖动
        if (data->dragSrcIdx >= 0)
        {
            int dx = pt.x - data->dragStart.x;
            int dy = pt.y - data->dragStart.y;
            if ((dx * dx + dy * dy) > 25)
            {
                int src = data->dragSrcIdx;
                data->dragSrcIdx = -1;
                data->openOnRelease = false;
                StartResultDrag(hwnd, src);
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
        }

        // ④ 悬浮高亮
        RECT closeRc = GetCloseButtonRect(hwnd);
        bool hoverClose = PtInRect(&closeRc, pt) != 0;
        int idx = HitTestItem(hwnd, data, pt);
        if (idx != data->hoverIdx || hoverClose != data->hoverClose)
        {
            data->hoverIdx = idx;
            data->hoverClose = hoverClose;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme = {sizeof(tme)};
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        break;
    }

    case WM_MOUSELEAVE:
        if (data && (data->hoverIdx != -1 || data->hoverClose))
        {
            data->hoverIdx = -1;
            data->hoverClose = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case WM_LBUTTONDOWN:
    {
        if (!data)
            break;

        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        data->dragSrcIdx = -1;
        data->moving = false;
        data->resizeEdge = 0;
        data->closePressed = false;
        data->openOnRelease = false;

        // ① 关闭按钮
        RECT closeRc = GetCloseButtonRect(hwnd);
        if (PtInRect(&closeRc, pt))
        {
            data->closePressed = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // ② 边框缩放
        int edge = HitTestBorder(hwnd, pt);
        if (edge)
        {
            data->resizeEdge = edge;
            data->dragStart = pt;
            data->dragStartScreen = pt;
            ClientToScreen(hwnd, &data->dragStartScreen);
            data->moveStartOffsetX = data->userOffsetX;
            data->moveStartOffsetY = data->userOffsetY;
            GetWindowRect(hwnd, &data->dragStartRect);
            SetCapture(hwnd);
            return 0;
        }

        // ③ 标题栏拖动（不写配置，只在本次会话里挪位置）
        if (pt.y < HEADER_HEIGHT)
        {
            data->moving = true;
            data->dragStart = pt;
            data->dragStartScreen = pt;
            ClientToScreen(hwnd, &data->dragStartScreen);
            data->moveStartOffsetX = data->userOffsetX;
            data->moveStartOffsetY = data->userOffsetY;
            GetWindowRect(hwnd, &data->dragStartRect);
            SetCapture(hwnd);
            return 0;
        }

        // ④ 列表项：Ctrl 加选 / Shift 范围 / 普通单击松开时打开
        int idx = HitTestItem(hwnd, data, pt);
        if (idx < 0)
        {
            ClearSelection(data);
        }
        else
        {
            if (wp & MK_CONTROL)
            {
                ToggleItemSelection(data, idx); // Ctrl + 单击 = 加选 / 取消选中
            }
            else if (wp & MK_SHIFT)
            {
                SelectRangeTo(data, idx); // Shift + 单击 = 范围选择
            }
            else
            {
                SelectOnlyItem(data, idx);
                data->openOnRelease = true;
            }

            data->dragSrcIdx = idx;
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

        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        if (data->closePressed)
        {
            data->closePressed = false;
            if (GetCapture() == hwnd)
                ReleaseCapture();

            RECT closeRc = GetCloseButtonRect(hwnd);
            if (PtInRect(&closeRc, pt))
            {
                // 本次关闭：新对话框 / 清空后重新搜索时会自己回来
                data->dismissed = true;
                ShowWindow(hwnd, SW_HIDE);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (data->resizeEdge)
        {
            data->resizeEdge = 0;
            if (GetCapture() == hwnd)
                ReleaseCapture();

            if (data->panelWidth > 0 && data->panelHeight > 0)
                SaveSearchPanelSize(data->panelWidth, data->panelHeight);
            return 0;
        }

        if (data->moving)
        {
            data->moving = false;
            if (GetCapture() == hwnd)
                ReleaseCapture();
            return 0;
        }

        int idx = data->dragSrcIdx;
        bool shouldOpen = data->openOnRelease;
        data->dragSrcIdx = -1;
        data->openOnRelease = false;
        if (GetCapture() == hwnd)
            ReleaseCapture();

        if (shouldOpen && idx >= 0 && idx < static_cast<int>(data->results.size()))
        {
            // 双击会连着来两次单击，这里对"同一个条目"做一下防抖，
            // 免得一次双击把默认程序打开两遍
            DWORD now = GetTickCount();
            if (idx != data->lastOpenIdx || (now - data->lastOpenTick) > 400)
            {
                data->lastOpenIdx = idx;
                data->lastOpenTick = now;
                OpenResultDefault(hwnd, data->results[idx]);
            }
        }

        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        // 单击已经负责打开，这里只清理状态
        if (data)
        {
            data->dragSrcIdx = -1;
            data->openOnRelease = false;
            if (GetCapture() == hwnd)
                ReleaseCapture();
        }
        return 0;
    }

    case WM_RBUTTONUP:
    {
        if (!data)
            break;

        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int idx = HitTestItem(hwnd, data, pt);
        if (idx < 0)
        {
            ClearSelection(data);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }

        // 右键点到未选中的项 → 只选中它；已在选择集里则保持多选不动
        if (IsItemSelected(data, idx))
            data->focusedIdx = idx;
        else
            SelectOnlyItem(data, idx);
        InvalidateRect(hwnd, nullptr, FALSE);

        POINT screenPt = pt;
        ClientToScreen(hwnd, &screenPt);

        std::vector<std::wstring> texts = {
            L"\u5728\u5f53\u524d\u7a97\u53e3\u6253\u5f00",
            L"\u5728\u65b0\u7a97\u53e3\u6253\u5f00",
            L"\u590d\u5236\u5b8c\u6574\u8def\u5f84"};
        std::vector<int> commands = {2001, 2002, 2003};
        ShowSearchMenu(hwnd, screenPt, idx, texts, commands);
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (data)
        {
            data->dragSrcIdx = -1;
            data->moving = false;
            data->resizeEdge = 0;
            data->openOnRelease = false;
            data->closePressed = false;
        }
        return 0;

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

    case WM_DESTROY:
        if (data)
        {
            if (data->registered && data->dialog)
            {
                EnterCriticalSection(&g_cs);
                auto it = g_searchPanels.find(data->dialog);
                if (it != g_searchPanels.end() && it->second == hwnd)
                    g_searchPanels.erase(it);
                LeaveCriticalSection(&g_cs);
            }
            KillTimer(hwnd, TIMER_DEBOUNCE);
            DestroySearchBackBuffer(hwnd);

            HWND menu = reinterpret_cast<HWND>(GetPropW(hwnd, L"SearchMenu"));
            if (menu && IsWindow(menu))
            {
                RemovePropW(hwnd, L"SearchMenu");
                DestroyWindow(menu);
            }

            RemovePropW(hwnd, L"SearchPanelData");
            delete data;
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void RegisterSearchPanelClass()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW | CS_DBLCLKS;
    wc.lpfnWndProc = SearchPanelProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLS_SEARCH_PANEL;
    RegisterClassExW(&wc);
}

HWND CreateSearchPanel(HWND hwndDialog, HWND hwndOwnerPanel)
{
    RegisterSearchPanelClass();

    EnterCriticalSection(&g_cs);
    auto it = g_searchPanels.find(hwndDialog);
    if (it != g_searchPanels.end())
    {
        HWND existing = IsWindow(it->second) ? it->second : nullptr;
        if (!existing)
            g_searchPanels.erase(it);
        else
        {
            LeaveCriticalSection(&g_cs);
            return existing;
        }
    }
    LeaveCriticalSection(&g_cs);

    int initHeight = HEADER_HEIGHT + ItemHeight() + ROW_PADDING;
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                CLS_SEARCH_PANEL, L"",
                                WS_POPUP,
                                0, 0, MIN_WIDTH, initHeight,
                                hwndDialog, nullptr, g_hInst, nullptr);
    if (!hwnd)
        return nullptr;

    auto *data = new SearchPanelData();
    data->dialog = hwndDialog;
    data->ownerPanel = hwndOwnerPanel;
    // 尺寸取用户上次拖出来的值（0 = 自动）
    data->panelWidth = g_settings.searchPanelWidth;
    data->panelHeight = g_settings.searchPanelHeight;
    SetPropW(hwnd, L"SearchPanelData", reinterpret_cast<HANDLE>(data));
    data->registered = true;

    EnterCriticalSection(&g_cs);
    g_searchPanels[hwndDialog] = hwnd;
    LeaveCriticalSection(&g_cs);

    HRGN rgn = CreateRoundRectRgn(0, 0, MIN_WIDTH, initHeight, CORNER_RADIUS, CORNER_RADIUS);
    SetWindowRgn(hwnd, rgn, FALSE);

    return hwnd;
}

void DestroySearchPanel(HWND hwndSearchPanel)
{
    if (hwndSearchPanel && IsWindow(hwndSearchPanel))
        DestroyWindow(hwndSearchPanel);
}

void SearchPanelSetQuery(HWND hwndSearchPanel, const std::wstring &query)
{
    if (!hwndSearchPanel || !IsWindow(hwndSearchPanel))
        return;

    SearchPanelData *data = GetPanelData(hwndSearchPanel);
    if (!data)
        return;

    if (query == data->query)
        return;

    bool wasEmpty = data->query.empty();
    data->query = query;

    if (query.empty())
    {
        KillTimer(hwndSearchPanel, TIMER_DEBOUNCE);
        data->results.clear();
        data->querying = false;
        data->queried = false;
        ShowWindow(hwndSearchPanel, SW_HIDE);
        return;
    }

    if (wasEmpty)
        data->dismissed = false; // 重新开始一次搜索，面板自己回来

    SetTimer(hwndSearchPanel, TIMER_DEBOUNCE, DEBOUNCE_MS, nullptr);
    data->querying = true;
    data->everythingMissing = false;
    UpdatePanelGeometry(hwndSearchPanel);
    InvalidateRect(hwndSearchPanel, nullptr, FALSE);
    EnsurePanelVisible(hwndSearchPanel);
}

void SearchPanelReposition(HWND hwndSearchPanel)
{
    if (!hwndSearchPanel || !IsWindow(hwndSearchPanel))
        return;
    if (!IsWindowVisible(hwndSearchPanel))
        return;
    UpdatePanelGeometry(hwndSearchPanel);
}

void SearchPanelHide(HWND hwndSearchPanel)
{
    if (hwndSearchPanel && IsWindow(hwndSearchPanel))
        ShowWindow(hwndSearchPanel, SW_HIDE);
}
