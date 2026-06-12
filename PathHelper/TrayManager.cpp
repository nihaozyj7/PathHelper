#include "framework.h"
#include <shellapi.h>
#include "TrayManager.h"
#include "Resource.h"

static NOTIFYICONDATAW g_nid;

BOOL ShowTrayIcon(HWND hWnd, HINSTANCE hInst)
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_PATHHELPER));
    wcscpy_s(g_nid.szTip, L"PathHelper");
    return Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void HideTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void ShowContextMenu(HWND hWnd)
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, L"\u663e\u793a");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"\u9000\u51fa");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}
