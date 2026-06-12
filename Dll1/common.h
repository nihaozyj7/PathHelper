#pragma once

#include "framework.h"
#include <vector>
#include <map>
#include <string>
#include <algorithm>

#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include "nlohmann/json.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#define CDM_FIRST (WM_USER + 100)
#define CDM_GETSPEC (CDM_FIRST + 0x0000)
#define CDM_GETFILEPATH (CDM_FIRST + 0x0001)
#define CDM_GETFOLDERPATH (CDM_FIRST + 0x0002)
#define CDM_SETFOLDERPATH (CDM_FIRST + 0x0006)

// IFileDialog::Show is at vtable index 3 (per shobjidl.h; inherits IFileDialog -> IModalWindow -> IUnknown)
#define IFileDialog_Show_Index 3

inline std::wstring GetDataDir()
{
    wchar_t userProfile[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH);
    if (len == 0)
        return L"";
    return std::wstring(userProfile) + L"\\.PathHelper";
}

constexpr auto PANEL_CLASS = L"PathHelperCompanionPanel";
constexpr int PANEL_WIDTH = 260;
constexpr UINT WM_REPOSITION_PANEL = WM_APP + 100;
constexpr UINT WM_NAVIGATE_PATH = WM_APP + 101;
constexpr UINT WM_QUERY_FOLDER_PATH = WM_APP + 102;

struct DialogInfo
{
    HWND hwnd;
    WNDPROC originalProc;
    HWND hwndPanel;
    std::wstring cachedPath;
};

struct VTableHookInfo
{
    void **vtable;
    void *originalShow;
};

struct Settings
{
    bool autoToLatest;
    std::wstring theme;
    int historyPanelWidth;
    int panelMargin;
    int historyPanelFontSize;
    bool stripCommonPrefix;
    int historyDisplayMax;

    Settings()
        : autoToLatest(false),
          theme(L"light"),
          historyPanelWidth(200),
          panelMargin(5),
          historyPanelFontSize(10),
          stripCommonPrefix(true),
          historyDisplayMax(5)
    {
    }
};

struct HistoryEntry
{
    std::wstring path;
    std::wstring timestamp;
};

constexpr int MAX_HISTORY_ENTRIES = 200;

inline std::string WstrToUTF8(const std::wstring &wstr)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (len <= 0)
        return {};
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], len, NULL, NULL);
    return result;
}

inline std::wstring UTF8ToWstr(const std::string &str)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    if (len <= 0)
        return {};
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], len);
    return result;
}

extern HWINEVENTHOOK g_hEventHook;
extern std::map<HWND, DialogInfo> g_dialogs;
extern std::vector<VTableHookInfo> g_hookedVtables;
extern std::map<DWORD, IFileDialog *> g_threadFileDialogs;
extern HINSTANCE g_hInst;
extern HANDLE g_hHookThread;
extern DWORD g_dwHookThreadId;
extern HANDLE g_hExitEvent;
extern CRITICAL_SECTION g_cs;
extern Settings g_settings;
extern HFONT g_hItemFont;
extern HFONT g_hHeaderFont;
extern HFONT g_hItemFontBold;
extern HFONT g_hItemFontSecondary;
extern ID2D1Factory* g_pD2DFactory;
extern IDWriteFactory* g_pDWriteFactory;
extern IDWriteTextFormat* g_pItemTextFormat;
extern IDWriteTextFormat* g_pItemTextFormatBold;
extern IDWriteTextFormat* g_pItemTextFormatSecondary;
extern IDWriteTextFormat* g_pHeaderTextFormat;
extern std::vector<HistoryEntry> g_historyCache;
extern bool g_historyCacheValid;
extern HANDLE g_hExplorerPathsNotify;

std::wstring GetSelectedPath(HWND hwnd);

inline std::wstring NormalizePath(const std::wstring &path)
{
    std::wstring result = path;
    if (!result.empty() && result.back() == L'\\')
        result.pop_back();
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

constexpr int CORNER_RADIUS = 14;
constexpr int LIST_PADDING = 6;
constexpr int HISTORY_DISPLAY_CAP = 5;
constexpr int EXPLORER_MAX = 5;
constexpr int EXPLORER_DISPLAY_HEIGHT_ITEMS = 3;
constexpr UINT WM_REFRESH_EXPLORER_PATHS = WM_APP + 103;
constexpr UINT WM_REFRESH_FAV_STATE = WM_APP + 104;
constexpr int SEL_CORNER_RADIUS = 8;

extern int g_itemFontPixelHeight;
extern int g_itemFontPixelHeightSecondary;
extern int g_headerFontPixelHeight;
extern int g_itemHeight;
extern int g_headerHeight;
extern int g_favHeaderHeight;
extern int g_explorerHeaderHeight;
extern int g_historyListHeight;
extern int g_explorerListHeight;

void ComputeLayoutMetrics();
void DWriteDrawText(HDC hdc, const RECT& rc, const wchar_t* text,
                    IDWriteTextFormat* format, HFONT fallbackFont,
                    COLORREF color, UINT dtFlags);


