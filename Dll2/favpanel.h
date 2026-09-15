#pragma once

#include "framework.h"

// 收藏面板（每个资源管理器窗口一个）
void RegisterFavPanelClass();
HWND AttachFavPanel(HWND explorer);
void DetachFavPanel(HWND explorer);
void DetachAllFavPanels();
bool HasAnyFavPanel();
size_t FavPanelCount();
void PositionFavPanel(HWND explorer, HWND panel);
void RepositionAllFavPanels();
void RefreshAllFavPanels();
void RefreshFavSettings();
