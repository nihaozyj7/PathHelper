#pragma once
#include <windows.h>

BOOL ShowTrayIcon(HWND hWnd, HINSTANCE hInst);
void HideTrayIcon();
void ShowContextMenu(HWND hWnd);
