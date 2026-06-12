#pragma once
#include <windows.h>

extern WCHAR g_szDllPath[260];

BOOL InjectDll(DWORD pid);
void ResolveDllPath(const WCHAR *exeDir);
