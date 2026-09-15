#pragma once
#include <windows.h>

// ---------------------------------------------------------------------------
// 资源管理器收藏面板（Dll2.dll）的注入管理
//
// 只有 %USERPROFILE%\.PathHelper\Setting.ini 里的
// [Settings] ExplorerFavorites=true 时才会把 Dll2.dll 注入 explorer.exe。
// Dll2.dll 自己会轮询这个开关，关闭后 2 秒内自动撤掉收藏面板。
// ---------------------------------------------------------------------------

bool IsExplorerFavoritesEnabled();
void InitExplorerFavorites();
void MonitorExplorerFavorites();
void CleanupExplorerFavorites();
