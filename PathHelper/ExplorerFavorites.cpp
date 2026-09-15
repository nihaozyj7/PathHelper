#include "framework.h"
#include <tlhelp32.h>
#include "ExplorerFavorites.h"
#include "Injector.h"
#include "ProcessManager.h"
#include <vector>
#include <unordered_set>

static std::unordered_set<DWORD> g_injectedExplorers;
static bool g_lastEnabled = false;

bool IsExplorerFavoritesEnabled()
{
    WCHAR buf[32] = {};
    GetPrivateProfileStringW(L"Settings", L"ExplorerFavorites", L"false", buf, 32, g_szIniPath);
    return _wcsicmp(buf, L"true") == 0 || wcscmp(buf, L"1") == 0 || _wcsicmp(buf, L"yes") == 0;
}

void InitExplorerFavorites()
{
    g_lastEnabled = IsExplorerFavoritesEnabled();
}

static void PruneDeadExplorers()
{
    std::vector<DWORD> alive;
    for (DWORD pid : g_injectedExplorers)
    {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (hProc)
        {
            DWORD code = 0;
            if (GetExitCodeProcess(hProc, &code) && code == STILL_ACTIVE)
                alive.push_back(pid);
            CloseHandle(hProc);
        }
    }

    g_injectedExplorers.clear();
    for (DWORD pid : alive)
        g_injectedExplorers.insert(pid);
}

void MonitorExplorerFavorites()
{
    bool enabled = IsExplorerFavoritesEnabled();
    g_lastEnabled = enabled;

    PruneDeadExplorers();

    if (!enabled)
    {
        // 不主动卸载（无法从 explorer.exe 里安全卸载 DLL），
        // Dll2.dll 会自己轮询开关并撤掉面板。
        return;
    }

    if (g_szFavDllPath[0] == L'\0')
        return;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            if (!MatchesProcessName(pe.szExeFile, L"explorer"))
                continue;
            if (g_injectedExplorers.find(pe.th32ProcessID) != g_injectedExplorers.end())
                continue;

            if (InjectFavoritesDll(pe.th32ProcessID))
                g_injectedExplorers.insert(pe.th32ProcessID);
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

void CleanupExplorerFavorites()
{
    g_injectedExplorers.clear();
}
