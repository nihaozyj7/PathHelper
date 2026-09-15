#include "pch.h"
#include "favpanel.h"

#include <ole2.h>
#include <map>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace
{

constexpr UINT_PTR TIMER_POLL_SETTINGS = 1;
constexpr UINT POLL_INTERVAL_MS = 2000;

HANDLE g_hThread = nullptr;
HANDLE g_hExitEvent = nullptr;
HWINEVENTHOOK g_hCreateHook = nullptr;
HWINEVENTHOOK g_hLocationHook = nullptr;

std::map<HWND, WNDPROC> g_subclassed;
bool g_active = false;
FILETIME g_iniWriteTime = {};

bool IsExplorerWindow(HWND hwnd)
{
    wchar_t cls[64] = {};
    if (!GetClassNameW(hwnd, cls, 64))
        return false;
    return _wcsicmp(cls, L"CabinetWClass") == 0 || _wcsicmp(cls, L"ExploreWClass") == 0;
}

LRESULT CALLBACK ExplorerSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC original = nullptr;
    auto it = g_subclassed.find(hwnd);
    if (it != g_subclassed.end())
        original = it->second;

    if (msg == WM_WINDOWPOSCHANGED || msg == WM_SIZE)
    {
        HWND panel = reinterpret_cast<HWND>(GetPropW(hwnd, L"PathHelperFavPanel"));
        if (panel && IsWindow(panel))
            PostMessageW(panel, WM_FAV_REPOSITION, 0, 0);
    }
    else if (msg == WM_NCDESTROY)
    {
        HWND panel = reinterpret_cast<HWND>(GetPropW(hwnd, L"PathHelperFavPanel"));
        if (panel)
        {
            RemovePropW(hwnd, L"PathHelperFavPanel");
            if (IsWindow(panel))
                PostMessageW(panel, WM_CLOSE, 0, 0);
        }

        g_subclassed.erase(hwnd);

        if (original)
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));

        return original ? CallWindowProcW(original, hwnd, msg, wp, lp)
                        : DefWindowProcW(hwnd, msg, wp, lp);
    }

    return original ? CallWindowProcW(original, hwnd, msg, wp, lp)
                    : DefWindowProcW(hwnd, msg, wp, lp);
}

void SubclassExplorer(HWND hwnd)
{
    if (g_subclassed.find(hwnd) != g_subclassed.end())
        return;
    WNDPROC original = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ExplorerSubclassProc)));
    if (original)
        g_subclassed[hwnd] = original;
}

void AttachToExplorer(HWND hwnd)
{
    if (!IsExplorerWindow(hwnd) || !IsWindow(hwnd))
        return;

    HWND existing = reinterpret_cast<HWND>(GetPropW(hwnd, L"PathHelperFavPanel"));
    if (existing && IsWindow(existing))
        return;

    HWND panel = AttachFavPanel(hwnd);
    if (panel)
    {
        SetPropW(hwnd, L"PathHelperFavPanel", reinterpret_cast<HANDLE>(panel));
        SubclassExplorer(hwnd);
    }
}

BOOL CALLBACK EnumExplorerProc(HWND hwnd, LPARAM lParam)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId())
        return TRUE;
    if (IsExplorerWindow(hwnd))
        reinterpret_cast<std::vector<HWND> *>(lParam)->push_back(hwnd);
    return TRUE;
}

void AttachToAllExplorers()
{
    // 注意：枚举过程中不能创建窗口（EnumWindows 的行为会变得不确定，
    // 新建的窗口可能不会被正确显示），所以先收集再挂载。
    std::vector<HWND> explorers;
    EnumWindows(EnumExplorerProc, reinterpret_cast<LPARAM>(&explorers));
    for (HWND hwnd : explorers)
        AttachToExplorer(hwnd);
}

void DetachAll()
{
    std::vector<HWND> explorers;
    for (const auto &kv : g_subclassed)
        explorers.push_back(kv.first);

    for (HWND hwnd : explorers)
    {
        HWND panel = reinterpret_cast<HWND>(GetPropW(hwnd, L"PathHelperFavPanel"));
        if (panel)
        {
            RemovePropW(hwnd, L"PathHelperFavPanel");
            if (IsWindow(panel))
                PostMessageW(panel, WM_CLOSE, 0, 0);
        }
        auto it = g_subclassed.find(hwnd);
        if (it != g_subclassed.end())
        {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(it->second));
            g_subclassed.erase(it);
        }
    }

    DetachAllFavPanels();
}

void PollIni()
{
    std::wstring ini = GetFavIniPath();
    if (ini.empty())
        return;

    bool settingsChanged = false;
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(ini.c_str(), GetFileExInfoStandard, &fad))
    {
        if (CompareFileTime(&fad.ftLastWriteTime, &g_iniWriteTime) != 0)
        {
            g_iniWriteTime = fad.ftLastWriteTime;
            settingsChanged = true;
        }
    }

    if (settingsChanged)
    {
        RefreshFavSettings();
        RepositionAllFavPanels();
        RefreshAllFavPanels();
    }

    bool enabled = ReadFavEnabledFromIni();

    if (!enabled)
    {
        if (g_active)
        {
            g_active = false;
            DetachAll();
        }
        return;
    }

    if (!g_active)
    {
        g_active = true;
    }

    // 每次轮询都补一次挂载：新打开的资源管理器窗口、以及钩子漏掉的窗口都能补上。
    // AttachToExplorer 是幂等的，开销很小。
    AttachToAllExplorers();
}

void CALLBACK FavWinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
{
    if (idObject != OBJID_WINDOW || !hwnd)
        return;

    switch (event)
    {
    case EVENT_OBJECT_CREATE:
    case EVENT_OBJECT_SHOW:
        if (g_active && IsExplorerWindow(hwnd))
            AttachToExplorer(hwnd);
        break;
    case EVENT_OBJECT_LOCATIONCHANGE:
        if (IsExplorerWindow(hwnd))
        {
            HWND panel = reinterpret_cast<HWND>(GetPropW(hwnd, L"PathHelperFavPanel"));
            if (panel && IsWindow(panel))
                PostMessageW(panel, WM_FAV_REPOSITION, 0, 0);
        }
        break;
    default:
        break;
    }
}

DWORD WINAPI FavHookThread(LPVOID)
{
    LoadFavSettings(g_favSettings);
    ComputeFavMetrics();
    EnsureFavFonts();
    RegisterFavPanelClass();

    OleInitialize(nullptr);

    g_iniWriteTime.dwLowDateTime = 0;
    g_iniWriteTime.dwHighDateTime = 0;

    g_active = ReadFavEnabledFromIni();
    if (g_active)
        AttachToAllExplorers();

    g_hCreateHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr,
                                    FavWinEventProc, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    g_hLocationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
                                      FavWinEventProc, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);

    // hwnd 为 NULL 的线程定时器：WM_TIMER 会被投递到本线程的消息队列
    SetTimer(nullptr, TIMER_POLL_SETTINGS, POLL_INTERVAL_MS, nullptr);

    MSG msg;
    HANDLE handles[1] = {g_hExitEvent};
    while (true)
    {
        DWORD wait = MsgWaitForMultipleObjects(1, handles, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0)
            break;

        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_TIMER && msg.hwnd == nullptr)
            {
                PollIni();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    KillTimer(nullptr, TIMER_POLL_SETTINGS);

    if (g_hCreateHook)
    {
        UnhookWinEvent(g_hCreateHook);
        g_hCreateHook = nullptr;
    }
    if (g_hLocationHook)
    {
        UnhookWinEvent(g_hLocationHook);
        g_hLocationHook = nullptr;
    }

    DetachAll();
    DetachAllFavPanels();

    if (g_hFavFont) { DeleteObject(g_hFavFont); g_hFavFont = nullptr; }
    if (g_hFavFontBold) { DeleteObject(g_hFavFontBold); g_hFavFontBold = nullptr; }
    if (g_hFavFontSecondary) { DeleteObject(g_hFavFontSecondary); g_hFavFontSecondary = nullptr; }
    if (g_hFavHeaderFont) { DeleteObject(g_hFavHeaderFont); g_hFavHeaderFont = nullptr; }

    OleUninitialize();
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_hFavInst = reinterpret_cast<HINSTANCE>(hModule);
        DisableThreadLibraryCalls(hModule);
        g_hExitEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_hThread = CreateThread(nullptr, 0, FavHookThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        if (g_hExitEvent)
            SetEvent(g_hExitEvent);
        if (g_hThread)
        {
            WaitForSingleObject(g_hThread, 5000);
            CloseHandle(g_hThread);
            g_hThread = nullptr;
        }
        if (g_hExitEvent)
        {
            CloseHandle(g_hExitEvent);
            g_hExitEvent = nullptr;
        }
        break;
    }
    return TRUE;
}
