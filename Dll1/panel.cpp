#include "pch.h"
#include "panel.h"
#include "history.h"
#include "settings.h"
#include <uxtheme.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d2d1_1.h>
#include <unordered_map>
#pragma comment(lib, "uxtheme.lib")

// 文本布局缓存代次：ComputeLayoutMetrics() 重建字体/格式时自增，
// 让缓存中的 IDWriteTextLayout 失效。
int g_textLayoutGeneration = 0;

int g_itemFontPixelHeight = 13;
int g_itemFontPixelHeightSecondary = 10;
int g_headerFontPixelHeight = 14;
int g_itemHeight = 46;
int g_headerHeight = 38;
int g_favHeaderHeight = 38;
int g_explorerHeaderHeight = 38;
int g_historyListHeight = 46 * HISTORY_DISPLAY_CAP + 6;
int g_explorerListHeight = 46 * EXPLORER_DISPLAY_HEIGHT_ITEMS + 6;

void ComputeLayoutMetrics()
{
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    g_itemFontPixelHeight = MulDiv(g_settings.historyPanelFontSize, dpi, 72);
    if (g_itemFontPixelHeight < 6)
        g_itemFontPixelHeight = 6;
    g_itemFontPixelHeightSecondary = (int)(g_itemFontPixelHeight * 0.9f + 0.5f);
    if (g_itemFontPixelHeightSecondary < 5)
        g_itemFontPixelHeightSecondary = 5;
    g_headerFontPixelHeight = g_itemFontPixelHeight + g_itemFontPixelHeight * 8 / 100;
    if (g_headerFontPixelHeight < g_itemFontPixelHeight + 1)
        g_headerFontPixelHeight = g_itemFontPixelHeight + 1;

    g_itemHeight = g_itemFontPixelHeight + g_itemFontPixelHeightSecondary + 10;
    g_headerHeight = MulDiv(g_headerFontPixelHeight, 272, 100);
    if (g_headerHeight < g_headerFontPixelHeight + 10)
        g_headerHeight = g_headerFontPixelHeight + 10;
    g_favHeaderHeight = g_headerHeight;
    g_explorerHeaderHeight = g_headerHeight;
    g_historyListHeight = g_itemHeight * HISTORY_DISPLAY_CAP + 6;
    g_explorerListHeight = g_itemHeight * EXPLORER_DISPLAY_HEIGHT_ITEMS + 6;

    if (g_hItemFont)
    {
        DeleteObject(g_hItemFont);
        g_hItemFont = nullptr;
    }
    if (g_hHeaderFont)
    {
        DeleteObject(g_hHeaderFont);
        g_hHeaderFont = nullptr;
    }
    if (g_hItemFontBold)
    {
        DeleteObject(g_hItemFontBold);
        g_hItemFontBold = nullptr;
    }
    if (g_hItemFontSecondary)
    {
        DeleteObject(g_hItemFontSecondary);
        g_hItemFontSecondary = nullptr;
    }

    if (g_pDWriteFactory)
    {
        if (g_pItemTextFormat) { g_pItemTextFormat->Release(); g_pItemTextFormat = nullptr; }
        if (g_pItemTextFormatBold) { g_pItemTextFormatBold->Release(); g_pItemTextFormatBold = nullptr; }
        if (g_pItemTextFormatSecondary) { g_pItemTextFormatSecondary->Release(); g_pItemTextFormatSecondary = nullptr; }
        if (g_pHeaderTextFormat) { g_pHeaderTextFormat->Release(); g_pHeaderTextFormat = nullptr; }

        HDC hdc = GetDC(NULL);
        int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(NULL, hdc);

        FLOAT itemSize = g_itemFontPixelHeight * 96.0f / dpi;
        FLOAT itemSizeBold = g_itemFontPixelHeight * 96.0f / dpi;
        FLOAT itemSizeSecondary = g_itemFontPixelHeightSecondary * 96.0f / dpi;
        FLOAT headerSize = g_headerFontPixelHeight * 96.0f / dpi;

        g_pDWriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            itemSize, L"zh-CN", &g_pItemTextFormat);
        g_pDWriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            itemSizeBold, L"zh-CN", &g_pItemTextFormatBold);
        g_pDWriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            itemSizeSecondary, L"zh-CN", &g_pItemTextFormatSecondary);
        g_pDWriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            headerSize, L"zh-CN", &g_pHeaderTextFormat);
    }

    // 字体/格式已重建，让文本布局缓存失效
    ++g_textLayoutGeneration;
}

// ── Modern color palette ──

struct ThemeColors
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
    COLORREF editBg;
    COLORREF editBorder;
    COLORREF accent;
};

static ThemeColors GetThemeColors(bool isDark)
{
    if (isDark)
    {
        return ThemeColors{
            RGB(30, 30, 30),    // bg
            RGB(40, 40, 44),    // headerBg
            RGB(220, 220, 228), // itemText
            RGB(140, 140, 155), // itemTextSecondary
            RGB(150, 195, 250), // itemTitle
            RGB(48, 48, 56),    // itemSep
            RGB(9, 71, 113),    // selBg
            RGB(140, 200, 250), // selText
            RGB(45, 48, 55),    // hoverBg
            RGB(56, 56, 62),    // sep
            RGB(56, 56, 62),    // border
            RGB(50, 50, 56),    // btnBg
            RGB(72, 72, 78),    // btnBorder
            RGB(180, 180, 195), // btnText
            RGB(60, 60, 68),    // btnHoverBg
            RGB(42, 42, 48),    // editBg
            RGB(62, 62, 68),    // editBorder
            RGB(90, 155, 220),  // accent
        };
    }
    else
    {
        return ThemeColors{
            RGB(255, 255, 255), // bg
            RGB(248, 248, 252), // headerBg
            RGB(30, 30, 36),    // itemText
            RGB(120, 120, 135), // itemTextSecondary
            RGB(10, 52, 120),   // itemTitle
            RGB(232, 232, 240), // itemSep
            RGB(220, 235, 252), // selBg
            RGB(0, 80, 180),    // selText
            RGB(242, 242, 248), // hoverBg
            RGB(226, 226, 232), // sep
            RGB(218, 218, 226), // border
            RGB(245, 245, 248), // btnBg
            RGB(200, 200, 210), // btnBorder
            RGB(80, 80, 95),    // btnText
            RGB(232, 232, 240), // btnHoverBg
            RGB(255, 255, 255), // editBg
            RGB(210, 210, 218), // editBorder
            RGB(0, 90, 180),    // accent
        };
    }
}

static bool IsDarkCached()
{
    static std::wstring cachedTheme;
    static bool cachedResult = false;
    if (cachedTheme != g_settings.theme)
    {
        cachedResult = IsDarkTheme();
        cachedTheme = g_settings.theme;
    }
    return cachedResult;
}

// ── DirectWrite text rendering helper ──

static void FallbackDrawText(HDC hdc, const RECT& rc, const wchar_t* text,
                              HFONT font, COLORREF color, UINT dtFlags)
{
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    HFONT hOldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;
    RECT rcCopy = rc;
    DrawTextW(hdc, text, -1, &rcCopy, dtFlags);
    if (hOldFont) SelectObject(hdc, hOldFont);
}

// 面板绘制全部发生在 HookThread 上，因此这里的缓存无需加锁。
//
// 旧实现每绘制一行文字都会重新创建 ID2D1DCRenderTarget / IDWriteTextLayout /
// ID2D1SolidColorBrush，一个列表每帧可能创建几十个 D2D 资源，这是滚动卡顿的主因。
// 现在复用同一个 DC 渲染目标，并按 (格式, 尺寸, 文本) 缓存文本布局。
static ID2D1DCRenderTarget *g_pTextRT = nullptr;
static ID2D1DeviceContext *g_pTextDevCtx = nullptr;
static ID2D1SolidColorBrush *g_pTextBrush = nullptr;
static std::unordered_map<std::wstring, IDWriteTextLayout *> g_layoutCache;

static void ClearLayoutCache()
{
    for (auto &kv : g_layoutCache)
    {
        if (kv.second)
            kv.second->Release();
    }
    g_layoutCache.clear();
}

void ReleaseDWriteCache()
{
    ClearLayoutCache();
    if (g_pTextBrush) { g_pTextBrush->Release(); g_pTextBrush = nullptr; }
    if (g_pTextDevCtx) { g_pTextDevCtx->Release(); g_pTextDevCtx = nullptr; }
    if (g_pTextRT) { g_pTextRT->Release(); g_pTextRT = nullptr; }
}

// 只有需要彩色字体（emoji 等）的文本才走较慢的 D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT 路径
static bool TextNeedsColorFont(const wchar_t *text)
{
    for (const wchar_t *p = text; p && *p; ++p)
    {
        wchar_t c = *p;
        if ((c >= 0xD800 && c <= 0xDFFF) || (c >= 0x2190 && c <= 0x2BFF) || c == 0xFE0F)
            return true;
    }
    return false;
}

static IDWriteTextLayout *GetCachedTextLayout(const wchar_t *text, UINT32 textLen,
                                              IDWriteTextFormat *format,
                                              FLOAT widthDIP, FLOAT heightDIP, UINT dtFlags)
{
    wchar_t meta[96];
    swprintf_s(meta, L"%d|%p|%d|%d|%u|", g_textLayoutGeneration, (void *)format,
               (int)(widthDIP * 16.0f + 0.5f), (int)(heightDIP * 16.0f + 0.5f), dtFlags);

    std::wstring key(meta);
    key.append(text, textLen);

    auto it = g_layoutCache.find(key);
    if (it != g_layoutCache.end())
        return it->second;

    if (g_layoutCache.size() >= 512)
        ClearLayoutCache();

    IDWriteTextLayout *pLayout = nullptr;
    if (FAILED(g_pDWriteFactory->CreateTextLayout(text, textLen, format, widthDIP, heightDIP, &pLayout)) || !pLayout)
        return nullptr;

    pLayout->SetTextAlignment((dtFlags & DT_CENTER) ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    pLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (dtFlags & DT_SINGLELINE)
        pLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    g_layoutCache.emplace(std::move(key), pLayout);
    return pLayout;
}

void DWriteDrawText(HDC hdc, const RECT& rc, const wchar_t* text,
                    IDWriteTextFormat* format, HFONT fallbackFont,
                    COLORREF color, UINT dtFlags)
{
    if (!hdc || !text || !format || !g_pD2DFactory || !g_pDWriteFactory)
    {
        FallbackDrawText(hdc, rc, text, fallbackFont, color, dtFlags);
        return;
    }

    if (!g_pTextRT)
    {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            0, 0,
            D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
        if (FAILED(g_pD2DFactory->CreateDCRenderTarget(&props, &g_pTextRT)))
            g_pTextRT = nullptr;
        else
            g_pTextRT->QueryInterface(IID_PPV_ARGS(&g_pTextDevCtx));
    }

    if (!g_pTextRT || FAILED(g_pTextRT->BindDC(hdc, &rc)))
    {
        FallbackDrawText(hdc, rc, text, fallbackFont, color, dtFlags);
        return;
    }

    FLOAT dpiX = 96.0f, dpiY = 96.0f;
    g_pTextRT->GetDpi(&dpiX, &dpiY);
    if (dpiX <= 0.0f) dpiX = 96.0f;
    if (dpiY <= 0.0f) dpiY = 96.0f;

    FLOAT widthDIP = (rc.right - rc.left) * 96.0f / dpiX;
    FLOAT heightDIP = (rc.bottom - rc.top) * 96.0f / dpiY;

    IDWriteTextLayout *pLayout = GetCachedTextLayout(text, (UINT32)wcslen(text), format, widthDIP, heightDIP, dtFlags);
    if (!pLayout)
    {
        FallbackDrawText(hdc, rc, text, fallbackFont, color, dtFlags);
        return;
    }

    bool drew = false;
    g_pTextRT->BeginDraw();
    if (!g_pTextBrush)
        g_pTextRT->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &g_pTextBrush);
    if (g_pTextBrush)
    {
        g_pTextBrush->SetColor(D2D1::ColorF(GetRValue(color) / 255.0f,
                                            GetGValue(color) / 255.0f,
                                            GetBValue(color) / 255.0f));
        if (g_pTextDevCtx && TextNeedsColorFont(text))
            g_pTextDevCtx->DrawTextLayout(D2D1::Point2F(0, 0), pLayout, g_pTextBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        else
            g_pTextRT->DrawTextLayout(D2D1::Point2F(0, 0), pLayout, g_pTextBrush);
        drew = true;
    }
    HRESULT hrEnd = g_pTextRT->EndDraw();
    if (hrEnd == D2DERR_RECREATE_TARGET)
        ReleaseDWriteCache();
    else if (!drew)
        FallbackDrawText(hdc, rc, text, fallbackFont, color, dtFlags);
}

// ── 双缓冲后台位图缓存 ──
// 列表每次重绘都新建/销毁兼容位图会造成明显的 GDI 抖动，这里按窗口 + 尺寸缓存。
struct WindowBackBuffer
{
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HBITMAP oldBmp = nullptr;
    int width = 0;
    int height = 0;
};

static WindowBackBuffer *GetWindowBackBuffer(HWND hwnd, HDC refDC, int width, int height)
{
    if (width <= 0 || height <= 0)
        return nullptr;

    auto *bb = (WindowBackBuffer *)GetPropW(hwnd, L"BackBuffer");
    if (!bb)
    {
        bb = new WindowBackBuffer();
        SetPropW(hwnd, L"BackBuffer", (HANDLE)bb);
    }

    if (bb->dc && bb->width == width && bb->height == height)
        return bb;

    if (bb->bmp)
    {
        if (bb->oldBmp)
            SelectObject(bb->dc, bb->oldBmp);
        DeleteObject(bb->bmp);
        bb->bmp = nullptr;
        bb->oldBmp = nullptr;
    }
    if (!bb->dc)
        bb->dc = CreateCompatibleDC(refDC);
    if (!bb->dc)
        return nullptr;

    bb->bmp = CreateCompatibleBitmap(refDC, width, height);
    if (!bb->bmp)
        return nullptr;

    bb->oldBmp = (HBITMAP)SelectObject(bb->dc, bb->bmp);
    bb->width = width;
    bb->height = height;
    return bb;
}

static void DestroyWindowBackBuffer(HWND hwnd)
{
    auto *bb = (WindowBackBuffer *)GetPropW(hwnd, L"BackBuffer");
    if (!bb)
        return;

    if (bb->bmp)
    {
        if (bb->oldBmp)
            SelectObject(bb->dc, bb->oldBmp);
        DeleteObject(bb->bmp);
    }
    if (bb->dc)
        DeleteDC(bb->dc);
    delete bb;
    RemovePropW(hwnd, L"BackBuffer");
}

// ── Custom self-drawn list control ──

constexpr auto CLS_CUSTOM_LIST = L"PathHelperListClass";

static int GetListVisibleWidth(HWND hwnd);

static bool PromptBookmarkNote(HWND hwndParent, const std::wstring &path, const std::wstring &initialNote, std::wstring &outNote);
static void UpdateFavBtnMode(HWND hwnd, bool force = false);
static void BuildBookmarkListData(HWND hwndPanel, int preserveScrollTo = -1);

struct CustomListItem
{
    std::wstring line1;
    std::wstring line2;
    std::wstring navPath;
};

struct CustomListData
{
    std::vector<CustomListItem> items;
    int hoverIdx = -1;
    int selectedIdx = -1;
    int scrollOffset = 0;
    bool isFav = false;
    int dragSrcIdx = -1;
    int dropTargetIdx = -1;
    bool dragging = false;
    POINT dragStartPt = {};
};

static CustomListData *GetListData(HWND hwnd)
{
    return (CustomListData *)GetPropW(hwnd, L"ListData");
}

static int GetListVisibleHeight(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    return rc.bottom - rc.top;
}

static int GetListMaxScroll(HWND hwnd)
{
    CustomListData *data = GetListData(hwnd);
    if (!data)
        return 0;
    int totalHeight = (int)data->items.size() * g_itemHeight;
    int visibleHeight = GetListVisibleHeight(hwnd);
    int maxScroll = totalHeight - visibleHeight;
    return maxScroll > 0 ? maxScroll : 0;
}

static void ClampScrollOffset(HWND hwnd)
{
    CustomListData *data = GetListData(hwnd);
    if (!data)
        return;
    int maxScroll = GetListMaxScroll(hwnd);
    if (data->scrollOffset < 0)
        data->scrollOffset = 0;
    if (data->scrollOffset > maxScroll)
        data->scrollOffset = maxScroll;
}

// 只重绘发生变化的那一行，避免整列表重绘
static void InvalidateListItem(HWND hwnd, int idx)
{
    if (idx < 0)
        return;
    CustomListData *data = GetListData(hwnd);
    if (!data || idx >= (int)data->items.size())
        return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int y = idx * g_itemHeight - data->scrollOffset;
    RECT itemRc = {rc.left, y, rc.right, y + g_itemHeight};
    InvalidateRect(hwnd, &itemRc, FALSE);
}

static void DrawListScrollIndicator(HWND hwnd, HDC hdc, const RECT &rc)
{
    CustomListData *data = GetListData(hwnd);
    if (!data || data->items.empty())
        return;
    int totalHeight = (int)data->items.size() * g_itemHeight;
    int visibleHeight = rc.bottom - rc.top;
    if (totalHeight <= visibleHeight)
        return;
    int maxScroll = totalHeight - visibleHeight;
    if (maxScroll <= 0 || data->scrollOffset <= 0 && maxScroll == 0)
        return;

    bool isDark = IsDarkCached();
    auto colors = GetThemeColors(isDark);

    int thumbHeight = (std::max)(visibleHeight * visibleHeight / totalHeight, 20);
    int thumbTop = rc.top + (int)((LONG64)data->scrollOffset * (visibleHeight - thumbHeight) / maxScroll);
    int thumbWidth = 4;
    int thumbX = rc.right - thumbWidth - 1;

    SetDCBrushColor(hdc, colors.sep);
    RECT thumbRc = {thumbX, thumbTop, thumbX + thumbWidth, thumbTop + thumbHeight};
    HRGN hRgn = CreateRoundRectRgn(thumbRc.left, thumbRc.top, thumbRc.right, thumbRc.bottom, thumbWidth, thumbWidth);
    FillRgn(hdc, hRgn, (HBRUSH)GetStockObject(DC_BRUSH));
    DeleteObject(hRgn);
}

static LRESULT CALLBACK CustomListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        CustomListData *data = GetListData(hwnd);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;

        bool isDark = IsDarkCached();
        auto colors = GetThemeColors(isDark);

        WindowBackBuffer *backBuffer = GetWindowBackBuffer(hwnd, hdc, cx, cy);
        if (!backBuffer)
        {
            EndPaint(hwnd, &ps);
            return 0;
        }
        HDC memDC = backBuffer->dc;

        SetDCBrushColor(memDC, colors.bg);
        FillRect(memDC, &rc, (HBRUSH)GetStockObject(DC_BRUSH));

        if (!data)
        {
            BitBlt(hdc, 0, 0, cx, cy, memDC, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }

        int scrollOff = data->scrollOffset;
        int firstVisible = scrollOff / g_itemHeight;
        int visibleHeight = rc.bottom - rc.top;
        int lastVisible = (scrollOff + visibleHeight) / g_itemHeight + 1;
        if (lastVisible >= (int)data->items.size())
            lastVisible = (int)data->items.size() - 1;

        SaveDC(memDC);
        IntersectClipRect(memDC, rc.left, rc.top, rc.right, rc.bottom);

        for (int i = firstVisible; i <= lastVisible && i < (int)data->items.size(); ++i)
        {
            int y = i * g_itemHeight - scrollOff;
            RECT itemRc = {rc.left, y, rc.right, y + g_itemHeight};

            if (itemRc.bottom <= rc.top || itemRc.top >= rc.bottom)
                continue;

            bool selected = (i == data->selectedIdx);
            bool hovered = (i == data->hoverIdx);
            bool isDragSrc = (data->isFav && data->dragging && i == data->dragSrcIdx);

            SetDCBrushColor(memDC, colors.bg);
            FillRect(memDC, &itemRc, (HBRUSH)GetStockObject(DC_BRUSH));

            if (selected)
            {
                RECT rcSel = {itemRc.left + 4, itemRc.top + 2, itemRc.right - 4, itemRc.bottom - 2};
                HRGN hRgn = CreateRoundRectRgn(rcSel.left, rcSel.top, rcSel.right, rcSel.bottom,
                                               SEL_CORNER_RADIUS, SEL_CORNER_RADIUS);
                SetDCBrushColor(memDC, isDragSrc ? colors.hoverBg : colors.selBg);
                FillRgn(memDC, hRgn, (HBRUSH)GetStockObject(DC_BRUSH));
                DeleteObject(hRgn);
            }
            else if (hovered)
            {
                RECT rcHover = {itemRc.left + 4, itemRc.top + 2, itemRc.right - 4, itemRc.bottom - 2};
                HRGN hRgn = CreateRoundRectRgn(rcHover.left, rcHover.top, rcHover.right, rcHover.bottom,
                                               SEL_CORNER_RADIUS, SEL_CORNER_RADIUS);
                SetDCBrushColor(memDC, colors.hoverBg);
                FillRgn(memDC, hRgn, (HBRUSH)GetStockObject(DC_BRUSH));
                DeleteObject(hRgn);
            }

            const CustomListItem &item = data->items[i];
            int textLeft = itemRc.left + 12;
            int textRight = itemRc.right - 12;
            int lineVPad = 3;
            int line1Top = itemRc.top + lineVPad;
            int line1Bottom = line1Top + g_itemFontPixelHeight + 2;
            int line2Top = line1Bottom;
            int line2Bottom = itemRc.bottom - lineVPad;

            RECT rcLine1 = {textLeft, line1Top, textRight, line1Bottom};
            RECT rcLine2 = {textLeft, line2Top, textRight, line2Bottom};

            COLORREF line1Color = selected ? colors.selText : colors.itemTitle;
            COLORREF line2Color = selected ? colors.selText : colors.itemTextSecondary;

            DWriteDrawText(memDC, rcLine1, item.line1.c_str(), g_pItemTextFormatBold, g_hItemFontBold,
                          line1Color, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            DWriteDrawText(memDC, rcLine2, item.line2.c_str(), g_pItemTextFormatSecondary, g_hItemFontSecondary,
                          line2Color, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            if (i < (int)data->items.size() - 1)
            {
                HPEN hSepPen = CreatePen(PS_SOLID, 1, colors.itemSep);
                HPEN hOldPen = (HPEN)SelectObject(memDC, hSepPen);
                MoveToEx(memDC, itemRc.left + 8, itemRc.bottom - 1, NULL);
                LineTo(memDC, itemRc.right - 8, itemRc.bottom - 1);
                SelectObject(memDC, hOldPen);
                DeleteObject(hSepPen);
            }
        }

        RestoreDC(memDC, -1);

        if (data->isFav && data->dragging && data->dropTargetIdx >= 0 && data->dropTargetIdx <= (int)data->items.size())
        {
            int lineY = data->dropTargetIdx * g_itemHeight - scrollOff;
            if (lineY > rc.top - 2 && lineY < rc.bottom + 2)
            {
                int lineLeft = rc.left + 4;
                int lineRight = rc.right - 4;
                HPEN hLinePen = CreatePen(PS_SOLID, 3, colors.accent);
                HPEN hOldLinePen = (HPEN)SelectObject(memDC, hLinePen);
                MoveToEx(memDC, lineLeft, lineY, NULL);
                LineTo(memDC, lineRight, lineY);
                SelectObject(memDC, hOldLinePen);
                DeleteObject(hLinePen);

                POINT tri[3] = {
                    {lineLeft, lineY - 5},
                    {lineLeft, lineY + 5},
                    {lineLeft + 7, lineY}
                };
                SetDCBrushColor(memDC, colors.accent);
                HRGN triRgn = CreatePolygonRgn(tri, 3, WINDING);
                FillRgn(memDC, triRgn, (HBRUSH)GetStockObject(DC_BRUSH));
                DeleteObject(triRgn);
            }
        }

        DrawListScrollIndicator(hwnd, memDC, rc);

        BitBlt(hdc, 0, 0, cx, cy, memDC, 0, 0, SRCCOPY);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        CustomListData *data = GetListData(hwnd);
        if (!data)
            break;

        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        if (data->isFav && data->dragSrcIdx >= 0)
        {
            int dx = pt.x - data->dragStartPt.x;
            int dy = pt.y - data->dragStartPt.y;
            if (!data->dragging && (dx * dx + dy * dy) > 25)
            {
                data->dragging = true;
                data->hoverIdx = -1;
            }
            if (data->dragging && !data->items.empty())
            {
                int visH = GetListVisibleHeight(hwnd);
                int scrollZone = g_itemHeight;
                if (pt.y >= 0 && pt.y < scrollZone && data->scrollOffset > 0)
                {
                    data->scrollOffset -= g_itemHeight;
                    ClampScrollOffset(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                else if (pt.y >= visH - scrollZone && pt.y < visH)
                {
                    int maxScroll = GetListMaxScroll(hwnd);
                    if (data->scrollOffset < maxScroll)
                    {
                        data->scrollOffset += g_itemHeight;
                        ClampScrollOffset(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }

                int y = pt.y + data->scrollOffset;
                int newDrop;
                if (pt.y < 0)
                    newDrop = 0;
                else if (pt.y >= visH)
                    newDrop = (int)data->items.size();
                else
                {
                    int itemIdx = y / g_itemHeight;
                    int itemY = y % g_itemHeight;
                    newDrop = (itemY >= g_itemHeight / 2) ? itemIdx + 1 : itemIdx;
                }
                if (newDrop < 0) newDrop = 0;
                if (newDrop > (int)data->items.size()) newDrop = (int)data->items.size();

                if (newDrop != data->dropTargetIdx)
                {
                    data->dropTargetIdx = newDrop;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            TRACKMOUSEEVENT tme = {sizeof(tme)};
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            break;
        }

        int y = pt.y + data->scrollOffset;
        int idx = y / g_itemHeight;
        if (idx < 0 || idx >= (int)data->items.size())
            idx = -1;

        if (pt.x < 0 || pt.x >= GetListVisibleWidth(hwnd) || pt.y < 0 || pt.y >= GetListVisibleHeight(hwnd))
            idx = -1;

        if (idx != data->hoverIdx)
        {
            int oldIdx = data->hoverIdx;
            data->hoverIdx = idx;
            InvalidateListItem(hwnd, oldIdx);
            InvalidateListItem(hwnd, idx);
        }

        TRACKMOUSEEVENT tme = {sizeof(tme)};
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        break;
    }

    case WM_MOUSELEAVE:
    {
        CustomListData *data = GetListData(hwnd);
        if (data && data->hoverIdx != -1)
        {
            int oldIdx = data->hoverIdx;
            data->hoverIdx = -1;
            InvalidateListItem(hwnd, oldIdx);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        CustomListData *data = GetListData(hwnd);
        if (!data)
            break;

        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int y = pt.y + data->scrollOffset;
        int idx = y / g_itemHeight;
        if (idx >= 0 && idx < (int)data->items.size())
        {
            int oldSel = data->selectedIdx;
            data->selectedIdx = idx;
            InvalidateListItem(hwnd, oldSel);
            InvalidateListItem(hwnd, idx);
            if (data->isFav)
            {
                data->dragSrcIdx = idx;
                data->dragging = false;
                data->dropTargetIdx = -1;
                data->dragStartPt = pt;
                SetCapture(hwnd);
            }
            else
            {
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKELONG(GetDlgCtrlID(hwnd), LBN_SELCHANGE), (LPARAM)hwnd);
            }
        }
        break;
    }

    case WM_LBUTTONDBLCLK:
    {
        CustomListData *data = GetListData(hwnd);
        if (!data)
            break;

        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int y = pt.y + data->scrollOffset;
        int idx = y / g_itemHeight;
        if (idx >= 0 && idx < (int)data->items.size())
        {
            int oldSel = data->selectedIdx;
            data->selectedIdx = idx;
            InvalidateListItem(hwnd, oldSel);
            InvalidateListItem(hwnd, idx);
            if (!data->isFav)
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKELONG(GetDlgCtrlID(hwnd), LBN_SELCHANGE), (LPARAM)hwnd);
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        CustomListData *data = GetListData(hwnd);
        if (!data || !data->isFav)
            break;

        if (data->dragSrcIdx >= 0)
        {
            int src = data->dragSrcIdx;
            int drop = data->dropTargetIdx;
            bool wasDragging = data->dragging;

            data->dragSrcIdx = -1;
            data->dragging = false;
            data->dropTargetIdx = -1;

            if (wasDragging && drop >= 0)
            {
                HWND hwndPanel = GetParent(hwnd);
                auto *bookmarks = (std::vector<BookmarkEntry> *)GetPropW(hwndPanel, L"BookmarkData");
                if (bookmarks && src >= 0 && src < (int)bookmarks->size() && drop >= 0 && drop <= (int)bookmarks->size())
                {
                    if (src != drop && src != drop - 1)
                    {
                        int effectiveDrop = drop;
                        BookmarkEntry entry = std::move((*bookmarks)[src]);
                        bookmarks->erase(bookmarks->begin() + src);
                        if (effectiveDrop > src)
                            effectiveDrop--;
                        bookmarks->insert(bookmarks->begin() + effectiveDrop, std::move(entry));
                        SaveBookmarks(*bookmarks);
                        BuildBookmarkListData(hwndPanel, effectiveDrop);
                        UpdateFavBtnMode(hwndPanel, true);
                    }
                }
            }
            else
            {
                if (data->selectedIdx >= 0 && data->selectedIdx < (int)data->items.size())
                    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKELONG(GetDlgCtrlID(hwnd), LBN_SELCHANGE), (LPARAM)hwnd);
            }

            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }

    case WM_CAPTURECHANGED:
    {
        CustomListData *data = GetListData(hwnd);
        if (data && data->dragSrcIdx >= 0)
        {
            data->dragSrcIdx = -1;
            data->dragging = false;
            data->dropTargetIdx = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }

    case WM_RBUTTONUP:
    {
        CustomListData *data = GetListData(hwnd);
        if (!data || !data->isFav)
            break;

        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int y = pt.y + data->scrollOffset;
        int idx = y / g_itemHeight;
        if (idx < 0 || idx >= (int)data->items.size())
            break;

        POINT screenPt = pt;
        ClientToScreen(hwnd, &screenPt);

        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 1001, L"\u4FEE\u6539\u5907\u6CE8");
        AppendMenuW(hMenu, MF_STRING, 1002, L"\u5220\u9664");
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, screenPt.x, screenPt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);

        HWND hwndPanel = GetParent(hwnd);
        auto *bookmarks = (std::vector<BookmarkEntry> *)GetPropW(hwndPanel, L"BookmarkData");
        if (!bookmarks || idx >= (int)bookmarks->size())
            break;

        if (cmd == 1001)
        {
            std::wstring newNote;
            if (PromptBookmarkNote(hwndPanel, (*bookmarks)[idx].path, (*bookmarks)[idx].note, newNote))
            {
                (*bookmarks)[idx].note = newNote;
                SaveBookmarks(*bookmarks);
                BuildBookmarkListData(hwndPanel);
            }
        }
        else if (cmd == 1002)
        {
            bookmarks->erase(bookmarks->begin() + idx);
            SaveBookmarks(*bookmarks);
            BuildBookmarkListData(hwndPanel);
            UpdateFavBtnMode(hwndPanel, true);
        }
        break;
    }

    case WM_MOUSEWHEEL:
    {
        CustomListData *data = GetListData(hwnd);
        if (!data)
            break;

        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int scrollAmount = delta > 0 ? -g_itemHeight : g_itemHeight;
        if (abs(delta) > 1)
            scrollAmount = -delta * g_itemHeight / WHEEL_DELTA;

        data->scrollOffset += scrollAmount;
        ClampScrollOffset(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return TRUE;
    }

    case WM_DESTROY:
    {
        CustomListData *data = GetListData(hwnd);
        if (data)
        {
            delete data;
            RemovePropW(hwnd, L"ListData");
        }
        DestroyWindowBackBuffer(hwnd);
        break;
    }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int GetListVisibleWidth(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    return rc.right - rc.left;
}

void RegisterCustomListClass()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CustomListProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLS_CUSTOM_LIST;
    RegisterClassExW(&wc);
}

// ── Utility ──

static RECT GetWindowVisibleRect(HWND hwnd)
{
    RECT rc;
    if (!GetWindowRect(hwnd, &rc))
        return {};

    HMODULE hDwm = GetModuleHandleW(L"dwmapi.dll");
    if (hDwm)
    {
        typedef HRESULT(WINAPI * P_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
        P_DwmGetWindowAttribute pDwmGetWindowAttribute = (P_DwmGetWindowAttribute)GetProcAddress(hDwm, "DwmGetWindowAttribute");
        if (pDwmGetWindowAttribute)
        {
            RECT extended = {};
            const DWORD DWMWA_EXTENDED_FRAME_BOUNDS = 9;
            if (SUCCEEDED(pDwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &extended, sizeof(extended))))
                rc = extended;
        }
    }
    return rc;
}

void PositionPanel(HWND hwndDialog, HWND hwndPanel)
{
    RECT rcDialog = GetWindowVisibleRect(hwndDialog);
    if (rcDialog.left == 0 && rcDialog.right == 0)
        return;

    int dialogHeight = rcDialog.bottom - rcDialog.top;
    if (dialogHeight < 200)
        dialogHeight = 200;

    HMONITOR hMon = MonitorFromWindow(hwndDialog, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(hMon, &mi);

    int panelWidth = g_settings.historyPanelWidth;
    int panelMargin = g_settings.panelMargin;
    int panelX;
    int panelY = rcDialog.top;

    panelX = rcDialog.right + panelMargin;
    if (panelX + panelWidth > mi.rcWork.right)
    {
        panelX = rcDialog.left - panelWidth - panelMargin;
        if (panelX < mi.rcWork.left)
            panelX = mi.rcWork.right - panelWidth;
    }

    if (panelY < mi.rcWork.top)
        panelY = mi.rcWork.top;
    if (panelY + dialogHeight > mi.rcWork.bottom)
        dialogHeight = mi.rcWork.bottom - panelY;

    SetWindowPos(hwndPanel, NULL, panelX, panelY, panelWidth, dialogHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void SetPanelRegion(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    HRGN hRgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, CORNER_RADIUS, CORNER_RADIUS);
    SetWindowRgn(hwnd, hRgn, TRUE);
}

static RECT GetFavBookmarkBtnRect(HWND hwnd, int favHeaderTop)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int btnWidth = 40;
    const int btnHeight = 32;
    int vCenter = favHeaderTop + g_favHeaderHeight / 2;
    return {rc.right - btnWidth - 10, vCenter - btnHeight / 2, rc.right - 10, vCenter + btnHeight / 2};
}

static RECT GetHistBtnRect(HWND hwnd, int headerTop, int btnIndex)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int btnWidth = 36;
    const int btnHeight = 30;
    const int btnGap = 6;
    const int rightPad = 8;
    int vCenter = headerTop + g_headerHeight / 2;
    int rightEdge = rc.right - rightPad - btnIndex * (btnWidth + btnGap);
    return {rightEdge - btnWidth, vCenter - btnHeight / 2, rightEdge, vCenter + btnHeight / 2};
}

static int GetExplorerHeaderTop()
{
    return g_headerHeight + g_historyListHeight;
}

static int GetFavHeaderTop()
{
    return g_headerHeight + g_historyListHeight + g_explorerHeaderHeight + g_explorerListHeight;
}

static void UpdateFavBtnMode(HWND hwnd, bool force)
{
    // 这里会向对话框线程发同步查询。若对话框正卡在慢速网络路径上，面板线程会被拖住，
    // 表现为列表滚动卡顿。因此做节流 + 短超时，且超时后绝不在面板线程上做昂贵的回退查询。
    static DWORD lastQueryTick = 0;
    DWORD now = GetTickCount();
    if (!force && (DWORD)(now - lastQueryTick) < 200)
        return;
    lastQueryTick = now;

    bool wasFav = (GetPropW(hwnd, L"FavBtnIsFav") != nullptr);
    bool isFav = false;

    HWND hwndDialog = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (hwndDialog && IsWindow(hwndDialog))
    {
        auto *bookmarks = (std::vector<BookmarkEntry> *)GetPropW(hwnd, L"BookmarkData");
        if (bookmarks)
        {
            DWORD_PTR ignored = 0;
            if (SendMessageTimeoutW(hwndDialog, WM_QUERY_FOLDER_PATH, 0, 0,
                                    SMTO_ABORTIFHUNG, 120, &ignored))
            {
                std::wstring path;
                if (TakeCachedDialogPath(hwndDialog, path) && !path.empty())
                {
                    std::wstring normPath = NormalizePath(path);
                    for (const auto &bm : *bookmarks)
                    {
                        if (NormalizePath(bm.path) == normPath)
                        {
                            isFav = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    SetPropW(hwnd, L"FavBtnIsFav", (HANDLE)(INT_PTR)isFav);
    if (wasFav != isFav)
    {
        RECT btnRc = GetFavBookmarkBtnRect(hwnd, GetFavHeaderTop());
        InvalidateRect(hwnd, &btnRc, FALSE);
    }
}

static std::wstring GetLastDirName(const std::wstring &path)
{
    std::wstring p = path;
    if (!p.empty() && p.back() == L'\\')
        p.pop_back();
    size_t pos = p.rfind(L'\\');
    if (pos != std::wstring::npos)
        return p.substr(pos + 1);
    return p;
}

static std::wstring CommonPathPrefix(const std::vector<std::wstring> &paths)
{
    if (paths.size() < 2)
        return L"";

    std::wstring prefix = paths[0];
    for (size_t i = 1; i < paths.size(); ++i)
    {
        size_t j = 0;
        while (j < prefix.size() && j < paths[i].size() && prefix[j] == paths[i][j])
            ++j;
        prefix.resize(j);
    }

    if (prefix.empty())
        return L"";

    size_t pos = prefix.rfind(L'\\');
    if (pos != std::wstring::npos)
        return prefix.substr(0, pos + 1);
    return L"";
}

// Strip common prefix from path, but keep at least 2 levels if possible
static std::wstring StripPrefixKeepLevels(const std::wstring &path, const std::wstring &prefix)
{
    if (prefix.empty())
        return path;
    if (prefix.size() > path.size() || path.compare(0, prefix.size(), prefix) != 0)
        return path;
    std::wstring stripped = path.substr(prefix.size());
    // Count remaining levels: at least one backslash means 2+ segments
    if (stripped.find(L'\\') == std::wstring::npos)
        return path;  // fewer than 2 levels, keep full path
    return stripped;
}

// ── Build bookmark display strings (pre-elided) ──

static std::wstring ElideText(HDC hdc, const std::wstring &text, int maxWidth);

static void BuildBookmarkListData(HWND hwndPanel, int preserveScrollTo)
{
    auto *bookmarks = (std::vector<BookmarkEntry> *)GetPropW(hwndPanel, L"BookmarkData");
    if (!bookmarks)
        return;

    HWND hFavList = GetDlgItem(hwndPanel, 2);
    CustomListData *data = hFavList ? GetListData(hFavList) : nullptr;
    if (!data)
        return;

const int textAreaWidth = g_settings.historyPanelWidth - LIST_PADDING * 2 - 20;
    HDC hdc = GetDC(hFavList);
    HFONT hOldFont = nullptr;
    if (hdc && g_hItemFontBold)
        hOldFont = (HFONT)SelectObject(hdc, g_hItemFontBold);

    data->items.clear();
    data->scrollOffset = 0;
    data->selectedIdx = -1;
    data->hoverIdx = -1;

    std::vector<std::wstring> bmPaths;
    bmPaths.reserve(bookmarks->size());
    for (const auto &b : *bookmarks)
        bmPaths.push_back(b.path);
    std::wstring bmPrefix = g_settings.stripCommonPrefix ? CommonPathPrefix(bmPaths) : L"";

    for (const auto &b : *bookmarks)
    {
        CustomListItem item;
        item.navPath = b.path;
        item.line1 = b.note.empty() ? GetLastDirName(b.path) : b.note;
        if (hdc) SelectObject(hdc, g_hItemFontBold);
        item.line1 = ElideText(hdc, item.line1, textAreaWidth);
        std::wstring displayPath = StripPrefixKeepLevels(b.path, bmPrefix);
        if (hdc) SelectObject(hdc, g_hItemFontSecondary);
        item.line2 = ElideText(hdc, displayPath, textAreaWidth);
        data->items.push_back(std::move(item));
    }

    if (hdc)
    {
        if (hOldFont)
            SelectObject(hdc, hOldFont);
        ReleaseDC(hFavList, hdc);
    }

    ClampScrollOffset(hFavList);

    if (preserveScrollTo >= 0 && preserveScrollTo < (int)data->items.size())
    {
        int visH = GetListVisibleHeight(hFavList);
        int itemTop = preserveScrollTo * g_itemHeight;
        if (itemTop < data->scrollOffset || itemTop + g_itemHeight > data->scrollOffset + visH)
            data->scrollOffset = itemTop;
        ClampScrollOffset(hFavList);
    }

    InvalidateRect(hFavList, NULL, TRUE);
}

// ── Note input dialog (modern modal) ──

static const wchar_t NOTE_DLG_CLASS[] = L"BookmarkNoteDlg";
static bool g_noteDlgRegistered = false;

struct NoteDialogState
{
    std::wstring note;
    bool confirmed = false;
    HWND hwndCreated = nullptr;
    HFONT hDialogFont = nullptr;
    HFONT hTitleFont = nullptr;
    bool isDark;
};

static void DrawModernButton(HDC hdc, const RECT &rc, const wchar_t *text, HFONT hFont,
                             bool isDefault, bool isHovered, bool isDark)
{
    auto colors = GetThemeColors(isDark);
    COLORREF bg = isDefault ? colors.accent : (isHovered ? colors.btnHoverBg : colors.btnBg);
    COLORREF textC = isDefault ? RGB(255, 255, 255) : colors.btnText;
    COLORREF border = isDefault ? colors.accent : colors.btnBorder;

    HRGN hRgn = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SetDCBrushColor(hdc, bg);
    FillRgn(hdc, hRgn, (HBRUSH)GetStockObject(DC_BRUSH));

    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hPen);
    DeleteObject(hRgn);

    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textC);
    RECT textRc = rc;
    DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
}

static LRESULT CALLBACK NoteDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NoteDialogState *state = (NoteDialogState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCT *cs = (CREATESTRUCT *)lp;
        state = (NoteDialogState *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);

        state->isDark = IsDarkTheme();
        state->hDialogFont = CreateFontW(-g_itemFontPixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        state->hTitleFont = CreateFontW(-g_headerFontPixelHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                                     16, 50, 318, 26, hwnd, (HMENU)100, NULL, NULL);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)state->hDialogFont, FALSE);

        HWND hOk = CreateWindowExW(0, L"BUTTON", L"\u786E\u5B9A",
                                   WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
                                   198, 86, 65, 30, hwnd, (HMENU)IDOK, NULL, NULL);
        SendMessageW(hOk, WM_SETFONT, (WPARAM)state->hDialogFont, FALSE);

        HWND hCancel = CreateWindowExW(0, L"BUTTON", L"\u53D6\u6D88",
                                       WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       270, 86, 65, 30, hwnd, (HMENU)IDCANCEL, NULL, NULL);
        SendMessageW(hCancel, WM_SETFONT, (WPARAM)state->hDialogFont, FALSE);

        RECT rcInit;
        GetClientRect(hwnd, &rcInit);
        HRGN hRgn = CreateRoundRectRgn(0, 0, rcInit.right, rcInit.bottom, 8, 8);
        SetWindowRgn(hwnd, hRgn, TRUE);

        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        auto colors = GetThemeColors(state->isDark);

        RECT rc;
        GetClientRect(hwnd, &rc);

        SetDCBrushColor(hdc, colors.bg);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));

        std::wstring pathText;
        wchar_t buf[1024] = {};
        GetDlgItemTextW(hwnd, 100, buf, 1024);

        HFONT hOldFont = (HFONT)SelectObject(hdc, state->hTitleFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors.itemText);

        RECT titleRc = {16, 12, rc.right - 16, 36};
        DrawTextW(hdc, L"\u6DFB\u52A0\u6536\u85CF", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, state->hDialogFont);
        SetTextColor(hdc, colors.itemTextSecondary);
        RECT labelRc = {16, 36, 80, 50};
        DrawTextW(hdc, L"\u540D\u79F0\uFF1A", -1, &labelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hOldFont);

        HPEN hPen = CreatePen(PS_SOLID, 1, colors.sep);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, 16, rc.bottom - 42, NULL);
        LineTo(hdc, rc.right - 16, rc.bottom - 42);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wp;
        auto colors = GetThemeColors(state->isDark);
        SetBkColor(hdc, colors.bg);
        SetTextColor(hdc, colors.itemTextSecondary);
        return (LRESULT)GetStockObject(DC_BRUSH);
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wp;
        auto colors = GetThemeColors(state->isDark);
        SetBkColor(hdc, colors.editBg);
        SetTextColor(hdc, colors.itemText);
        return (LRESULT)GetStockObject(DC_BRUSH);
    }
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lp;
        if (pdis->CtlType == ODT_BUTTON)
        {
            bool isDefault = (pdis->CtlID == IDOK);
            bool isHovered = (pdis->itemState & ODS_SELECTED) == 0;
            wchar_t text[32] = {};
            GetWindowTextW(pdis->hwndItem, text, 32);
            DrawModernButton(pdis->hDC, pdis->rcItem, text,
                             state->hDialogFont, isDefault, isHovered, state->isDark);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
    {
        if (!state)
            break;
        if (LOWORD(wp) == IDOK)
        {
            wchar_t buf[1024] = {};
            GetDlgItemTextW(hwnd, 100, buf, 1024);
            state->note = buf;
            state->confirmed = true;
            DestroyWindow(hwnd);
        }
        else if (LOWORD(wp) == IDCANCEL)
        {
            state->confirmed = false;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_NCHITTEST:
    {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (pt.y < 45)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DESTROY:
    {
        if (state)
        {
            state->hwndCreated = nullptr;
            if (state->hDialogFont)
            {
                DeleteObject(state->hDialogFont);
                state->hDialogFont = nullptr;
            }
            if (state->hTitleFont)
            {
                DeleteObject(state->hTitleFont);
                state->hTitleFont = nullptr;
            }
        }
        HWND hwndParent = GetWindow(hwnd, GW_OWNER);
        if (hwndParent)
        {
            EnableWindow(hwndParent, TRUE);
            SetForegroundWindow(hwndParent);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool PromptBookmarkNote(HWND hwndParent, const std::wstring &path, const std::wstring &initialNote, std::wstring &outNote)
{
    if (!g_noteDlgRegistered)
    {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc = NoteDialogProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = NOTE_DLG_CLASS;
        RegisterClassExW(&wc);
        g_noteDlgRegistered = true;
    }

    NoteDialogState state;

    RECT parentRc;
    GetWindowRect(hwndParent, &parentRc);
    int dlgW = 350, dlgH = 128;

    HWND hDlg = CreateWindowExW(0, NOTE_DLG_CLASS, L"",
                                WS_POPUP,
                                parentRc.left + (parentRc.right - parentRc.left - dlgW) / 2,
                                parentRc.top + (parentRc.bottom - parentRc.top - dlgH) / 2,
                                dlgW, dlgH,
                                hwndParent, NULL, g_hInst, &state);
    if (!hDlg)
        return false;

    state.hwndCreated = hDlg;
    SetDlgItemTextW(hDlg, 100, initialNote.c_str());

    EnableWindow(hwndParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);

    MSG msg;
    while (state.hwndCreated && GetMessageW(&msg, NULL, 0, 0))
    {
        if (!state.hwndCreated || !IsDialogMessageW(hDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (state.confirmed)
        outNote = std::move(state.note);
    return state.confirmed;
}

// ── Companion panel window procedure ──

static std::wstring ElideText(HDC hdc, const std::wstring &text, int maxWidth)
{
    if (maxWidth <= 0 || !hdc)
        return text;
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
    if (sz.cx <= maxWidth)
        return text;
    int fit = 0;
    GetTextExtentExPointW(hdc, text.c_str(), (int)text.size(), maxWidth, &fit, NULL, &sz);
    if (fit > 3)
        return text.substr(0, fit - 3) + L"...";
    if (fit > 0)
        return text.substr(0, fit);
    return text;
}

static void RefreshHistoryList(HWND hwndPanel)
{
    HWND hHistList = GetDlgItem(hwndPanel, 1);
    if (!hHistList)
        return;

    CustomListData *data = GetListData(hHistList);
    if (!data)
        return;

    std::vector<std::wstring> paths = LoadHistoryPaths();
    int displayMax = g_settings.historyDisplayMax < 1 ? 1 : g_settings.historyDisplayMax;
    size_t storeCount = paths.size() < (size_t)displayMax ? paths.size() : (size_t)displayMax;

    data->items.clear();
    data->scrollOffset = 0;
    data->selectedIdx = -1;
    data->hoverIdx = -1;

    std::wstring histPrefix = CommonPathPrefix(std::vector<std::wstring>(paths.begin(), paths.begin() + storeCount));

    const int textAreaWidth = g_settings.historyPanelWidth - LIST_PADDING * 2 - 20;
    HDC hdc = GetDC(hHistList);
    HFONT hOldFont = nullptr;
    if (hdc && g_hItemFontBold)
        hOldFont = (HFONT)SelectObject(hdc, g_hItemFontBold);

    for (size_t i = 0; i < storeCount; ++i)
    {
        CustomListItem item;
        item.navPath = paths[i];
        if (hdc) SelectObject(hdc, g_hItemFontBold);
        item.line1 = ElideText(hdc, GetLastDirName(paths[i]), textAreaWidth);
        std::wstring displayPath = StripPrefixKeepLevels(paths[i], histPrefix);
        if (hdc) SelectObject(hdc, g_hItemFontSecondary);
        item.line2 = ElideText(hdc, displayPath, textAreaWidth);
        data->items.push_back(std::move(item));
    }

    if (hdc)
    {
        if (hOldFont)
            SelectObject(hdc, hOldFont);
        ReleaseDC(hHistList, hdc);
    }

    auto *oldPaths = (std::vector<std::wstring> *)GetPropW(hwndPanel, L"HistoryPaths");
    auto *newPaths = new std::vector<std::wstring>(paths.begin(), paths.begin() + storeCount);
    SetPropW(hwndPanel, L"HistoryPaths", (HANDLE)newPaths);
    if (oldPaths)
        delete oldPaths;

    InvalidateRect(hHistList, NULL, TRUE);
}

LRESULT CALLBACK CompanionPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        if (HIWORD(wp) == LBN_SELCHANGE)
        {
            HWND hWndList = (HWND)lp;
            int ctlId = GetDlgCtrlID(hWndList);
            CustomListData *listData = GetListData(hWndList);
            if (listData && listData->selectedIdx >= 0 && listData->selectedIdx < (int)listData->items.size())
            {
                HWND hwndDialog = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
                if (hwndDialog && IsWindow(hwndDialog))
                {
                    std::wstring navPath = listData->items[listData->selectedIdx].navPath;
                    if (!navPath.empty())
                    {
                        auto *pathCopy = new std::wstring(std::move(navPath));
                        PostMessageW(hwndDialog, WM_NAVIGATE_PATH, 0, (LPARAM)pathCopy);
                    }
                }
            }
        }
        return 0;
    }
    case WM_CREATE:
    {
        CREATESTRUCT *cs = (CREATESTRUCT *)lp;
        if (!g_hItemFont)
        {
            g_hItemFont = CreateFontW(-g_itemFontPixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        }
        if (!g_hHeaderFont)
        {
            g_hHeaderFont = CreateFontW(-g_headerFontPixelHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        }
        if (!g_hItemFontBold)
        {
            g_hItemFontBold = CreateFontW(-g_itemFontPixelHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        }
        if (!g_hItemFontSecondary)
        {
            g_hItemFontSecondary = CreateFontW(-g_itemFontPixelHeightSecondary, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        }
        int cx = cs->cx - LIST_PADDING * 2;
        int favListTop = GetFavHeaderTop() + g_favHeaderHeight;

        HWND hHistList = CreateWindowW(CLS_CUSTOM_LIST, NULL,
                                       WS_CHILD | WS_VISIBLE,
                                       LIST_PADDING, g_headerHeight, cx, g_historyListHeight,
                                       hwnd, (HMENU)1, g_hInst, NULL);
        SetPropW(hHistList, L"ListData", (HANDLE)new CustomListData());

        HWND hFavList = CreateWindowW(CLS_CUSTOM_LIST, NULL,
                                      WS_CHILD | WS_VISIBLE,
                                      LIST_PADDING, favListTop, cx, cs->cy - favListTop - LIST_PADDING,
                                      hwnd, (HMENU)2, g_hInst, NULL);
        {
            auto *favData = new CustomListData();
            favData->isFav = true;
            SetPropW(hFavList, L"ListData", (HANDLE)favData);
        }

        int expListTop = GetExplorerHeaderTop() + g_explorerHeaderHeight;
        HWND hExpList = CreateWindowW(CLS_CUSTOM_LIST, NULL,
                                      WS_CHILD | WS_VISIBLE,
                                      LIST_PADDING, expListTop, cx, g_explorerListHeight,
                                      hwnd, (HMENU)3, g_hInst, NULL);
        SetPropW(hExpList, L"ListData", (HANDLE)new CustomListData());

        SetPropW(hwnd, L"BtnHovered", (HANDLE)(INT_PTR)0);
        SetPropW(hwnd, L"HistSaveBtnHover", (HANDLE)(INT_PTR)0);
        SetPropW(hwnd, L"HistExpBtnHover", (HANDLE)(INT_PTR)0);
        SetPropW(hwnd, L"FavBtnIsFav", (HANDLE)(INT_PTR)0);

        SetTimer(hwnd, 1, 500, NULL);

        return 0;
    }
    case WM_TIMER:
    {
        if (wp == 1)
        {
            UpdateFavBtnMode(hwnd);
            return 0;
        }
        break;
    }
    case WM_SIZE:
    {
        int cx = LOWORD(lp) - LIST_PADDING * 2;
        int expListTop = GetExplorerHeaderTop() + g_explorerHeaderHeight;
        int favListTop = GetFavHeaderTop() + g_favHeaderHeight;

        HWND hHistList = GetDlgItem(hwnd, 1);
        if (hHistList)
            SetWindowPos(hHistList, NULL, LIST_PADDING, g_headerHeight, cx, g_historyListHeight, SWP_NOZORDER);

        HWND hExpList = GetDlgItem(hwnd, 3);
        if (hExpList)
            SetWindowPos(hExpList, NULL, LIST_PADDING, expListTop, cx, g_explorerListHeight, SWP_NOZORDER);

        HWND hFavList = GetDlgItem(hwnd, 2);
        if (hFavList)
            SetWindowPos(hFavList, NULL, LIST_PADDING, favListTop, cx, HIWORD(lp) - favListTop - LIST_PADDING, SWP_NOZORDER);

        SetPanelRegion(hwnd);
        return 0;
    }
    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        auto colors = GetThemeColors(IsDarkCached());
        SetDCBrushColor((HDC)wp, colors.bg);
        FillRect((HDC)wp, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
        return TRUE;
    }

    case WM_MOUSEWHEEL:
    {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        POINT ptClient = pt;
        ScreenToClient(hwnd, &ptClient);
        for (int id = 1; id <= 3; id++)
        {
            HWND hList = GetDlgItem(hwnd, id);
            if (hList)
            {
                RECT rc;
                GetWindowRect(hList, &rc);
                if (pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom)
                {
                    SendMessageW(hList, WM_MOUSEWHEEL, wp, lp);
                    break;
                }
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        RECT saveBtnRc = GetHistBtnRect(hwnd, 0, 1);
        bool saveWasHover = (GetPropW(hwnd, L"HistSaveBtnHover") != nullptr);
        bool saveIsHover = PtInRect(&saveBtnRc, pt) != 0;
        if (saveWasHover != saveIsHover)
        {
            SetPropW(hwnd, L"HistSaveBtnHover", (HANDLE)(INT_PTR)saveIsHover);
            InvalidateRect(hwnd, &saveBtnRc, FALSE);
        }

        RECT expBtnRc = GetHistBtnRect(hwnd, 0, 0);
        bool expWasHover = (GetPropW(hwnd, L"HistExpBtnHover") != nullptr);
        bool expIsHover = PtInRect(&expBtnRc, pt) != 0;
        if (expWasHover != expIsHover)
        {
            SetPropW(hwnd, L"HistExpBtnHover", (HANDLE)(INT_PTR)expIsHover);
            InvalidateRect(hwnd, &expBtnRc, FALSE);
        }

        int favHeaderTop = GetFavHeaderTop();
        RECT btnRc = GetFavBookmarkBtnRect(hwnd, favHeaderTop);
        {
            RECT checkRc = btnRc;
            InflateRect(&checkRc, 20, 20);
            if (PtInRect(&checkRc, pt))
                UpdateFavBtnMode(hwnd);
        }
        bool wasHovered = (GetPropW(hwnd, L"BtnHovered") != nullptr);
        bool isHovered = PtInRect(&btnRc, pt) != 0;
        if (wasHovered != isHovered)
        {
            SetPropW(hwnd, L"BtnHovered", (HANDLE)(INT_PTR)isHovered);
            InvalidateRect(hwnd, &btnRc, FALSE);
        }

        TRACKMOUSEEVENT tme = {sizeof(tme)};
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        bool saveBtnHover = (GetPropW(hwnd, L"HistSaveBtnHover") != nullptr);
        if (saveBtnHover)
        {
            SetPropW(hwnd, L"HistSaveBtnHover", (HANDLE)(INT_PTR)0);
            RECT saveBtnRc = GetHistBtnRect(hwnd, 0, 1);
            InvalidateRect(hwnd, &saveBtnRc, FALSE);
        }
        bool expBtnHover = (GetPropW(hwnd, L"HistExpBtnHover") != nullptr);
        if (expBtnHover)
        {
            SetPropW(hwnd, L"HistExpBtnHover", (HANDLE)(INT_PTR)0);
            RECT expBtnRc = GetHistBtnRect(hwnd, 0, 0);
            InvalidateRect(hwnd, &expBtnRc, FALSE);
        }
        bool wasHovered = (GetPropW(hwnd, L"BtnHovered") != nullptr);
        if (wasHovered)
        {
            SetPropW(hwnd, L"BtnHovered", (HANDLE)(INT_PTR)0);
            int favHeaderTop = GetFavHeaderTop();
            RECT btnRc = GetFavBookmarkBtnRect(hwnd, favHeaderTop);
            InvalidateRect(hwnd, &btnRc, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        // Save-to-history button
        RECT saveBtnRc = GetHistBtnRect(hwnd, 0, 1);
        if (PtInRect(&saveBtnRc, pt))
        {
            HWND hwndDialog = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (hwndDialog && IsWindow(hwndDialog))
            {
                SendMessageTimeoutW(hwndDialog, WM_QUERY_FOLDER_PATH, 0, 0, SMTO_ABORTIFHUNG, 200, NULL);
                std::wstring path = GetSelectedPath(hwndDialog);
                if (!path.empty())
                {
                    WritePathToHistory(path);
                    RefreshHistoryList(hwnd);
                }
            }
            return 0;
        }

        // Open-in-explorer button
        RECT expBtnRc = GetHistBtnRect(hwnd, 0, 0);
        if (PtInRect(&expBtnRc, pt))
        {
            HWND hwndDialog = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (hwndDialog && IsWindow(hwndDialog))
            {
                SendMessageTimeoutW(hwndDialog, WM_QUERY_FOLDER_PATH, 0, 0, SMTO_ABORTIFHUNG, 200, NULL);
                std::wstring path = GetSelectedPath(hwndDialog);
                if (!path.empty())
                {
                    std::wstring dirPath = path;
                    DWORD attrs = GetFileAttributesW(dirPath.c_str());
                    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
                    {
                        size_t pos = dirPath.rfind(L'\\');
                        if (pos != std::wstring::npos && pos > 0)
                            dirPath = dirPath.substr(0, pos);
                    }
                    ShellExecuteW(NULL, L"explore", dirPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
            }
            return 0;
        }

        int favHeaderTop = GetFavHeaderTop();
        RECT btnRc = GetFavBookmarkBtnRect(hwnd, favHeaderTop);
        if (!PtInRect(&btnRc, pt))
            return 0;

        HWND hwndDialog = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!hwndDialog || !IsWindow(hwndDialog))
            return 0;

        SendMessageTimeoutW(hwndDialog, WM_QUERY_FOLDER_PATH, 0, 0, SMTO_ABORTIFHUNG, 200, NULL);
        std::wstring path = GetSelectedPath(hwndDialog);
        if (path.empty())
            return 0;

        auto *bookmarks = (std::vector<BookmarkEntry> *)GetPropW(hwnd, L"BookmarkData");
        if (!bookmarks)
            return 0;

        std::wstring normPath = NormalizePath(path);

        int existingIdx = -1;
        for (int i = 0; i < (int)bookmarks->size(); ++i)
        {
            if (NormalizePath((*bookmarks)[i].path) == normPath)
            {
                existingIdx = i;
                break;
            }
        }

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t ts[32];
        swprintf_s(ts, L"%04d-%02d-%02dT%02d:%02d:%02d",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        if (existingIdx >= 0)
        {
            std::wstring note;
            if (!PromptBookmarkNote(hwnd, path, (*bookmarks)[existingIdx].note, note))
                return 0;
            (*bookmarks)[existingIdx].note = note;
            (*bookmarks)[existingIdx].timestamp = ts;
            SaveBookmarks(*bookmarks);
            BuildBookmarkListData(hwnd);
        }
        else
        {
            std::wstring note;
            if (!PromptBookmarkNote(hwnd, path, GetLastDirName(path), note))
                return 0;
            BookmarkEntry entry;
            entry.path = path;
            entry.note = note;
            entry.timestamp = ts;
            bookmarks->insert(bookmarks->begin(), entry);
            SaveBookmarks(*bookmarks);
            BuildBookmarkListData(hwnd);
        }
        UpdateFavBtnMode(hwnd, true);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;

        bool isDark = IsDarkCached();
        auto colors = GetThemeColors(isDark);

        WindowBackBuffer *backBuffer = GetWindowBackBuffer(hwnd, hdc, cx, cy);
        if (!backBuffer)
        {
            EndPaint(hwnd, &ps);
            return 0;
        }
        HDC memDC = backBuffer->dc;

        SetDCBrushColor(memDC, colors.bg);
        FillRect(memDC, &rc, (HBRUSH)GetStockObject(DC_BRUSH));

        // ── Phase 1: GDI drawing (backgrounds, borders, lines) ──

        // Header background
        RECT headerRc = {0, 0, rc.right, g_headerHeight};
        SetDCBrushColor(memDC, colors.headerBg);
        FillRect(memDC, &headerRc, (HBRUSH)GetStockObject(DC_BRUSH));

        // Save-to-history button
        RECT saveBtnRc = GetHistBtnRect(hwnd, 0, 1);
        bool saveBtnHover = (GetPropW(hwnd, L"HistSaveBtnHover") != nullptr);
        SetDCBrushColor(memDC, saveBtnHover ? colors.btnHoverBg : colors.btnBg);
        FillRect(memDC, &saveBtnRc, (HBRUSH)GetStockObject(DC_BRUSH));
        {
            HPEN hPen = CreatePen(PS_SOLID, 1, saveBtnHover ? colors.accent : colors.btnBorder);
            HPEN hOldPen2 = (HPEN)SelectObject(memDC, hPen);
            HBRUSH hOldBrush2 = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
            RoundRect(memDC, saveBtnRc.left, saveBtnRc.top, saveBtnRc.right, saveBtnRc.bottom, 6, 6);
            SelectObject(memDC, hOldPen2);
            SelectObject(memDC, hOldBrush2);
            DeleteObject(hPen);
        }

        // Open-in-explorer button
        RECT expBtnRc = GetHistBtnRect(hwnd, 0, 0);
        bool expBtnHover = (GetPropW(hwnd, L"HistExpBtnHover") != nullptr);
        SetDCBrushColor(memDC, expBtnHover ? colors.btnHoverBg : colors.btnBg);
        FillRect(memDC, &expBtnRc, (HBRUSH)GetStockObject(DC_BRUSH));
        {
            HPEN hPen = CreatePen(PS_SOLID, 1, expBtnHover ? colors.accent : colors.btnBorder);
            HPEN hOldPen2 = (HPEN)SelectObject(memDC, hPen);
            HBRUSH hOldBrush2 = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
            RoundRect(memDC, expBtnRc.left, expBtnRc.top, expBtnRc.right, expBtnRc.bottom, 6, 6);
            SelectObject(memDC, hOldPen2);
            SelectObject(memDC, hOldBrush2);
            DeleteObject(hPen);
        }

        // Separator under header
        HPEN hSepPen = CreatePen(PS_SOLID, 1, colors.sep);
        HPEN hOldSepPen = (HPEN)SelectObject(memDC, hSepPen);
        MoveToEx(memDC, 10, g_headerHeight - 1, NULL);
        LineTo(memDC, rc.right - 10, g_headerHeight - 1);
        SelectObject(memDC, hOldSepPen);
        DeleteObject(hSepPen);

        // Explorer paths section header background
        int expHeaderTop = GetExplorerHeaderTop();
        RECT expHeaderRc = {0, expHeaderTop, rc.right, expHeaderTop + g_explorerHeaderHeight};
        SetDCBrushColor(memDC, colors.headerBg);
        FillRect(memDC, &expHeaderRc, (HBRUSH)GetStockObject(DC_BRUSH));

        // Separator under explorer header
        HPEN hSepPenExp = CreatePen(PS_SOLID, 1, colors.sep);
        HPEN hOldSepPenExp = (HPEN)SelectObject(memDC, hSepPenExp);
        MoveToEx(memDC, 10, expHeaderTop + g_explorerHeaderHeight - 1, NULL);
        LineTo(memDC, rc.right - 10, expHeaderTop + g_explorerHeaderHeight - 1);
        SelectObject(memDC, hOldSepPenExp);
        DeleteObject(hSepPenExp);

        // Favorites section header background
        int favHeaderTop = GetFavHeaderTop();
        RECT favHeaderRc = {0, favHeaderTop, rc.right, favHeaderTop + g_favHeaderHeight};
        SetDCBrushColor(memDC, colors.headerBg);
        FillRect(memDC, &favHeaderRc, (HBRUSH)GetStockObject(DC_BRUSH));

        // Bookmark button
        RECT btnRc = GetFavBookmarkBtnRect(hwnd, favHeaderTop);
        bool isBtnHovered = (GetPropW(hwnd, L"BtnHovered") != nullptr);
        SetDCBrushColor(memDC, isBtnHovered ? colors.btnHoverBg : colors.btnBg);
        FillRect(memDC, &btnRc, (HBRUSH)GetStockObject(DC_BRUSH));
        {
            HPEN hBtnPen = CreatePen(PS_SOLID, 1, isBtnHovered ? colors.accent : colors.btnBorder);
            HPEN hOldBtnPen = (HPEN)SelectObject(memDC, hBtnPen);
            HBRUSH hOldBtnBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
            RoundRect(memDC, btnRc.left, btnRc.top, btnRc.right, btnRc.bottom, 6, 6);
            SelectObject(memDC, hOldBtnPen);
            SelectObject(memDC, hOldBtnBrush);
            DeleteObject(hBtnPen);
        }

        // Separator under fav header
        HPEN hSepPen2 = CreatePen(PS_SOLID, 1, colors.sep);
        HPEN hOldSepPen2 = (HPEN)SelectObject(memDC, hSepPen2);
        MoveToEx(memDC, 10, favHeaderTop + g_favHeaderHeight - 1, NULL);
        LineTo(memDC, rc.right - 10, favHeaderTop + g_favHeaderHeight - 1);
        SelectObject(memDC, hOldSepPen2);
        DeleteObject(hSepPen2);

        // Border
        HPEN hBorderPen = CreatePen(PS_SOLID, 1, colors.border);
        HBRUSH hOldBorderBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
        HPEN hOldPen = (HPEN)SelectObject(memDC, hBorderPen);
        RoundRect(memDC, 0, 0, rc.right - 1, rc.bottom - 1, CORNER_RADIUS, CORNER_RADIUS);
        SelectObject(memDC, hOldPen);
        SelectObject(memDC, hOldBorderBrush);
        DeleteObject(hBorderPen);

        // ── Phase 2: DirectWrite text rendering ──

        RECT titleRc = {14, 0, rc.right - 90, g_headerHeight};
        DWriteDrawText(memDC, titleRc, L"\u5386\u53F2\u8BB0\u5F55", g_pHeaderTextFormat, g_hHeaderFont, colors.itemText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DWriteDrawText(memDC, saveBtnRc, L"\U0001F5C3", g_pHeaderTextFormat, g_hHeaderFont, saveBtnHover ? colors.accent : colors.btnText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DWriteDrawText(memDC, expBtnRc, L"\U0001F4C2", g_pHeaderTextFormat, g_hHeaderFont, expBtnHover ? colors.accent : colors.btnText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT expTitleRc = {14, expHeaderTop, rc.right - 14, expHeaderTop + g_explorerHeaderHeight};
        DWriteDrawText(memDC, expTitleRc, L"\u5DF2\u6253\u5F00\u7684\u6587\u4EF6\u5939", g_pHeaderTextFormat, g_hHeaderFont, colors.itemText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT favTitleRc = {14, favHeaderTop, rc.right - 54, favHeaderTop + g_favHeaderHeight};
        DWriteDrawText(memDC, favTitleRc, L"\u6536\u85CF", g_pHeaderTextFormat, g_hHeaderFont, colors.itemText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        bool isFav = (GetPropW(hwnd, L"FavBtnIsFav") != nullptr);
        DWriteDrawText(memDC, btnRc, isFav ? L"\U0001F4AB" : L"\u2B50", g_pHeaderTextFormat, g_hHeaderFont, isBtnHovered ? colors.accent : colors.btnText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        BitBlt(hdc, 0, 0, cx, cy, memDC, 0, 0, SRCCOPY);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_REFRESH_FAV_STATE:
    {
        UpdateFavBtnMode(hwnd, true);
        return 0;
    }
    case WM_REFRESH_EXPLORER_PATHS:
    {
        std::vector<std::wstring> paths = LoadExplorerPaths();
        size_t storeCount = paths.size() < (size_t)EXPLORER_MAX ? paths.size() : EXPLORER_MAX;

        auto *oldPaths = (std::vector<std::wstring> *)GetPropW(hwnd, L"ExplorerPaths");
        bool changed = (!oldPaths || oldPaths->size() != storeCount);
        if (!changed)
        {
            for (size_t i = 0; i < storeCount; ++i)
            {
                if ((*oldPaths)[i] != paths[i])
                {
                    changed = true;
                    break;
                }
            }
        }

        if (changed)
        {
            HWND hExpList = GetDlgItem(hwnd, 3);
            CustomListData *data = hExpList ? GetListData(hExpList) : nullptr;
            if (data)
            {
                const int textAreaWidth = g_settings.historyPanelWidth - LIST_PADDING * 2 - 20;
                HDC hdc = GetDC(hExpList);
                HFONT hOldFont = nullptr;
                if (hdc && g_hItemFontBold)
                    hOldFont = (HFONT)SelectObject(hdc, g_hItemFontBold);

                data->items.clear();
                data->scrollOffset = 0;
                data->selectedIdx = -1;
                data->hoverIdx = -1;

                std::wstring expPrefix = g_settings.stripCommonPrefix ? CommonPathPrefix(std::vector<std::wstring>(paths.begin(), paths.begin() + storeCount)) : L"";

                for (size_t i = 0; i < storeCount; ++i)
                {
                    CustomListItem item;
                    item.navPath = paths[i];
                    if (hdc) SelectObject(hdc, g_hItemFontBold);
                    item.line1 = ElideText(hdc, GetLastDirName(paths[i]), textAreaWidth);
                    std::wstring displayPath = StripPrefixKeepLevels(paths[i], expPrefix);
                    if (hdc) SelectObject(hdc, g_hItemFontSecondary);
                    item.line2 = ElideText(hdc, displayPath, textAreaWidth);
                    data->items.push_back(std::move(item));
                }

                if (hdc)
                {
                    if (hOldFont)
                        SelectObject(hdc, hOldFont);
                    ReleaseDC(hExpList, hdc);
                }

                ClampScrollOffset(hExpList);
                InvalidateRect(hExpList, NULL, TRUE);
            }

            auto *newPaths = new std::vector<std::wstring>(paths.begin(), paths.begin() + storeCount);
            SetPropW(hwnd, L"ExplorerPaths", (HANDLE)newPaths);
            if (oldPaths)
                delete oldPaths;
        }
        return 0;
    }
    case WM_REPOSITION_PANEL:
    {
        MSG peek;
        while (PeekMessageW(&peek, hwnd, WM_REPOSITION_PANEL, WM_REPOSITION_PANEL, PM_REMOVE))
        {
        }
        HWND hwndDialog = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (hwndDialog && IsWindow(hwndDialog))
            PositionPanel(hwndDialog, hwnd);
        return 0;
    }
    case WM_DESTROY:
    {
        KillTimer(hwnd, 1);
        DestroyWindowBackBuffer(hwnd);
        auto *paths = (std::vector<std::wstring> *)GetPropW(hwnd, L"HistoryPaths");
        if (paths)
        {
            delete paths;
            RemovePropW(hwnd, L"HistoryPaths");
        }
        auto *expPaths = (std::vector<std::wstring> *)GetPropW(hwnd, L"ExplorerPaths");
        if (expPaths)
        {
            delete expPaths;
            RemovePropW(hwnd, L"ExplorerPaths");
        }
        auto *bmData = (std::vector<BookmarkEntry> *)GetPropW(hwnd, L"BookmarkData");
        if (bmData)
        {
            delete bmData;
            RemovePropW(hwnd, L"BookmarkData");
        }
        RemovePropW(hwnd, L"BtnHovered");
        RemovePropW(hwnd, L"HistSaveBtnHover");
        RemovePropW(hwnd, L"HistExpBtnHover");
        RemovePropW(hwnd, L"FavBtnIsFav");

        HWND hwndDialog = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (hwndDialog)
        {
            EnterCriticalSection(&g_cs);
            auto it = g_dialogs.find(hwndDialog);
            if (it != g_dialogs.end() && it->second.hwndPanel == hwnd)
                it->second.hwndPanel = NULL;
            LeaveCriticalSection(&g_cs);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Panel creation ──

HWND CreateCompanionPanel(HWND hwndDialog)
{
    HWND hwndPanel = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        PANEL_CLASS,
        L"History",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, g_settings.historyPanelWidth, 300,
        hwndDialog, NULL, g_hInst, NULL);

    if (!hwndPanel)
        return NULL;

    SetWindowLongPtrW(hwndPanel, GWLP_USERDATA, (LONG_PTR)hwndDialog);

    // Populate history list
    {
        HWND hHistList = GetDlgItem(hwndPanel, 1);
        CustomListData *data = GetListData(hHistList);
        if (data)
        {
            std::vector<std::wstring> paths = LoadHistoryPaths();
            int displayMax = g_settings.historyDisplayMax < 1 ? 1 : g_settings.historyDisplayMax;
            size_t storeCount = paths.size() < (size_t)displayMax ? paths.size() : (size_t)displayMax;

std::wstring histPrefix = g_settings.stripCommonPrefix ? CommonPathPrefix(std::vector<std::wstring>(paths.begin(), paths.begin() + storeCount)) : L"";

            const int textAreaWidth = g_settings.historyPanelWidth - LIST_PADDING * 2 - 20;
            HDC hdc = GetDC(hHistList);
            HFONT hOldFont = nullptr;
            if (hdc && g_hItemFontBold)
                hOldFont = (HFONT)SelectObject(hdc, g_hItemFontBold);

            for (size_t i = 0; i < storeCount; ++i)
            {
                CustomListItem item;
                item.navPath = paths[i];
                if (hdc) SelectObject(hdc, g_hItemFontBold);
                item.line1 = ElideText(hdc, GetLastDirName(paths[i]), textAreaWidth);
                std::wstring displayPath = StripPrefixKeepLevels(paths[i], histPrefix);
                if (hdc) SelectObject(hdc, g_hItemFontSecondary);
                item.line2 = ElideText(hdc, displayPath, textAreaWidth);
                data->items.push_back(std::move(item));
            }

            if (hdc)
            {
                if (hOldFont)
                    SelectObject(hdc, hOldFont);
                ReleaseDC(hHistList, hdc);
            }

            auto *storedPaths = new std::vector<std::wstring>(paths.begin(), paths.begin() + storeCount);
            SetPropW(hwndPanel, L"HistoryPaths", (HANDLE)storedPaths);
            InvalidateRect(hHistList, NULL, TRUE);
        }
    }

    // Populate explorer paths list
    {
        std::vector<std::wstring> expPaths = LoadExplorerPaths();
        size_t expCount = expPaths.size() < (size_t)EXPLORER_MAX ? expPaths.size() : EXPLORER_MAX;

        std::wstring expPrefix = g_settings.stripCommonPrefix ? CommonPathPrefix(std::vector<std::wstring>(expPaths.begin(), expPaths.begin() + expCount)) : L"";

        HWND hExpList = GetDlgItem(hwndPanel, 3);
        CustomListData *data = GetListData(hExpList);
        if (data)
        {
            const int textAreaWidth = g_settings.historyPanelWidth - LIST_PADDING * 2 - 20;
            HDC hdc = GetDC(hExpList);
            HFONT hOldFont = nullptr;
            if (hdc && g_hItemFontBold)
                hOldFont = (HFONT)SelectObject(hdc, g_hItemFontBold);

            for (size_t i = 0; i < expCount; ++i)
            {
                CustomListItem item;
                item.navPath = expPaths[i];
                if (hdc) SelectObject(hdc, g_hItemFontBold);
                item.line1 = ElideText(hdc, GetLastDirName(expPaths[i]), textAreaWidth);
                std::wstring displayPath = StripPrefixKeepLevels(expPaths[i], expPrefix);
                if (hdc) SelectObject(hdc, g_hItemFontSecondary);
                item.line2 = ElideText(hdc, displayPath, textAreaWidth);
                data->items.push_back(std::move(item));
            }

            if (hdc)
            {
                if (hOldFont)
                    SelectObject(hdc, hOldFont);
                ReleaseDC(hExpList, hdc);
            }

            ClampScrollOffset(hExpList);
            InvalidateRect(hExpList, NULL, TRUE);
        }

        auto *storedExpPaths = new std::vector<std::wstring>(expPaths.begin(), expPaths.begin() + expCount);
        SetPropW(hwndPanel, L"ExplorerPaths", (HANDLE)storedExpPaths);
    }

    auto *bmData = new std::vector<BookmarkEntry>(LoadBookmarks());
    SetPropW(hwndPanel, L"BookmarkData", (HANDLE)bmData);
    BuildBookmarkListData(hwndPanel);

    PositionPanel(hwndDialog, hwndPanel);
    ShowWindow(hwndPanel, SW_SHOWNA);
    SetPanelRegion(hwndPanel);
    UpdateFavBtnMode(hwndPanel, true);

    return hwndPanel;
}
