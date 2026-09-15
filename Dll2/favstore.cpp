#include "pch.h"

#include <shlobj.h>
#include <string>
#include <vector>

HINSTANCE g_hFavInst = nullptr;
FavSettings g_favSettings;

int g_favItemFontHeight = 13;
int g_favItemFontHeightSecondary = 11;
int g_favHeaderFontHeight = 14;
int g_favItemHeight = 46;
int g_favHeaderHeight = 34;

HFONT g_hFavFont = nullptr;
HFONT g_hFavFontBold = nullptr;
HFONT g_hFavFontSecondary = nullptr;
HFONT g_hFavHeaderFont = nullptr;
HFONT g_hFavIconFont = nullptr;

namespace
{

const wchar_t FAV_MUTEX_NAME[] = L"Local\\PathHelper_Favorites_Mutex";

HANDLE AcquireFavMutex(DWORD timeoutMs = 5000)
{
    HANDLE mutex = CreateMutexW(nullptr, FALSE, FAV_MUTEX_NAME);
    if (!mutex)
        return nullptr;
    if (WaitForSingleObject(mutex, timeoutMs) != WAIT_OBJECT_0)
    {
        CloseHandle(mutex);
        return nullptr;
    }
    return mutex;
}

void ReleaseFavMutex(HANDLE mutex)
{
    if (mutex)
    {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
}

std::string WstrToUTF8(const std::wstring &wstr)
{
    if (wstr.empty())
        return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], len, nullptr, nullptr);
    return result;
}

std::wstring UTF8ToWstr(const std::string &str)
{
    if (str.empty())
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], len);
    return result;
}

void AppendJsonString(std::string &out, const std::wstring &value)
{
    out += '"';
    for (char c : WstrToUTF8(value))
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                sprintf_s(buf, "\\u%04x", static_cast<unsigned char>(c));
                out += buf;
            }
            else
            {
                out += c;
            }
            break;
        }
    }
    out += '"';
}

// 从 {"key":"value", ...} 中取出一个字符串字段
bool ExtractJsonString(const std::string &object, const char *key, std::string &out)
{
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = object.find(pattern);
    if (pos == std::string::npos)
        return false;
    pos = object.find(':', pos + pattern.size());
    if (pos == std::string::npos)
        return false;
    ++pos;
    while (pos < object.size() && (object[pos] == ' ' || object[pos] == '\t'))
        ++pos;
    if (pos >= object.size() || object[pos] != '"')
        return false;
    ++pos;

    out.clear();
    while (pos < object.size())
    {
        char c = object[pos++];
        if (c == '"')
            return true;
        if (c != '\\')
        {
            out += c;
            continue;
        }
        if (pos >= object.size())
            break;
        char esc = object[pos++];
        switch (esc)
        {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case '/': out += '/'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'u':
        {
            if (pos + 4 > object.size())
                return true;
            unsigned int code = 0;
            for (int i = 0; i < 4; ++i)
            {
                char h = object[pos + i];
                code <<= 4;
                if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
            }
            pos += 4;
            // 转成 UTF-8
            std::wstring w(1, (wchar_t)code);
            out += WstrToUTF8(w);
            break;
        }
        default:
            out += esc;
            break;
        }
    }
    return true;
}

} // namespace

std::wstring GetFavDataDir()
{
    wchar_t userProfile[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return L"";
    return std::wstring(userProfile) + L"\\.PathHelper";
}

std::wstring GetFavIniPath()
{
    std::wstring dir = GetFavDataDir();
    if (dir.empty())
        return L"";
    return dir + L"\\Setting.ini";
}

std::wstring GetFavStorePath()
{
    std::wstring dir = GetFavDataDir();
    if (dir.empty())
        return L"";
    return dir + L"\\Favorites.jsonl";
}

std::vector<FavoriteEntry> LoadFavorites()
{
    std::vector<FavoriteEntry> result;

    std::wstring filePath = GetFavStorePath();
    if (filePath.empty())
        return result;

    HANDLE mutex = AcquireFavMutex();
    if (!mutex)
        return result;

    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        ReleaseFavMutex(mutex);
        return result;
    }

    DWORD size = GetFileSize(file, nullptr);
    if (size == 0 || size == INVALID_FILE_SIZE || size > 1024 * 1024)
    {
        CloseHandle(file);
        ReleaseFavMutex(mutex);
        return result;
    }

    std::string content(size, 0);
    DWORD read = 0;
    ReadFile(file, &content[0], size, &read, nullptr);
    CloseHandle(file);
    ReleaseFavMutex(mutex);

    size_t pos = 0;
    while (pos < content.size())
    {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos)
            end = content.size();
        std::string line = content.substr(pos, end - pos);
        pos = end + 1;

        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos)
            continue;
        if (line[start] != '{')
            continue;

        FavoriteEntry entry;
        std::string path;
        if (!ExtractJsonString(line, "path", path))
            continue;
        entry.path = UTF8ToWstr(path);
        if (entry.path.empty())
            continue;

        std::string note;
        if (ExtractJsonString(line, "note", note))
            entry.note = UTF8ToWstr(note);
        std::string timestamp;
        if (ExtractJsonString(line, "timestamp", timestamp))
            entry.timestamp = UTF8ToWstr(timestamp);

        result.push_back(std::move(entry));
    }

    return result;
}

void SaveFavorites(const std::vector<FavoriteEntry> &favorites)
{
    std::wstring filePath = GetFavStorePath();
    if (filePath.empty())
        return;

    HANDLE mutex = AcquireFavMutex();
    if (!mutex)
        return;

    size_t slash = filePath.rfind(L'\\');
    if (slash != std::wstring::npos)
        SHCreateDirectoryExW(nullptr, filePath.substr(0, slash).c_str(), nullptr);

    std::string output;
    for (const auto &entry : favorites)
    {
        if (entry.path.empty())
            continue;
        output += "{\"path\":";
        AppendJsonString(output, entry.path);
        if (!entry.note.empty())
        {
            output += ",\"note\":";
            AppendJsonString(output, entry.note);
        }
        if (!entry.timestamp.empty())
        {
            output += ",\"timestamp\":";
            AppendJsonString(output, entry.timestamp);
        }
        output += "}\n";
    }

    std::wstring tmpPath = filePath + L".tmp";
    HANDLE file = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(file, output.data(), (DWORD)output.size(), &written, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);
        MoveFileExW(tmpPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    ReleaseFavMutex(mutex);
}

void LoadFavSettings(FavSettings &settings)
{
    std::wstring ini = GetFavIniPath();
    if (ini.empty())
        return;

    wchar_t buf[256] = {};
    GetPrivateProfileStringW(L"Settings", L"theme", L"light", buf, 256, ini.c_str());
    settings.theme = buf;

    settings.panelWidth = GetPrivateProfileIntW(L"Settings", L"HistoryPanelWidth", 260, ini.c_str());
    if (settings.panelWidth < 160)
        settings.panelWidth = 160;
    if (settings.panelWidth > 800)
        settings.panelWidth = 800;

    settings.panelMargin = GetPrivateProfileIntW(L"Settings", L"PanelMargin", 5, ini.c_str());
    settings.fontSize = GetPrivateProfileIntW(L"Settings", L"HistoryPanelFontSize", 10, ini.c_str());
    if (settings.fontSize < 6)
        settings.fontSize = 6;
    if (settings.fontSize > 32)
        settings.fontSize = 32;

    GetPrivateProfileStringW(L"Settings", L"StripCommonPrefix", L"true", buf, 256, ini.c_str());
    settings.stripCommonPrefix = (_wcsicmp(buf, L"true") == 0 || wcscmp(buf, L"1") == 0);
}

bool ReadFavEnabledFromIni()
{
    std::wstring ini = GetFavIniPath();
    if (ini.empty())
        return false;

    wchar_t buf[32] = {};
    GetPrivateProfileStringW(L"Settings", L"ExplorerFavorites", L"false", buf, 32, ini.c_str());
    return _wcsicmp(buf, L"true") == 0 || wcscmp(buf, L"1") == 0 || _wcsicmp(buf, L"yes") == 0;
}

void WriteFavEnabledToIni(bool enabled)
{
    std::wstring ini = GetFavIniPath();
    if (ini.empty())
        return;
    WritePrivateProfileStringW(L"Settings", L"ExplorerFavorites", enabled ? L"true" : L"false", ini.c_str());
}

bool IsFavDarkTheme()
{
    if (g_favSettings.theme == L"dark")
        return true;
    if (g_favSettings.theme != L"auto")
        return false;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        DWORD value = 1;
        DWORD size = sizeof(value);
        LSTATUS st = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                                      reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(key);
        if (st == ERROR_SUCCESS)
            return value == 0;
    }
    return false;
}

FavThemeColors GetFavThemeColors(bool isDark)
{
    if (isDark)
    {
        return FavThemeColors{
            RGB(30, 30, 30), RGB(40, 40, 44), RGB(220, 220, 228), RGB(140, 140, 155),
            RGB(150, 195, 250), RGB(48, 48, 56), RGB(9, 71, 113), RGB(140, 200, 250),
            RGB(45, 48, 55), RGB(56, 56, 62), RGB(56, 56, 62), RGB(50, 50, 56),
            RGB(72, 72, 78), RGB(180, 180, 195), RGB(60, 60, 68), RGB(90, 155, 220)};
    }
    return FavThemeColors{
        RGB(255, 255, 255), RGB(248, 248, 252), RGB(30, 30, 36), RGB(120, 120, 135),
        RGB(10, 52, 120), RGB(232, 232, 240), RGB(220, 235, 252), RGB(0, 80, 180),
        RGB(242, 242, 248), RGB(226, 226, 232), RGB(218, 218, 226), RGB(245, 245, 248),
        RGB(200, 200, 210), RGB(80, 80, 95), RGB(232, 232, 240), RGB(0, 90, 180)};
}

void ComputeFavMetrics()
{
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    if (dpi <= 0)
        dpi = 96;

    g_favItemFontHeight = MulDiv(g_favSettings.fontSize, dpi, 72);
    if (g_favItemFontHeight < 8)
        g_favItemFontHeight = 8;
    g_favItemFontHeightSecondary = g_favItemFontHeight * 9 / 10;
    if (g_favItemFontHeightSecondary < 7)
        g_favItemFontHeightSecondary = 7;
    g_favHeaderFontHeight = g_favItemFontHeight + 1;
    g_favItemHeight = g_favItemFontHeight + g_favItemFontHeightSecondary + 10;
    g_favHeaderHeight = g_favHeaderFontHeight + 18;

    if (g_hFavFont) { DeleteObject(g_hFavFont); g_hFavFont = nullptr; }
    if (g_hFavFontBold) { DeleteObject(g_hFavFontBold); g_hFavFontBold = nullptr; }
    if (g_hFavFontSecondary) { DeleteObject(g_hFavFontSecondary); g_hFavFontSecondary = nullptr; }
    if (g_hFavHeaderFont) { DeleteObject(g_hFavHeaderFont); g_hFavHeaderFont = nullptr; }
    if (g_hFavIconFont) { DeleteObject(g_hFavIconFont); g_hFavIconFont = nullptr; }
}

bool EnsureFavFonts()
{
    if (g_hFavFont && g_hFavFontBold && g_hFavFontSecondary && g_hFavHeaderFont && g_hFavIconFont)
        return true;

    if (!g_hFavFont)
        g_hFavFont = CreateFontW(-g_favItemFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
    if (!g_hFavFontBold)
        g_hFavFontBold = CreateFontW(-g_favItemFontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
    if (!g_hFavFontSecondary)
        g_hFavFontSecondary = CreateFontW(-g_favItemFontHeightSecondary, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
    if (!g_hFavHeaderFont)
        g_hFavHeaderFont = CreateFontW(-g_favHeaderFontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

    if (!g_hFavIconFont)
        g_hFavIconFont = CreateFontW(-(g_favItemFontHeight + 3), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");

    return g_hFavFont && g_hFavFontBold && g_hFavFontSecondary && g_hFavHeaderFont && g_hFavIconFont;
}
