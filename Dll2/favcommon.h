#pragma once

#include "framework.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Dll2（注入到 explorer.exe 的收藏面板 DLL）
//
// 只保留收藏相关的功能：收藏当前文件夹、收藏任意路径、管理（备注/排序/删除），
// 没有历史记录，也没有"已打开的文件夹"模块。收藏数据与 Dll1 共用
// %USERPROFILE%\.PathHelper\Favorites.jsonl。
// ---------------------------------------------------------------------------

struct FavoriteEntry
{
    std::wstring path;
    std::wstring note;
    std::wstring timestamp;
};

struct FavSettings
{
    std::wstring theme = L"light";
    int panelWidth = 260;
    int panelMargin = 5;
    int fontSize = 10;
    bool stripCommonPrefix = true;
};

struct FavThemeColors
{
    COLORREF bg;
    COLORREF headerBg;
    COLORREF itemText;
    COLORREF itemTextSecondary;
    COLORREF itemTitle;
    COLORREF itemSep;
    COLORREF selBg;
    COLORREF selText;
    COLORREF hoverBg;
    COLORREF sep;
    COLORREF border;
    COLORREF btnBg;
    COLORREF btnBorder;
    COLORREF btnText;
    COLORREF btnHoverBg;
    COLORREF accent;
};

FavThemeColors GetFavThemeColors(bool isDark);
bool IsFavDarkTheme();

std::wstring GetFavDataDir();
std::wstring GetFavIniPath();
std::wstring GetFavStorePath();

std::vector<FavoriteEntry> LoadFavorites();
void SaveFavorites(const std::vector<FavoriteEntry> &favorites);

void LoadFavSettings(FavSettings &settings);
bool ReadFavEnabledFromIni();
void WriteFavEnabledToIni(bool enabled);

// 字体与度量
void ComputeFavMetrics();
bool EnsureFavFonts();

extern HINSTANCE g_hFavInst;
extern FavSettings g_favSettings;

extern int g_favItemFontHeight;
extern int g_favItemFontHeightSecondary;
extern int g_favHeaderFontHeight;
extern int g_favItemHeight;
extern int g_favHeaderHeight;

extern HFONT g_hFavFont;
extern HFONT g_hFavFontBold;
extern HFONT g_hFavFontSecondary;
extern HFONT g_hFavHeaderFont;
// 专门用来画两个按钮图标（U+1F4C1 / U+1F4CD 这类字符）的字体。
// explorer 进程里 GDI 的字体链接不一定生效，直接用带这些字形的字体更稳。
extern HFONT g_hFavIconFont;

constexpr auto FAV_PANEL_CLASS = L"PathHelperFavoritesPanel";
constexpr UINT WM_FAV_REPOSITION = WM_APP + 300;
constexpr int FAV_CORNER_RADIUS = 12;
