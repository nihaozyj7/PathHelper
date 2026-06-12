#pragma once
#include <windows.h>
#include <string>

extern std::wstring g_dlgSelectedProcess;

INT_PTR CALLBACK ProcessDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
