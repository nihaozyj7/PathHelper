// PathHelper.cpp
//

#include "framework.h"
#include "PathHelper.h"
#include "Injector.h"
#include "ProcessManager.h"
#include "SettingsManager.h"
#include "TrayManager.h"
#include "Dialogs.h"
#include "ExplorerMonitor.h"
#include "AutoStartManager.h"
#include "ExplorerFavorites.h"
#include <commctrl.h>
#include <shellapi.h>
#include <ctime>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define MAX_LOADSTRING 100

#define WINDOW_W 640
#define WINDOW_H 644
#define TAB_H 32
#define PAGE_Y 38
#define PAGE_X 20
#define PAGE_W (WINDOW_W - 2 * PAGE_X)
#define PAGE_H (WINDOW_H - PAGE_Y - 24)
#define ROW_H 38
#define LABEL_W 150
#define EDIT_X 170
#define EDIT_W_SM 100
#define EDIT_W_LG 420
#define GROUP_MARGIN 12
#define GROUP_PADDING 16
#define CARD_RADIUS 8

static HFONT g_hFontUI = NULL;

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

HWND g_hTab = NULL;
HWND g_hInjList = NULL;
HWND g_hBtnAdd = NULL;
HWND g_hBtnRemove = NULL;

HWND g_hChkAutoToLatest = NULL;
HWND g_hCboTheme = NULL;
HWND g_hEditHistoryWidth = NULL;
HWND g_hEditHistoryFontSize = NULL;
HWND g_hEditPanelMargin = NULL;
HWND g_hEditTimeFormat = NULL;
HWND g_hHotKeyTimePaste = NULL;
HWND g_hChkAutoStartToTray = NULL;
HWND g_hChkStripCommonPrefix = NULL;
HWND g_hEditHistoryDisplayMax = NULL;
HWND g_hChkExplorerFavorites = NULL;
HWND g_hChkEverythingPanel = NULL;
HWND g_hWndMain = NULL;
bool g_timeHotkeyRegistered = false;
bool g_autoStartToTray = false;
static UINT g_uTaskbarRestart = 0;

std::vector<HWND> g_injControls;
std::vector<HWND> g_setControls;

int g_nCurrentTab = 0;

static void SetFontForControl(HWND hWnd, HWND hCtrl)
{
    SendMessageW(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontUI), TRUE);
}

static void SetFontForAllChildren(HWND hWnd)
{
    g_hFontUI = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    EnumChildWindows(hWnd, [](HWND child, LPARAM lParam) -> BOOL
                     {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontUI), TRUE);
        return TRUE; }, 0);
}

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
void InitDataDirectory();
void AddInjectionProcess(HWND hWnd);
void RemoveInjectionProcess();
void SwitchTab(HWND hWnd, int index);

void InitDataDirectory()
{
    WCHAR userProfile[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH);
    if (len == 0 || len > MAX_PATH)
    {
        // fallback to exe dir if USERPROFILE not available
        GetModuleFileNameW(NULL, userProfile, MAX_PATH);
        WCHAR *p = wcsrchr(userProfile, L'\\');
        if (p)
            *p = L'\0';
    }

    _snwprintf_s(g_szDataDir, MAX_PATH, _TRUNCATE, L"%s\\.PathHelper", userProfile);
    _snwprintf_s(g_szIniPath, MAX_PATH, _TRUNCATE, L"%s\\Setting.ini", g_szDataDir);

    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    WCHAR *p = wcsrchr(exeDir, L'\\');
    if (p)
        *p = L'\0';

    ResolveDllPath(exeDir);

    CreateDirectoryW(g_szDataDir, NULL);

    if (GetFileAttributesW(g_szIniPath) == INVALID_FILE_ATTRIBUTES)
    {
        WritePrivateProfileStringW(L"Settings", L"AutoToLatest", L"false", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"theme", L"light", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"HistoryPanelWidth", L"200", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"HistoryPanelFontSize", L"10", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"PanelMargin", L"5", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"StripCommonPrefix", L"true", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"HistoryDisplayMax", L"5", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"AutoStartToTray", L"false", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"TimeFormat", L"%Y-%m-%d %H:%M:%S", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"TimeHotkeyVK", L"0", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"TimeHotkeyMod", L"0", g_szIniPath);
        WritePrivateProfileStringW(L"Settings", L"ExplorerFavorites", L"false", g_szIniPath);
        WritePrivateProfileStringW(L"Injection", L"Count", L"0", g_szIniPath);
    }

    // 旧版本升级上来的 ini 里可能没有这一项，补一个默认值，
    // 这样注入到 explorer.exe 的 Dll2.dll 才能读到开关。
    WCHAR explorerFav[32] = {};
    GetPrivateProfileStringW(L"Settings", L"ExplorerFavorites", L"", explorerFav, 32, g_szIniPath);
    if (explorerFav[0] == L'\0')
        WritePrivateProfileStringW(L"Settings", L"ExplorerFavorites", L"false", g_szIniPath);
}

void AddInjectionProcess(HWND hWnd)
{
    g_dlgSelectedProcess.clear();
    INT_PTR result = DialogBoxW(hInst, MAKEINTRESOURCEW(IDD_PROCESS_DIALOG), hWnd, ProcessDlgProc);
    if (result == IDOK && !g_dlgSelectedProcess.empty())
    {
        for (const auto &entry : g_injectionEntries)
        {
            if (_wcsicmp(entry.processName.c_str(), g_dlgSelectedProcess.c_str()) == 0)
                return;
        }
        InjectionEntry entry;
        entry.processName = g_dlgSelectedProcess;
        g_injectionEntries.push_back(entry);
        SaveInjectionList();
        RefreshInjectionListView(g_hInjList);
        PerformInjectAction(g_injectionEntries.size() - 1);
    }
}

void RemoveInjectionProcess()
{
    int idx = ListView_GetNextItem(g_hInjList, -1, LVNI_SELECTED);
    if (idx >= 0 && idx < static_cast<int>(g_injectionEntries.size()))
    {
        g_injectionEntries.erase(g_injectionEntries.begin() + idx);
        SaveInjectionList();
        RefreshInjectionListView(g_hInjList);
    }
}

static void UnregisterTimeHotkey()
{
    if (g_timeHotkeyRegistered)
    {
        UnregisterHotKey(g_hWndMain, IDH_TIMEPASTE);
        g_timeHotkeyRegistered = false;
    }
}

static void RegisterTimeHotkeyFromINI()
{
    UINT vk = GetPrivateProfileIntW(L"Settings", L"TimeHotkeyVK", 0, g_szIniPath);
    UINT modFlags = GetPrivateProfileIntW(L"Settings", L"TimeHotkeyMod", 0, g_szIniPath);

    if (vk == 0)
        return;

    UINT mod = MOD_NOREPEAT;
    if (modFlags & HOTKEYF_CONTROL) mod |= MOD_CONTROL;
    if (modFlags & HOTKEYF_ALT) mod |= MOD_ALT;
    if (modFlags & HOTKEYF_SHIFT) mod |= MOD_SHIFT;

    if (RegisterHotKey(g_hWndMain, IDH_TIMEPASTE, mod, vk))
        g_timeHotkeyRegistered = true;
}

static void RegisterTimeHotkeyFromControl()
{
    UnregisterTimeHotkey();

    DWORD hk = static_cast<DWORD>(SendMessageW(g_hHotKeyTimePaste, HKM_GETHOTKEY, 0, 0));
    BYTE vk = LOBYTE(LOWORD(hk));
    BYTE modFlags = HIBYTE(LOWORD(hk));

    if (vk == 0)
        return;

    UINT mod = MOD_NOREPEAT;
    if (modFlags & HOTKEYF_CONTROL) mod |= MOD_CONTROL;
    if (modFlags & HOTKEYF_ALT) mod |= MOD_ALT;
    if (modFlags & HOTKEYF_SHIFT) mod |= MOD_SHIFT;

    if (RegisterHotKey(g_hWndMain, IDH_TIMEPASTE, mod, vk))
    {
        g_timeHotkeyRegistered = true;
    }
    else
    {
        MessageBoxW(g_hWndMain, L"\u5feb\u6377\u952e\u6ce8\u518c\u5931\u8d25\uff0c\u53ef\u80fd\u4e0e\u5176\u4ed6\u7a0b\u5e8f\u51b2\u7a81\u3002", L"PathHelper", MB_OK | MB_ICONWARNING);
    }

    WCHAR vkStr[16], modStr[16];
    _itow_s(vk, vkStr, 10);
    _itow_s(modFlags, modStr, 10);
    WritePrivateProfileStringW(L"Settings", L"TimeHotkeyVK", vkStr, g_szIniPath);
    WritePrivateProfileStringW(L"Settings", L"TimeHotkeyMod", modStr, g_szIniPath);
}

static void PasteCurrentTime()
{
    WCHAR formatBuf[256];
    GetPrivateProfileStringW(L"Settings", L"TimeFormat", L"%Y-%m-%d %H:%M:%S", formatBuf, 256, g_szIniPath);

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_s(&tm_now, &now);

    WCHAR timeBuf[256];
    wcsftime(timeBuf, 256, formatBuf, &tm_now);

    size_t len = wcslen(timeBuf);
    if (len == 0) return;

    std::vector<INPUT> inputs(len * 2);
    for (size_t i = 0; i < len; i++)
    {
        inputs[i * 2].type = INPUT_KEYBOARD;
        inputs[i * 2].ki.wScan = static_cast<WORD>(timeBuf[i]);
        inputs[i * 2].ki.dwFlags = KEYEVENTF_UNICODE;

        inputs[i * 2 + 1].type = INPUT_KEYBOARD;
        inputs[i * 2 + 1].ki.wScan = static_cast<WORD>(timeBuf[i]);
        inputs[i * 2 + 1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    }

    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void SwitchTab(HWND hWnd, int index)
{
    g_nCurrentTab = index;
    TabCtrl_SetCurSel(g_hTab, index);

    for (HWND h : g_injControls)
        ShowWindow(h, index == 0 ? SW_SHOW : SW_HIDE);
    for (HWND h : g_setControls)
        ShowWindow(h, index == 1 ? SW_SHOW : SW_HIDE);

    if (index == 1)
        LoadSettingsToUI(g_hChkAutoToLatest, g_hCboTheme,
                         g_hEditHistoryWidth, g_hEditHistoryFontSize,
                         g_hEditPanelMargin, g_hChkStripCommonPrefix,
                         g_hEditHistoryDisplayMax, g_hEditTimeFormat, g_hHotKeyTimePaste,
                         g_hChkAutoStartToTray, g_hChkExplorerFavorites,
                         g_hChkEverythingPanel);
}

static void CreateInjectionTabControls(HWND hWnd)
{
    int groupY = PAGE_Y + 8;
    // 说明区域高度：3行文字 + padding
    int descGroupH = 105;
    int groupH = PAGE_H - descGroupH - 18;
    
    // 第一个分组：注入程序列表
    HWND hGroup1 = CreateWindowExW(0, L"BUTTON", L" \U0001F4CB \u6ce8\u5165\u7a0b\u5e8f\u5217\u8868 ",
                                   WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                   PAGE_X, groupY, PAGE_W, groupH, hWnd, NULL, hInst, NULL);
    g_injControls.push_back(hGroup1);
    
    int listX = PAGE_X + GROUP_MARGIN;
    int listY = groupY + GROUP_PADDING + 8;
    int listW = PAGE_W - 2 * GROUP_MARGIN;
    int listH = groupH - 2 * GROUP_PADDING - 16;
    
    g_hInjList = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 listX, listY, listW, listH - 50, hWnd, reinterpret_cast<HMENU>(IDC_INJECT_LIST), hInst, NULL);
    ListView_SetExtendedListViewStyle(g_hInjList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    HIMAGELIST hImgList = ImageList_Create(1, 26, ILC_COLOR, 0, 1);
    ListView_SetImageList(g_hInjList, hImgList, LVSIL_SMALL);

    LVCOLUMNW lvc0 = {LVCF_TEXT | LVCF_WIDTH};
    lvc0.pszText = const_cast<LPWSTR>(L"\u5e94\u7528\u540d\u79f0");
    lvc0.cx = listW - 160;
    ListView_InsertColumn(g_hInjList, 0, &lvc0);

    LVCOLUMNW lvc1 = {LVCF_TEXT | LVCF_WIDTH};
    lvc1.pszText = const_cast<LPWSTR>(L"\u72b6\u6001");
    lvc1.cx = 145;
    ListView_InsertColumn(g_hInjList, 1, &lvc1);

    g_injControls.push_back(g_hInjList);
    
    // 按钮区域
    int btnY = listY + listH - 42;
    g_hBtnAdd = CreateWindowExW(0, L"BUTTON", L" \u2795 \u6dfb\u52a0\u5e94\u7528 ",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                listX, btnY, 110, 32, hWnd, reinterpret_cast<HMENU>(IDC_BTN_ADD), hInst, NULL);
    g_injControls.push_back(g_hBtnAdd);

    g_hBtnRemove = CreateWindowExW(0, L"BUTTON", L" \u2796 \u79fb\u9664\u5e94\u7528 ",
                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   listX + 120, btnY, 110, 32, hWnd, reinterpret_cast<HMENU>(IDC_BTN_REMOVE), hInst, NULL);
    g_injControls.push_back(g_hBtnRemove);
    
    // 第二个分组：说明信息（紧凑布局）
    int group2Y = groupY + groupH + 6;
    
    HWND hGroup2 = CreateWindowExW(0, L"BUTTON", L" \U00002139\uFE0F \u4f7f\u7528\u8bf4\u660e ",
                                   WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                   PAGE_X, group2Y, PAGE_W, descGroupH, hWnd, NULL, hInst, NULL);
    g_injControls.push_back(hGroup2);
    
    HWND hDesc = CreateWindowExW(0, L"STATIC",
                                 L"\u25B8 \u5c06DLL\u6ce8\u5165\u76ee\u6807\u7a0b\u5e8f\uff0c\u76d1\u542c\u6587\u4ef6/\u6587\u4ef6\u5939\u5bf9\u8bdd\u6846\r\n"
                                 L"\u25B8 \u7528\u6237\u9009\u4e2d\u8def\u5f84\u65f6\u81ea\u52a8\u8bb0\u5f55\uff0c\u6253\u5f00\u5bf9\u8bdd\u6846\u65f6\u663e\u793a\u5386\u53f2\u8def\u5f84\u9762\u677f\r\n"
                                 L"\u25B8 \u672c\u7a0b\u5e8f\u9700\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u8fd0\u884c",
                                 WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 PAGE_X + GROUP_MARGIN + 12, group2Y + GROUP_PADDING + 8, PAGE_W - 2 * GROUP_MARGIN - 24, descGroupH - 2 * GROUP_PADDING - 12, hWnd, NULL, hInst, NULL);
    g_injControls.push_back(hDesc);
}

static void CreateSettingsTabControls(HWND hWnd)
{
    int groupY = PAGE_Y + 8;
    int groupH = PAGE_H - 16;
    
    // 设置分组
    HWND hGroup = CreateWindowExW(0, L"BUTTON", L" \U00002699\uFE0F \u5e94\u7528\u8bbe\u7f6e ",
                                  WS_CHILD | BS_GROUPBOX,
                                  PAGE_X, groupY, PAGE_W, groupH, hWnd, NULL, hInst, NULL);
    g_setControls.push_back(hGroup);
    
    int y = groupY + GROUP_PADDING + 10;
    int labelX = PAGE_X + GROUP_MARGIN;
    int editX = labelX + LABEL_W + 10;
    int contentW = PAGE_W - 2 * GROUP_MARGIN - LABEL_W - 20;
    
    // 第一行：自动定位到最新
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F4CD \u81ea\u52a8\u5b9a\u4f4d\u5230\u6700\u65b0:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hChkAutoToLatest = CreateWindowExW(0, L"BUTTON", L"",
                                         WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         editX, y, 20, 24, hWnd, reinterpret_cast<HMENU>(IDC_CHK_AUTOTOLATEST), hInst, NULL);
    g_setControls.push_back(g_hChkAutoToLatest);

    y += ROW_H;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F3A8 \u4e3b\u9898:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hCboTheme = CreateWindowExW(0, L"COMBOBOX", L"",
                                  WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                  editX, y - 2, 130, 120, hWnd, reinterpret_cast<HMENU>(IDC_CBO_THEME), hInst, NULL);
    SendMessageW(g_hCboTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\U0001F570\uFE0F Auto"));
    SendMessageW(g_hCboTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u2600\uFE0F Light"));
    SendMessageW(g_hCboTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\U0001F319 Dark"));
    SendMessageW(g_hCboTheme, CB_SETCURSEL, 0, 0);
    g_setControls.push_back(g_hCboTheme);

    y += ROW_H;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F3C3 \u53bb\u9664\u516c\u5171\u8def\u5f84\u524d\u7f00:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hChkStripCommonPrefix = CreateWindowExW(0, L"BUTTON", L"",
                                               WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                                               editX, y, 20, 24, hWnd, reinterpret_cast<HMENU>(IDC_CHK_STRIPCOMMONPREFIX), hInst, NULL);
    g_setControls.push_back(g_hChkStripCommonPrefix);

    y += ROW_H;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F680 \u5f00\u673a\u81ea\u542f\u52a8\u5230\u6258\u76d8:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hChkAutoStartToTray = CreateWindowExW(0, L"BUTTON", L"",
                                             WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                                             editX, y, 20, 24, hWnd, reinterpret_cast<HMENU>(IDC_CHK_AUTOSTARTTOTRAY), hInst, NULL);
    g_setControls.push_back(g_hChkAutoStartToTray);

    y += ROW_H;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F4C2 \u8d44\u6e90\u7ba1\u7406\u5668\u6536\u85cf\u9762\u677f:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hChkExplorerFavorites = CreateWindowExW(0, L"BUTTON", L"",
                                              WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                                              editX, y, 20, 24, hWnd, reinterpret_cast<HMENU>(IDC_CHK_EXPLORERFAVORITES), hInst, NULL);
    g_setControls.push_back(g_hChkExplorerFavorites);
    g_setControls.push_back(CreateWindowExW(0, L"STATIC",
                                            L"\u5f00\u542f\u540e\u5411 explorer.exe \u6ce8\u5165\u4ec5\u4fdd\u7559\u6536\u85cf\u7684\u4fa7\u8fb9\u9762\u677f",
                                            WS_CHILD | SS_LEFT,
                                            editX + 28, y + 2, contentW - 28, 22, hWnd, NULL, hInst, NULL));

    y += ROW_H;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F50D Everything \u641c\u7d22\u9762\u677f:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hChkEverythingPanel = CreateWindowExW(0, L"BUTTON", L"",
                                            WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                                            editX, y, 20, 24, hWnd, reinterpret_cast<HMENU>(IDC_CHK_EVERYTHINGPANEL), hInst, NULL);
    g_setControls.push_back(g_hChkEverythingPanel);
    g_setControls.push_back(CreateWindowExW(0, L"STATIC",
                                            L"\u5728\u6587\u4ef6\u5bf9\u8bdd\u6846\u7684\u641c\u7d22\u6846\u4e0b\u663e\u793a Everything \u7ed3\u679c",
                                            WS_CHILD | SS_LEFT,
                                            editX + 28, y + 2, contentW - 28, 22, hWnd, NULL, hInst, NULL));

    y += ROW_H + 4;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F4D0 \u5386\u53f2\u9762\u677f\u5bbd\u5ea6:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hEditHistoryWidth = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                          editX, y - 2, EDIT_W_SM, 24, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_HISTORYWIDTH), hInst, NULL);
    g_setControls.push_back(g_hEditHistoryWidth);

    y += ROW_H;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F524 \u5386\u53f2\u8bb0\u5f55\u5b57\u4f53:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hEditHistoryFontSize = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                             WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                             editX, y - 2, EDIT_W_SM, 24, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_HISTORYFONTSIZE), hInst, NULL);
    g_setControls.push_back(g_hEditHistoryFontSize);

    y += ROW_H;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F4CF \u9762\u677f\u8fb9\u8ddd:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hEditPanelMargin = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                         WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                         editX, y - 2, EDIT_W_SM, 24, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_PANELMARGIN), hInst, NULL);
    g_setControls.push_back(g_hEditPanelMargin);

    y += ROW_H;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F4C2 \u5386\u53f2\u6700\u5927\u663e\u793a\u6761\u6570:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hEditHistoryDisplayMax = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                               WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                               editX, y - 2, EDIT_W_SM, 24, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_HISTORYDISPLAYMAX), hInst, NULL);
    g_setControls.push_back(g_hEditHistoryDisplayMax);

    y += ROW_H + 4;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\U0001F4C5 \u65f6\u95f4\u683c\u5f0f:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hEditTimeFormat = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                        editX, y - 2, EDIT_W_LG, 24, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_TIMEFORMAT), hInst, NULL);
    g_setControls.push_back(g_hEditTimeFormat);

    y += ROW_H;
    g_setControls.push_back(CreateWindowExW(0, L"STATIC", L"\u2328\uFE0F \u7c98\u8d34\u65f6\u95f4\u5feb\u6377\u952e:",
                                            WS_CHILD | SS_RIGHT,
                                            labelX, y, LABEL_W, 24, hWnd, NULL, hInst, NULL));
    g_hHotKeyTimePaste = CreateWindowExW(0, HOTKEY_CLASSW, L"",
                                         WS_CHILD | WS_TABSTOP | WS_BORDER,
                                         editX, y - 2, 170, 24, hWnd, reinterpret_cast<HMENU>(IDC_HOTKEY_TIMEPASTE), hInst, NULL);
    SendMessageW(g_hHotKeyTimePaste, HKM_SETRULES, HKCOMB_NONE | HKCOMB_S, 0);
    g_setControls.push_back(g_hHotKeyTimePaste);

    y += ROW_H + 14;
    HWND hBtnSave = CreateWindowExW(0, L"BUTTON", L" \U0001F4BE \u4fdd\u5b58\u8bbe\u7f6e ",
                                    WS_CHILD | BS_PUSHBUTTON,
                                    editX, y, 130, 34, hWnd, reinterpret_cast<HMENU>(IDC_BTN_SAVESETTINGS), hInst, NULL);
    g_setControls.push_back(hBtnSave);

    for (HWND h : g_setControls)
        ShowWindow(h, SW_HIDE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (g_uTaskbarRestart && message == g_uTaskbarRestart)
    {
        KillTimer(hWnd, IDT_TRAY_RETRY);
        ShowTrayIcon(hWnd, hInst);
        return 0;
    }

    switch (message)
    {
    case WM_CREATE:
    {
        g_hWndMain = hWnd;
        g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                 WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH | TCS_HOTTRACK,
                                 8, 8, WINDOW_W - 16, TAB_H, hWnd, reinterpret_cast<HMENU>(IDC_TAB), hInst, NULL);
        TabCtrl_SetItemSize(g_hTab, 120, TAB_H - 4);

        TCITEMW tci = {TCIF_TEXT};
        tci.pszText = const_cast<LPWSTR>(L" \U0001F50D \u76d1\u542c\u5e94\u7528 ");
        TabCtrl_InsertItem(g_hTab, 0, &tci);
        tci.pszText = const_cast<LPWSTR>(L" \U00002699\uFE0F \u8bbe\u7f6e ");
        TabCtrl_InsertItem(g_hTab, 1, &tci);
        TabCtrl_SetCurSel(g_hTab, 0);

        CreateInjectionTabControls(hWnd);
        CreateSettingsTabControls(hWnd);

        SetFontForAllChildren(hWnd);

        ShowTrayIcon(hWnd, hInst);
        SetTimer(hWnd, IDT_TRAY_RETRY, 2000, NULL);
        SetTimer(hWnd, IDT_PROCESS_MONITOR, 2000, NULL);
        SetTimer(hWnd, IDT_EXPLORER_MONITOR, 3000, NULL);
        RefreshInjectionListView(g_hInjList);

        PostMessageW(hWnd, WM_TIMER, IDT_PROCESS_MONITOR, 0);
        PostMessageW(hWnd, WM_TIMER, IDT_EXPLORER_MONITOR, 0);
        RegisterTimeHotkeyFromINI();
        g_uTaskbarRestart = RegisterWindowMessageW(L"TaskbarCreated");
    }
        return 0;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDC_BTN_ADD:
            AddInjectionProcess(hWnd);
            break;
        case IDC_BTN_REMOVE:
            RemoveInjectionProcess();
            break;
        case IDC_BTN_SAVESETTINGS:
SaveSettingsFromUI(g_hChkAutoToLatest, g_hCboTheme,
                                g_hEditHistoryWidth, g_hEditHistoryFontSize,
                                g_hEditPanelMargin, g_hChkStripCommonPrefix,
                                g_hEditHistoryDisplayMax, g_hEditTimeFormat,
                                g_hChkAutoStartToTray, g_hChkExplorerFavorites,
                                g_hChkEverythingPanel);
            RegisterTimeHotkeyFromControl();
            MonitorExplorerFavorites();
            MessageBoxW(hWnd, L"\u8bbe\u7f6e\u5df2\u4fdd\u5b58\u3002", L"PathHelper", MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_ABOUT:
            DialogBoxW(hInst, MAKEINTRESOURCEW(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            HideTrayIcon();
            KillTimer(hWnd, IDT_PROCESS_MONITOR);
            KillTimer(hWnd, IDT_EXPLORER_MONITOR);
            DestroyWindow(hWnd);
            break;
        case IDM_TRAY_SHOW:
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
            break;
        case IDM_TRAY_EXIT:
            HideTrayIcon();
            KillTimer(hWnd, IDT_PROCESS_MONITOR);
            KillTimer(hWnd, IDT_EXPLORER_MONITOR);
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_TIMER:
        if (wParam == IDT_TRAY_RETRY)
        {
            if (ShowTrayIcon(hWnd, hInst))
                KillTimer(hWnd, IDT_TRAY_RETRY);
        }
        else if (wParam == IDT_PROCESS_MONITOR)
        {
            MonitorProcesses();
            MonitorExplorerFavorites();
            RefreshInjectionListView(g_hInjList);
        }
        else if (wParam == IDT_EXPLORER_MONITOR)
        {
            UpdateExplorerPaths();
        }
        break;

    case WM_HOTKEY:
        if (wParam == IDH_TIMEPASTE)
            PasteCurrentTime();
        break;

    case WM_NOTIFY:
    {
        NMHDR *nm = reinterpret_cast<NMHDR *>(lParam);
        if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE)
        {
            int sel = TabCtrl_GetCurSel(g_hTab);
            SwitchTab(hWnd, sel);
        }
    }
    break;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP)
        {
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
        }
        else if (lParam == WM_RBUTTONUP)
        {
            ShowContextMenu(hWnd);
        }
        break;

    case WM_CLOSE:
    {
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    }

    case WM_DESTROY:
        UnregisterTimeHotkey();
        if (g_hFontUI)
        {
            DeleteObject(g_hFontUI);
            g_hFontUI = NULL;
        }
        HideTrayIcon();
        KillTimer(hWnd, IDT_TRAY_RETRY);
        KillTimer(hWnd, IDT_PROCESS_MONITOR);
        KillTimer(hWnd, IDT_EXPLORER_MONITOR);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return static_cast<INT_PTR>(TRUE);
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return static_cast<INT_PTR>(TRUE);
        }
        break;
    }
    return static_cast<INT_PTR>(FALSE);
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PATHHELPER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(245, 245, 250));  // 浅灰色背景
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    RECT rc = {0, 0, WINDOW_W, WINDOW_H};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    HWND hWnd = CreateWindowW(szWindowClass, szTitle,
                              WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                              CW_USEDEFAULT, 0,
                              rc.right - rc.left, rc.bottom - rc.top,
                              nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
        return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    if (lpCmdLine && wcsstr(lpCmdLine, L"--autostart"))
        g_autoStartToTray = true;

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"PathHelper_InstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND hExisting = FindWindowW(szWindowClass, NULL);
        if (hExisting)
        {
            ShowWindow(hExisting, SW_SHOW);
            SetForegroundWindow(hExisting);
        }
        return 0;
    }

    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc = {sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS};
    InitCommonControlsEx(&icc);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PATHHELPER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    InitDataDirectory();
    LoadInjectionList();
    InitExplorerFavorites();
    InitExplorerMonitor();

    if (!InitInstance(hInstance, g_autoStartToTray ? SW_HIDE : nCmdShow))
        return FALSE;

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_PATHHELPER));

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    CleanupExplorerMonitor();
    CleanupExplorerFavorites();
    CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}
