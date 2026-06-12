#include "framework.h"
#include <tlhelp32.h>
#include "ProcessManager.h"
#include "Injector.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>

std::vector<InjectionEntry> g_injectionEntries;
WCHAR g_szDataDir[260];
WCHAR g_szIniPath[260];

BOOL MatchesProcessName(const WCHAR *procExeName, const std::wstring &targetName)
{
    if (_wcsicmp(procExeName, targetName.c_str()) == 0)
        return TRUE;

    std::wstring procBase(procExeName);
    size_t dot = procBase.rfind(L'.');
    if (dot != std::wstring::npos)
    {
        procBase = procBase.substr(0, dot);
        if (_wcsicmp(procBase.c_str(), targetName.c_str()) == 0)
            return TRUE;
    }

    std::wstring targetExe = targetName + L".exe";
    if (_wcsicmp(procExeName, targetExe.c_str()) == 0)
        return TRUE;

    return FALSE;
}

void LoadInjectionList()
{
    g_injectionEntries.clear();
    int count = GetPrivateProfileIntW(L"Injection", L"Count", 0, g_szIniPath);
    WCHAR buf[2048];
    WCHAR key[32];
    for (int i = 0; i < count; i++)
    {
        _itow_s(i, key, 32, 10);
        GetPrivateProfileStringW(L"Injection", key, L"", buf, 2048, g_szIniPath);
        if (buf[0] != L'\0')
        {
            InjectionEntry entry;
            entry.processName = buf;
            g_injectionEntries.push_back(entry);
        }
    }
}

void SaveInjectionList()
{
    WCHAR buf[32];
    int oldCount = GetPrivateProfileIntW(L"Injection", L"Count", 0, g_szIniPath);

    _itow_s(static_cast<int>(g_injectionEntries.size()), buf, 32, 10);
    WritePrivateProfileStringW(L"Injection", L"Count", buf, g_szIniPath);
    for (size_t i = 0; i < g_injectionEntries.size(); i++)
    {
        _itow_s(static_cast<int>(i), buf, 32, 10);
        WritePrivateProfileStringW(L"Injection", buf, g_injectionEntries[i].processName.c_str(), g_szIniPath);
    }

    for (int i = static_cast<int>(g_injectionEntries.size()); i < oldCount; i++)
    {
        _itow_s(i, buf, 32, 10);
        WritePrivateProfileStringW(L"Injection", buf, NULL, g_szIniPath);
    }
}

int RefreshEntryStatus(size_t index)
{
    if (index >= g_injectionEntries.size())
        return 0;

    InjectionEntry &entry = g_injectionEntries[index];

    entry.runningPids.clear();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            if (MatchesProcessName(pe.szExeFile, entry.processName))
                entry.runningPids.push_back(pe.th32ProcessID);
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    std::vector<DWORD> stillAlive;
    for (DWORD pid : entry.injectedPids)
    {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (hProc)
        {
            DWORD code;
            if (GetExitCodeProcess(hProc, &code) && code == STILL_ACTIVE)
                stillAlive.push_back(pid);
            CloseHandle(hProc);
        }
    }
    entry.injectedPids.clear();
    for (DWORD pid : stillAlive)
        entry.injectedPids.insert(pid);

    return GetEntryStatus(entry);
}

int GetEntryStatus(const InjectionEntry &entry)
{
    if (entry.runningPids.empty())
        return 0;

    bool allInjected = true;
    for (DWORD pid : entry.runningPids)
    {
        if (entry.injectedPids.find(pid) == entry.injectedPids.end())
        {
            allInjected = false;
            break;
        }
    }

    if (allInjected)
        return 2;

    bool anyInjected = false;
    for (DWORD pid : entry.runningPids)
    {
        if (entry.injectedPids.find(pid) != entry.injectedPids.end())
        {
            anyInjected = true;
            break;
        }
    }
    return anyInjected ? 2 : 1;
}

void PerformInjectAction(size_t index)
{
    if (index >= g_injectionEntries.size())
        return;

    InjectionEntry &entry = g_injectionEntries[index];

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            if (MatchesProcessName(pe.szExeFile, entry.processName))
            {
                if (entry.injectedPids.find(pe.th32ProcessID) == entry.injectedPids.end())
                {
                    if (InjectDll(pe.th32ProcessID))
                        entry.injectedPids.insert(pe.th32ProcessID);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

void MonitorProcesses()
{
    if (g_injectionEntries.empty())
        return;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            for (auto &entry : g_injectionEntries)
            {
                if (MatchesProcessName(pe.szExeFile, entry.processName))
                {
                    if (entry.injectedPids.find(pe.th32ProcessID) == entry.injectedPids.end())
                    {
                        if (InjectDll(pe.th32ProcessID))
                            entry.injectedPids.insert(pe.th32ProcessID);
                    }
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    for (auto &entry : g_injectionEntries)
    {
        std::vector<DWORD> stillAlive;
        for (DWORD pid : entry.injectedPids)
        {
            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
            if (hProc)
            {
                DWORD code;
                if (GetExitCodeProcess(hProc, &code) && code == STILL_ACTIVE)
                    stillAlive.push_back(pid);
                CloseHandle(hProc);
            }
        }
        entry.injectedPids.clear();
        for (DWORD pid : stillAlive)
            entry.injectedPids.insert(pid);
    }
}

void RefreshInjectionListView(HWND hList)
{
    ListView_DeleteAllItems(hList);
    for (size_t i = 0; i < g_injectionEntries.size(); i++)
    {
        int status = RefreshEntryStatus(i);

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = static_cast<int>(i);
        lvi.pszText = const_cast<LPWSTR>(g_injectionEntries[i].processName.c_str());
        ListView_InsertItem(hList, &lvi);

        const WCHAR *statusText = L"\u672a\u8fd0\u884c";
        if (status == 1)
            statusText = L"\u672a\u76d1\u542c";
        else if (status == 2)
            statusText = L"\u76d1\u542c\u4e2d";
        ListView_SetItemText(hList, static_cast<int>(i), 1, const_cast<LPWSTR>(statusText));
    }
}
