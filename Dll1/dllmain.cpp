#include "pch.h"
#include "settings.h"
#include "history.h"
#include "hooking.h"
#include "panel.h"
#include <commdlg.h>

// {71A5BA9E-0A34-4afc-B1E1-DFD7DD3F95D7}
static const GUID SID_SExplorerBrowser = { 0x71A5BA9E, 0x0A34, 0x4AFC, { 0xB1, 0xE1, 0xDF, 0xD7, 0xDD, 0x3F, 0x95, 0xD7 } };

HWINEVENTHOOK g_hEventHook = nullptr;
std::map<HWND, DialogInfo> g_dialogs;
std::map<DWORD, IFileDialog *> g_threadFileDialogs;
HINSTANCE g_hInst = nullptr;
HANDLE g_hHookThread = nullptr;
DWORD g_dwHookThreadId = 0;
HANDLE g_hExitEvent = nullptr;
CRITICAL_SECTION g_cs;
HFONT g_hItemFont = nullptr;
HFONT g_hHeaderFont = nullptr;
HFONT g_hItemFontBold = nullptr;
HFONT g_hItemFontSecondary = nullptr;
ID2D1Factory* g_pD2DFactory = nullptr;
IDWriteFactory* g_pDWriteFactory = nullptr;
IDWriteTextFormat* g_pItemTextFormat = nullptr;
IDWriteTextFormat* g_pItemTextFormatBold = nullptr;
IDWriteTextFormat* g_pItemTextFormatSecondary = nullptr;
IDWriteTextFormat* g_pHeaderTextFormat = nullptr;
HANDLE g_hExplorerPathsNotify = nullptr;

static HWND FindWindowExRecursive(HWND hwndParent, LPCWSTR lpszClass)
{
    HWND hWnd = FindWindowExW(hwndParent, nullptr, lpszClass, nullptr);
    if (hWnd)
        return hWnd;

    HWND hChild = FindWindowExW(hwndParent, nullptr, nullptr, nullptr);
    while (hChild)
    {
        hWnd = FindWindowExRecursive(hChild, lpszClass);
        if (hWnd)
            return hWnd;
        hChild = FindWindowExW(hwndParent, hChild, nullptr, nullptr);
    }
    return nullptr;
}

static void SetDialogPathClassic(HWND hwnd, const std::wstring &path)
{
    HWND comboEx = FindWindowExRecursive(hwnd, L"ComboBoxEx32");
    if (comboEx)
    {
        HWND edit = FindWindowExW(comboEx, nullptr, L"Edit", nullptr);
        if (edit)
        {
            SetWindowTextW(edit, path.c_str());
            return;
        }
    }

    HWND edit = FindWindowExW(hwnd, nullptr, L"Edit", nullptr);
    while (edit)
    {
        SetWindowTextW(edit, path.c_str());
        edit = FindWindowExW(hwnd, edit, L"Edit", nullptr);
    }
}

static bool TryBrowseObjectViaShellView(HWND hwnd, const std::wstring &targetPath)
{
    HWND shellDefView = FindWindowExRecursive(hwnd, L"SHELLDLL_DefView");
    if (!shellDefView)
        return false;

    IShellView *pShellView = (IShellView *)GetWindowLongPtrW(shellDefView, GWLP_USERDATA);
    if (!pShellView)
        return false;

    IServiceProvider *pSP = nullptr;
    HRESULT hr = [&]() -> HRESULT {
        __try { return pShellView->QueryInterface(IID_IServiceProvider, (void **)&pSP); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return E_POINTER; }
    }();
    if (FAILED(hr) || !pSP)
        return false;

    IShellBrowser *pSB = nullptr;
    hr = pSP->QueryService(SID_SShellBrowser, IID_IShellBrowser, (void **)&pSB);
    if (FAILED(hr) || !pSB)
    {
        hr = pSP->QueryService(SID_SExplorerBrowser, IID_IShellBrowser, (void **)&pSB);
    }
    pSP->Release();

    if (FAILED(hr) || !pSB)
        return false;

    PIDLIST_ABSOLUTE pidl = nullptr;
    hr = [&]() -> HRESULT {
        __try { return SHParseDisplayName(targetPath.c_str(), nullptr, &pidl, 0, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return E_POINTER; }
    }();

    bool result = false;
    if (SUCCEEDED(hr) && pidl)
    {
        hr = [&]() -> HRESULT {
            __try { return pSB->BrowseObject(pidl, SBSP_SAMEBROWSER | SBSP_ABSOLUTE); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return E_POINTER; }
        }();
        result = SUCCEEDED(hr);
        ILFree(pidl);
    }
    pSB->Release();
    return result;
}

static bool NavigateIFileDialog(const std::wstring &targetPath)
{
    EnterCriticalSection(&g_cs);
    auto fdIt = g_threadFileDialogs.find(GetCurrentThreadId());
    IFileDialog *pFD = (fdIt != g_threadFileDialogs.end()) ? fdIt->second : nullptr;
    LeaveCriticalSection(&g_cs);

    if (!pFD)
        return false;

    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(targetPath.c_str(), nullptr, &pidl, 0, nullptr);
    if (FAILED(hr) || !pidl)
        return false;

    IServiceProvider *pSP = nullptr;
    hr = pFD->QueryInterface(IID_IServiceProvider, (void **)&pSP);
    if (FAILED(hr) || !pSP)
    {
        ILFree(pidl);
        return false;
    }

    IShellBrowser *pSB = nullptr;
    hr = pSP->QueryService(SID_SShellBrowser, IID_IShellBrowser, (void **)&pSB);
    if (FAILED(hr) || !pSB)
        hr = pSP->QueryService(SID_SExplorerBrowser, IID_IShellBrowser, (void **)&pSB);
    pSP->Release();

    if (FAILED(hr) || !pSB)
    {
        ILFree(pidl);
        return false;
    }

    hr = pSB->BrowseObject(pidl, SBSP_SAMEBROWSER | SBSP_ABSOLUTE);
    pSB->Release();
    ILFree(pidl);
    return SUCCEEDED(hr);
}

static std::wstring ResolveDirectoryPath(const std::wstring &path)
{
    std::wstring targetPath = path;

    DWORD attrs = GetFileAttributesW(targetPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if (!(attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            size_t pos = targetPath.rfind(L'\\');
            if (pos != std::wstring::npos && pos > 0)
                targetPath = targetPath.substr(0, pos);
            else
                return targetPath;
        }
        return targetPath;
    }

    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
        return path;

    size_t pos = targetPath.rfind(L'\\');
    if (pos != std::wstring::npos && pos > 0)
    {
        targetPath = targetPath.substr(0, pos);
        attrs = GetFileAttributesW(targetPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            return targetPath;
    }

    return path;
}

static void NavigateClassicDialog(HWND hwnd, const std::wstring &path)
{
    std::wstring targetPath = ResolveDirectoryPath(path);

    if (targetPath.empty())
        return;

    if (TryBrowseObjectViaShellView(hwnd, targetPath))
        return;

    if (NavigateIFileDialog(targetPath))
        return;

    SendMessageW(hwnd, CDM_SETFOLDERPATH, 0, (LPARAM)targetPath.c_str());
}

static bool IsFileDialog(HWND hwnd)
{
    wchar_t className[32] = {};
    GetClassNameW(hwnd, className, 32);
    if (wcscmp(className, L"#32770") != 0)
        return false;

    HWND workerW = FindWindowExW(hwnd, nullptr, L"WorkerW", nullptr);
    HWND shellView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    HWND comboBoxEx = FindWindowExW(hwnd, nullptr, L"ComboBoxEx32", nullptr);
    HWND comboBox = FindWindowExW(hwnd, nullptr, L"ComboBox", nullptr);

    return workerW != nullptr || shellView != nullptr || comboBoxEx != nullptr || comboBox != nullptr;
}

static std::wstring GetSelectedPathCom(HWND hwnd)
{
    std::wstring result;

    HWND shellDefView = FindWindowExRecursive(hwnd, L"SHELLDLL_DefView");
    if (!shellDefView)
        return result;

    IShellView *pShellView = (IShellView *)GetWindowLongPtrW(shellDefView, GWLP_USERDATA);
    if (!pShellView)
        return result;

    IFolderView2 *pFolderView2 = nullptr;
    HRESULT hr = [&]() -> HRESULT {
        __try { return pShellView->QueryInterface(IID_IFolderView2, (void **)&pFolderView2); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return E_POINTER; }
    }();
    if (FAILED(hr) || !pFolderView2)
        return result;

    IShellItem *pFolderItem = nullptr;
    hr = pFolderView2->GetFolder(IID_IShellItem, (void **)&pFolderItem);

    PIDLIST_ABSOLUTE pidlFolder = nullptr;
    std::wstring folderPath;
    if (SUCCEEDED(hr) && pFolderItem)
    {
        LPWSTR pPath = nullptr;
        HRESULT hr2 = pFolderItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath);
        if (SUCCEEDED(hr2) && pPath)
        {
            folderPath = pPath;
            CoTaskMemFree(pPath);
        }
        else
        {
            pPath = nullptr;
            hr2 = pFolderItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pPath);
            if (SUCCEEDED(hr2) && pPath)
            {
                folderPath = pPath;
                CoTaskMemFree(pPath);
            }
        }
        hr2 = SHGetIDListFromObject(pFolderItem, &pidlFolder);
        pFolderItem->Release();
    }

    IEnumIDList *pEnum = nullptr;
    hr = pFolderView2->Items(SVGIO_SELECTION, IID_IEnumIDList, (void **)&pEnum);

    if (SUCCEEDED(hr) && pEnum)
    {
        LPITEMIDLIST pidlItem = nullptr;
        ULONG fetched = 0;
        while (pEnum->Next(1, &pidlItem, &fetched) == S_OK && fetched == 1)
        {
            if (pidlFolder)
            {
                IShellItem *pItem = nullptr;
                HRESULT hr3 = SHCreateShellItem(pidlFolder, nullptr, pidlItem, &pItem);
                if (SUCCEEDED(hr3) && pItem)
                {
                    LPWSTR pItemPath = nullptr;
                    hr3 = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pItemPath);
                    if (SUCCEEDED(hr3) && pItemPath)
                    {
                        result = pItemPath;
                        CoTaskMemFree(pItemPath);
                        pItem->Release();
                        ILFree(pidlItem);
                        break;
                    }
                    hr3 = pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pItemPath);
                    if (SUCCEEDED(hr3) && pItemPath)
                    {
                        result = pItemPath;
                        CoTaskMemFree(pItemPath);
                        pItem->Release();
                        ILFree(pidlItem);
                        break;
                    }
                    pItem->Release();
                }
            }
            ILFree(pidlItem);
        }
        pEnum->Release();
    }

    if (result.empty() && !folderPath.empty())
    {
        HWND edit = FindWindowExW(hwnd, nullptr, L"Edit", nullptr);
        while (edit)
        {
            wchar_t text[MAX_PATH] = {};
            GetWindowTextW(edit, text, MAX_PATH);
            if (wcslen(text) > 0)
            {
                if (wcsstr(text, L":\\") || text[0] == L'\\')
                    result = text;
                else
                    result = folderPath + L"\\" + text;
                break;
            }
            edit = FindWindowExW(hwnd, edit, L"Edit", nullptr);
        }
    }

    if (pidlFolder)
        ILFree(pidlFolder);
    pFolderView2->Release();
    return result;
}

static std::wstring GetDialogPathMsg(HWND hwnd, UINT msg)
{
    wchar_t buf[MAX_PATH] = {};
    LRESULT len = SendMessageW(hwnd, msg, MAX_PATH, (LPARAM)buf);
    if (len <= 0)
        return L"";
    if (len <= MAX_PATH)
        return buf;
    std::wstring largeBuf((size_t)len, L'\0');
    SendMessageW(hwnd, msg, (WPARAM)largeBuf.size(), (LPARAM)largeBuf.data());
    largeBuf.resize(std::wcslen(largeBuf.c_str()));
    return largeBuf;
}

std::wstring GetSelectedPath(HWND hwnd)
{
    {
        EnterCriticalSection(&g_cs);
        auto it = g_dialogs.find(hwnd);
        if (it != g_dialogs.end() && !it->second.cachedPath.empty())
        {
            std::wstring path = it->second.cachedPath;
            it->second.cachedPath.clear();
            LeaveCriticalSection(&g_cs);
            
            return path;
        }
        LeaveCriticalSection(&g_cs);
    }

    std::wstring path = GetDialogPathMsg(hwnd, CDM_GETFILEPATH);
    if (!path.empty())
        return path;

    std::wstring folderBuf = GetDialogPathMsg(hwnd, CDM_GETFOLDERPATH);

    std::wstring result = GetSelectedPathCom(hwnd);
    if (!result.empty())
        return result;

    if (!folderBuf.empty())
    {
        HWND edit = FindWindowExW(hwnd, nullptr, L"Edit", nullptr);
        while (edit)
        {
            wchar_t text[MAX_PATH] = {};
            SendMessageW(edit, WM_GETTEXT, MAX_PATH, (LPARAM)text);
            if (wcslen(text) > 0)
            {
                std::wstring r = folderBuf;
                if (!r.empty() && r.back() != L'\\')
                    r += L'\\';
                r += text;
                return r;
            }
            edit = FindWindowExW(hwnd, edit, L"Edit", nullptr);
        }
        return folderBuf;
    }

    std::wstring folderFromUI;
    // Try ComboBoxEx32 first
    HWND comboEx = FindWindowExW(hwnd, nullptr, L"ComboBoxEx32", nullptr);
    if (!comboEx)
        comboEx = FindWindowExRecursive(hwnd, L"ComboBoxEx32");
    if (comboEx)
    {
        wchar_t comboText[MAX_PATH] = {};
        SendMessageW(comboEx, WM_GETTEXT, MAX_PATH, (LPARAM)comboText);
        if (comboText[0] != L'\0' && (wcsstr(comboText, L":\\") || (wcslen(comboText) >= 2 && comboText[0] == L'\\' && comboText[1] == L'\\')))
            folderFromUI = comboText;
    }
    // Try regular ComboBox (address bar in modern dialogs)
    if (folderFromUI.empty())
    {
        HWND combo = FindWindowExW(hwnd, nullptr, L"ComboBox", nullptr);
        if (!combo)
            combo = FindWindowExRecursive(hwnd, L"ComboBox");
        if (combo)
        {
            wchar_t comboText[MAX_PATH] = {};
            SendMessageW(combo, WM_GETTEXT, MAX_PATH, (LPARAM)comboText);
            if (comboText[0] != L'\0' && (wcsstr(comboText, L":\\") || (wcslen(comboText) >= 2 && comboText[0] == L'\\' && comboText[1] == L'\\')))
                folderFromUI = comboText;
        }
    }
    // Try any child with a valid folder path
    if (folderFromUI.empty())
    {
        HWND child = FindWindowExW(hwnd, nullptr, nullptr, nullptr);
        while (child)
        {
            wchar_t text[MAX_PATH] = {};
            SendMessageW(child, WM_GETTEXT, MAX_PATH, (LPARAM)text);
            if (text[0] != L'\0' && (wcsstr(text, L":\\") || (wcslen(text) >= 2 && text[0] == L'\\' && text[1] == L'\\')))
            {
                DWORD attrs = GetFileAttributesW(text);
                if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
                {
                    folderFromUI = text;
                    break;
                }
            }
            child = FindWindowExW(hwnd, child, nullptr, nullptr);
        }
    }

    if (!folderFromUI.empty())
    {
        HWND edit = FindWindowExW(hwnd, nullptr, L"Edit", nullptr);
        while (edit)
        {
            wchar_t text[MAX_PATH] = {};
            SendMessageW(edit, WM_GETTEXT, MAX_PATH, (LPARAM)text);
            if (wcslen(text) > 0 && wcsstr(text, L":\\") == nullptr && text[0] != L'\\')
            {
                std::wstring r = folderFromUI;
                if (!r.empty() && r.back() != L'\\')
                    r += L'\\';
                r += text;
                return r;
            }
            edit = FindWindowExW(hwnd, edit, L"Edit", nullptr);
        }
        return folderFromUI;
    }

    {
        HWND edit = FindWindowExW(hwnd, nullptr, L"Edit", nullptr);
        while (edit)
        {
            wchar_t text[MAX_PATH] = {};
            SendMessageW(edit, WM_GETTEXT, MAX_PATH, (LPARAM)text);
            if (wcsstr(text, L":\\") || (wcslen(text) > 0 && text[0] == L'\\' && text[1] == L'\\'))
                return text;
            edit = FindWindowExW(hwnd, edit, L"Edit", nullptr);
        }
    }

    return L"";
}

static LRESULT CALLBACK DialogSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    EnterCriticalSection(&g_cs);
    auto it = g_dialogs.find(hwnd);
    if (it == g_dialogs.end())
    {
        LeaveCriticalSection(&g_cs);
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    DialogInfo *info = &it->second;
    LeaveCriticalSection(&g_cs);

    if (uMsg == WM_NAVIGATE_PATH)
    {
        auto *pathCopy = (std::wstring *)lParam;
        std::wstring targetPath = *pathCopy;
        delete pathCopy;
        NavigateClassicDialog(hwnd, targetPath);
        return 0;
    }
    else if (uMsg == WM_QUERY_FOLDER_PATH)
    {
        std::wstring path;

        EnterCriticalSection(&g_cs);
        auto fdIt = g_threadFileDialogs.find(GetCurrentThreadId());
        IFileDialog *pFD = (fdIt != g_threadFileDialogs.end()) ? fdIt->second : nullptr;
        LeaveCriticalSection(&g_cs);

        if (pFD)
        {
            IShellItem *pItem = nullptr;
            if (SUCCEEDED(pFD->GetFolder(&pItem)) && pItem)
            {
                LPWSTR pPath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath)) && pPath)
                {
                    path = pPath;
                    CoTaskMemFree(pPath);
                }
                pItem->Release();
            }
        }

        if (path.empty())
            path = GetSelectedPathCom(hwnd);
        if (path.empty())
        {
            path = GetDialogPathMsg(hwnd, CDM_GETFILEPATH);
            if (path.empty())
            {
                path = GetDialogPathMsg(hwnd, CDM_GETFOLDERPATH);
                if (!path.empty())
                {
                    HWND edit = FindWindowExW(hwnd, nullptr, L"Edit", nullptr);
                    if (edit)
                    {
                        wchar_t text[MAX_PATH] = {};
                        SendMessageW(edit, WM_GETTEXT, MAX_PATH, (LPARAM)text);
                        if (wcslen(text) > 0 && wcsstr(text, L":\\") == nullptr && text[0] != L'\\')
                        {
                            if (!path.empty() && path.back() != L'\\')
                                path += L'\\';
                            path += text;
                        }
                    }
                }
            }
        }
        EnterCriticalSection(&g_cs);
        info->cachedPath = path;
        LeaveCriticalSection(&g_cs);
        return 0;
    }
    else if (uMsg == WM_COMMAND && LOWORD(wParam) == IDOK)
    {
        std::wstring path = GetSelectedPath(hwnd);
        if (!path.empty())
            WritePathToHistory(path);

        return CallWindowProcW(info->originalProc, hwnd, uMsg, wParam, lParam);
    }
    else if (uMsg == WM_NOTIFY)
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->code == CDN_FOLDERCHANGE)
        {
            if (info->hwndPanel && IsWindow(info->hwndPanel))
                PostMessageW(info->hwndPanel, WM_REFRESH_FAV_STATE, 0, 0);
        }
        return CallWindowProcW(info->originalProc, hwnd, uMsg, wParam, lParam);
    }
    else if (uMsg == WM_WINDOWPOSCHANGED)
    {
        if (info->hwndPanel && IsWindow(info->hwndPanel))
            PostMessageW(info->hwndPanel, WM_REPOSITION_PANEL, 0, 0);
        return CallWindowProcW(info->originalProc, hwnd, uMsg, wParam, lParam);
    }
    else if (uMsg == WM_NCDESTROY)
    {
        WNDPROC originalProc = info->originalProc;
        HWND panel = info->hwndPanel;
        info->hwndPanel = NULL;

        if (panel && IsWindow(panel))
            PostMessageW(panel, WM_CLOSE, 0, 0);

        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)originalProc);

        EnterCriticalSection(&g_cs);
        g_dialogs.erase(hwnd);
        LeaveCriticalSection(&g_cs);

        return CallWindowProcW(originalProc, hwnd, uMsg, wParam, lParam);
    }

    return CallWindowProcW(info->originalProc, hwnd, uMsg, wParam, lParam);
}

static void SubclassDialog(HWND hwnd)
{
    EnterCriticalSection(&g_cs);
    if (g_dialogs.find(hwnd) != g_dialogs.end())
    {
        LeaveCriticalSection(&g_cs);
        return;
    }

    WNDPROC originalProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)DialogSubclassProc);

    DialogInfo info = {};
    info.hwnd = hwnd;
    info.originalProc = originalProc;
    info.hwndPanel = NULL;

    g_dialogs[hwnd] = info;
    LeaveCriticalSection(&g_cs);

    HWND hwndPanel = CreateCompanionPanel(hwnd);

    if (g_settings.autoToLatest)
    {
        std::vector<std::wstring> paths = LoadHistoryPaths();
        if (!paths.empty())
        {
            std::wstring targetPath = paths[0];
            DWORD attrs = GetFileAttributesW(targetPath.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::wstring curFolder = GetDialogPathMsg(hwnd, CDM_GETFOLDERPATH);
                if (NormalizePath(curFolder) != NormalizePath(targetPath))
                    SetDialogPathClassic(hwnd, targetPath);
            }
            else
            {
                SetDialogPathClassic(hwnd, targetPath);
            }
        }
    }

    EnterCriticalSection(&g_cs);
    auto it = g_dialogs.find(hwnd);
    if (it != g_dialogs.end())
        it->second.hwndPanel = hwndPanel;
    LeaveCriticalSection(&g_cs);
}

static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
{
    if (idObject != OBJID_WINDOW)
        return;
    if (event != EVENT_OBJECT_SHOW)
        return;

    DWORD wndProcId = 0;
    GetWindowThreadProcessId(hwnd, &wndProcId);
    if (wndProcId != GetCurrentProcessId())
        return;

    wchar_t className[32] = {};
    GetClassNameW(hwnd, className, 32);
    if (wcscmp(className, L"#32770") != 0)
        return;

    if (!IsFileDialog(hwnd))
        return;

    // Wait for dialog to fully initialize before subclassing
    for (int i = 0; i < 100 && !IsWindowVisible(hwnd); ++i)
        Sleep(1);
    SubclassDialog(hwnd);
}

static DWORD WINAPI HookThread(LPVOID)
{
    EnsureSettingsFile();
    LoadSettings(g_settings);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(&g_pDWriteFactory));

    ComputeLayoutMetrics();

    for (int retry = 0; retry < 10; ++retry)
    {
        HookAllFileDialogVtables();
        if (!g_hookedVtables.empty())
            break;
        Sleep(100);
    }

    g_hEventHook = SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW,
        nullptr,
        WinEventProc,
        GetCurrentProcessId(), 0,
        WINEVENT_OUTOFCONTEXT);

    {
        std::wstring dataDir = GetDataDir();
        if (!dataDir.empty())
        {
            SHCreateDirectoryExW(NULL, dataDir.c_str(), NULL);
            g_hExplorerPathsNotify = FindFirstChangeNotificationW(dataDir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
        }
    }

    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc = CompanionPanelProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = PANEL_CLASS;
        RegisterClassExW(&wc);
        RegisterCustomListClass();
    }

    if (g_hEventHook)
    {
        MSG msg;

        HANDLE waitHandles[2] = { g_hExitEvent, g_hExplorerPathsNotify };
        int waitCount = g_hExplorerPathsNotify ? 2 : 1;

        while (true)
        {
            DWORD dwResult = MsgWaitForMultipleObjects(
                waitCount, waitHandles,
                FALSE,
                INFINITE,
                QS_ALLINPUT);

            if (dwResult == WAIT_OBJECT_0)
                break;

            if (g_hExplorerPathsNotify && dwResult == WAIT_OBJECT_0 + 1)
            {
                EnterCriticalSection(&g_cs);
                for (auto &pair : g_dialogs)
                {
                    if (pair.second.hwndPanel && IsWindow(pair.second.hwndPanel))
                        PostMessageW(pair.second.hwndPanel, WM_REFRESH_EXPLORER_PATHS, 0, 0);
                }
                LeaveCriticalSection(&g_cs);
                FindNextChangeNotification(g_hExplorerPathsNotify);
                continue;
            }

            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
    if (g_hExplorerPathsNotify)
    {
        FindCloseChangeNotification(g_hExplorerPathsNotify);
        g_hExplorerPathsNotify = nullptr;
    }

    if (g_pItemTextFormat) { g_pItemTextFormat->Release(); g_pItemTextFormat = nullptr; }
    if (g_pItemTextFormatBold) { g_pItemTextFormatBold->Release(); g_pItemTextFormatBold = nullptr; }
    if (g_pItemTextFormatSecondary) { g_pItemTextFormatSecondary->Release(); g_pItemTextFormatSecondary = nullptr; }
    if (g_pHeaderTextFormat) { g_pHeaderTextFormat->Release(); g_pHeaderTextFormat = nullptr; }
    if (g_pDWriteFactory) { g_pDWriteFactory->Release(); g_pDWriteFactory = nullptr; }
    if (g_pD2DFactory) { g_pD2DFactory->Release(); g_pD2DFactory = nullptr; }

    CoUninitialize();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hInst = (HINSTANCE)hModule;
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_cs);
        g_hExitEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        g_hHookThread = CreateThread(nullptr, 0, HookThread, nullptr, 0, &g_dwHookThreadId);
        break;
    case DLL_PROCESS_DETACH:
        if (g_hExitEvent)
            SetEvent(g_hExitEvent);
        if (g_hHookThread)
            WaitForSingleObject(g_hHookThread, 5000);

        if (g_hEventHook)
        {
            UnhookWinEvent(g_hEventHook);
            g_hEventHook = nullptr;
        }
        if (g_hHookThread)
        {
            CloseHandle(g_hHookThread);
            g_hHookThread = nullptr;
        }
        if (g_hExitEvent)
        {
            CloseHandle(g_hExitEvent);
            g_hExitEvent = nullptr;
        }
        g_dwHookThreadId = 0;

        DeleteCriticalSection(&g_cs);

        if (g_hItemFont)
        {
            DeleteObject(g_hItemFont);
            g_hItemFont = nullptr;
        }
        if (g_hHeaderFont)
        {
            DeleteObject(g_hHeaderFont);
            g_hHeaderFont = nullptr;
        }
        if (g_hItemFontBold)
        {
            DeleteObject(g_hItemFontBold);
            g_hItemFontBold = nullptr;
        }
        if (g_hItemFontSecondary)
        {
            DeleteObject(g_hItemFontSecondary);
            g_hItemFontSecondary = nullptr;
        }
        break;
    }
    return TRUE;
}
