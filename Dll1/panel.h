#pragma once
#include <windows.h>

HWND CreateCompanionPanel(HWND hwndDialog);
LRESULT CALLBACK CompanionPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void PositionPanel(HWND hwndDialog, HWND hwndPanel);
void SetPanelRegion(HWND hwnd);
void RegisterCustomListClass();
void ReleaseDWriteCache();
