#pragma once
#include <windows.h>

extern WCHAR g_szDllPath[260];    // Dll1.dll —— 文件对话框增强面板
extern WCHAR g_szFavDllPath[260]; // Dll2.dll —— 资源管理器收藏面板

BOOL InjectDll(DWORD pid);
BOOL InjectFavoritesDll(DWORD pid);
void ResolveDllPath(const WCHAR *exeDir);
