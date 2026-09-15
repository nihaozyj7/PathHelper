#include "framework.h"
#include "Resource.h"
#include "SettingsManager.h"
#include "ProcessManager.h"
#include "AutoStartManager.h"
#include <string>
#include <algorithm>

static const struct
{
    const WCHAR *key;
    const WCHAR *defaultValue;
    int controlId;
    bool isCheckbox;
    bool isStringEdit;
    int intDefault;
} g_settingDefs[] = {
    {L"AutoToLatest", L"false", IDC_CHK_AUTOTOLATEST, true, false, 0},
    {L"theme", L"light", IDC_CBO_THEME, false, false, 0},
    {L"HistoryPanelWidth", L"200", IDC_EDIT_HISTORYWIDTH, false, false, 200},
    {L"HistoryPanelFontSize", L"10", IDC_EDIT_HISTORYFONTSIZE, false, false, 10},
    {L"PanelMargin", L"5", IDC_EDIT_PANELMARGIN, false, false, 5},
    {L"StripCommonPrefix", L"true", IDC_CHK_STRIPCOMMONPREFIX, true, false, 0},
    {L"HistoryDisplayMax", L"5", IDC_EDIT_HISTORYDISPLAYMAX, false, false, 5},
    {L"AutoStartToTray", L"false", IDC_CHK_AUTOSTARTTOTRAY, true, false, 0},
    {L"ExplorerFavorites", L"false", IDC_CHK_EXPLORERFAVORITES, true, false, 0},
    {L"EverythingPanel", L"true", IDC_CHK_EVERYTHINGPANEL, true, false, 0},
};

static const int g_settingCount = sizeof(g_settingDefs) / sizeof(g_settingDefs[0]);

void LoadSettingsToUI(HWND hChkAutoToLatest, HWND hCboTheme,
                       HWND hEditHistoryWidth, HWND hEditHistoryFontSize,
                       HWND hEditPanelMargin, HWND hChkStripCommonPrefix,
                       HWND hEditHistoryDisplayMax, HWND hEditTimeFormat, HWND hHotKeyTimePaste,
                       HWND hChkAutoStartToTray, HWND hChkExplorerFavorites,
                       HWND hChkEverythingPanel)
{
    HWND controls[] = {
        hChkAutoToLatest, hCboTheme,
        hEditHistoryWidth, hEditHistoryFontSize,
        hEditPanelMargin, hChkStripCommonPrefix, hEditHistoryDisplayMax, hChkAutoStartToTray,
        hChkExplorerFavorites, hChkEverythingPanel
    };

    WCHAR buf[2048];

    for (int i = 0; i < g_settingCount; i++)
    {
        if (g_settingDefs[i].isCheckbox)
        {
            GetPrivateProfileStringW(L"Settings", g_settingDefs[i].key, g_settingDefs[i].defaultValue, buf, 2048, g_szIniPath);
            SendMessageW(controls[i], BM_SETCHECK, (_wcsicmp(buf, L"true") == 0) ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        else if (g_settingDefs[i].controlId == IDC_CBO_THEME)
        {
            GetPrivateProfileStringW(L"Settings", g_settingDefs[i].key, g_settingDefs[i].defaultValue, buf, 2048, g_szIniPath);
            int sel = 0;
            if (_wcsicmp(buf, L"light") == 0)
                sel = 1;
            else if (_wcsicmp(buf, L"dark") == 0)
                sel = 2;
            SendMessageW(controls[i], CB_SETCURSEL, sel, 0);
        }
        else if (g_settingDefs[i].isStringEdit)
        {
            GetPrivateProfileStringW(L"Settings", g_settingDefs[i].key, g_settingDefs[i].defaultValue, buf, 2048, g_szIniPath);
            SetWindowTextW(controls[i], buf);
        }
        else
        {
            int val = GetPrivateProfileIntW(L"Settings", g_settingDefs[i].key, g_settingDefs[i].intDefault, g_szIniPath);
            _itow_s(val, buf, 2048, 10);
            SetWindowTextW(controls[i], buf);
        }
    }

    GetPrivateProfileStringW(L"Settings", L"TimeFormat", L"%Y-%m-%d %H:%M:%S", buf, 2048, g_szIniPath);
    SetWindowTextW(hEditTimeFormat, buf);

    UINT vk = GetPrivateProfileIntW(L"Settings", L"TimeHotkeyVK", 0, g_szIniPath);
    UINT modFlags = GetPrivateProfileIntW(L"Settings", L"TimeHotkeyMod", 0, g_szIniPath);
    if (vk != 0)
        SendMessageW(hHotKeyTimePaste, HKM_SETHOTKEY, MAKEWORD(vk, static_cast<BYTE>(modFlags)), 0);
    else
        SendMessageW(hHotKeyTimePaste, HKM_SETHOTKEY, 0, 0);
}

void SaveSettingsFromUI(HWND hChkAutoToLatest, HWND hCboTheme,
                        HWND hEditHistoryWidth, HWND hEditHistoryFontSize,
                        HWND hEditPanelMargin, HWND hChkStripCommonPrefix,
                        HWND hEditHistoryDisplayMax, HWND hEditTimeFormat,
                        HWND hChkAutoStartToTray, HWND hChkExplorerFavorites,
                        HWND hChkEverythingPanel)
{
    HWND controls[] = {
        hChkAutoToLatest, hCboTheme,
        hEditHistoryWidth, hEditHistoryFontSize,
        hEditPanelMargin, hChkStripCommonPrefix, hEditHistoryDisplayMax, hChkAutoStartToTray,
        hChkExplorerFavorites, hChkEverythingPanel
    };

    WCHAR buf[2048];

    for (int i = 0; i < g_settingCount; i++)
    {
        if (g_settingDefs[i].isCheckbox)
        {
            int chk = (SendMessageW(controls[i], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            WritePrivateProfileStringW(L"Settings", g_settingDefs[i].key, chk ? L"true" : L"false", g_szIniPath);
        }
        else if (g_settingDefs[i].controlId == IDC_CBO_THEME)
        {
            int sel = static_cast<int>(SendMessageW(controls[i], CB_GETCURSEL, 0, 0));
            const WCHAR *theme = L"auto";
            if (sel == 1)
                theme = L"light";
            else if (sel == 2)
                theme = L"dark";
            WritePrivateProfileStringW(L"Settings", g_settingDefs[i].key, theme, g_szIniPath);
        }
        else
        {
            GetWindowTextW(controls[i], buf, 2048);
            WritePrivateProfileStringW(L"Settings", g_settingDefs[i].key, buf, g_szIniPath);
        }
    }

    GetWindowTextW(hEditTimeFormat, buf, 2048);
    WritePrivateProfileStringW(L"Settings", L"TimeFormat", buf, g_szIniPath);

    if (SendMessageW(hChkAutoStartToTray, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        if (!EnableAutoStartToTray())
            MessageBoxW(hChkAutoStartToTray, L"\u521b\u5efa\u5f00\u673a\u81ea\u542f\u52a8\u4efb\u52a1\u5931\u8d25\uff0c\u8bf7\u786e\u4fdd\u7a0b\u5e8f\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u8fd0\u884c\u3002", L"PathHelper", MB_OK | MB_ICONWARNING);
    }
    else
    {
        DisableAutoStartToTray();
    }
}
