#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <windows.h>

struct InjectionEntry
{
    std::wstring processName;
    std::vector<DWORD> runningPids;
    std::unordered_set<DWORD> injectedPids;
};

extern std::vector<InjectionEntry> g_injectionEntries;
extern WCHAR g_szDataDir[260];
extern WCHAR g_szIniPath[260];

BOOL MatchesProcessName(const WCHAR *procExeName, const std::wstring &targetName);
void LoadInjectionList();
void SaveInjectionList();
int RefreshEntryStatus(size_t index);
int GetEntryStatus(const InjectionEntry &entry);
void PerformInjectAction(size_t index);
void MonitorProcesses();
void RefreshInjectionListView(HWND hList);
