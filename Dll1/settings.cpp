#include "pch.h"
#include "settings.h"

Settings g_settings;

static std::wstring GetSettingsFilePath()
{
    std::wstring dir = GetDataDir();
    if (dir.empty())
        return L"";
    return dir + L"\\Setting.ini";
}

static void TrimString(std::wstring &s)
{
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r' || s.back() == L'\n'))
        s.pop_back();
    size_t start = 0;
    while (start < s.size() && (s[start] == L' ' || s[start] == L'\t'))
        ++start;
    if (start > 0)
        s = s.substr(start);
}

void EnsureSettingsFile()
{
    std::wstring filePath = GetSettingsFilePath();
    if (filePath.empty())
        return;

    DWORD attribs = GetFileAttributesW(filePath.c_str());
    if (attribs != INVALID_FILE_ATTRIBUTES)
        return;

    size_t pos = filePath.rfind(L'\\');
    if (pos != std::wstring::npos)
        SHCreateDirectoryExW(NULL, filePath.substr(0, pos).c_str(), NULL);

    const char *defaultContent =
        "# 控制是否在弹出文件或文件夹选择弹出层的时候，导航到最近一条历史\r\n"
        "AutoToLatest=false\r\n"
        "\r\n"
        "# 主题模式 (light/dark/auto)\r\n"
        "theme=light\r\n"
        "\r\n"
        "# 历史记录面板的宽度\r\n"
        "HistoryPanelWidth=200\r\n"
        "\r\n"
        "# 面板和窗口的间距\r\n"
        "PanelMargin=5\r\n"
        "\r\n"
        "# 历史记录面板字体大小（磅）\r\n"
        "HistoryPanelFontSize=10\r\n"
        "\r\n"
        "# 是否去除列表中的公共路径前缀\r\n"
        "StripCommonPrefix=true\r\n"
        "\r\n"
        "# 历史记录可显示的最大条数（面板高度固定为5条，超出可通过滚动查看）\r\n"
        "HistoryDisplayMax=5\r\n"
        "\r\n"
        "# 是否在文件对话框里显示 Everything 搜索结果面板\r\n"
        "EverythingPanel=true\r\n";

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(hFile, defaultContent, (DWORD)strlen(defaultContent), &written, NULL);
        CloseHandle(hFile);
    }
}

void LoadSettings(Settings &s)
{
    std::wstring filePath = GetSettingsFilePath();
    if (filePath.empty())
        return;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE || fileSize > 64 * 1024)
    {
        CloseHandle(hFile);
        return;
    }

    std::string content(fileSize, 0);
    DWORD read = 0;
    ReadFile(hFile, &content[0], fileSize, &read, NULL);
    CloseHandle(hFile);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), (int)content.size(), NULL, 0);
    if (wlen <= 0)
        return;
    std::wstring wcontent(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, content.c_str(), (int)content.size(), &wcontent[0], wlen);

    size_t lineStart = 0;
    while (lineStart < wcontent.size())
    {
        size_t lineEnd = wcontent.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos)
            lineEnd = wcontent.size();

        std::wstring line = wcontent.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        TrimString(line);

        if (line.empty() || line[0] == L'#' || line[0] == L';' || line[0] == L'[')
            continue;

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos)
            continue;

        std::wstring key = line.substr(0, eq);
        std::wstring value = line.substr(eq + 1);
        TrimString(key);
        TrimString(value);

        if (key == L"AutoToLatest")
            s.autoToLatest = (value == L"true" || value == L"1" || value == L"yes");
        else if (key == L"theme")
            s.theme = value;
        else if (key == L"HistoryPanelWidth")
            s.historyPanelWidth = _wtoi(value.c_str());
        else if (key == L"PanelMargin")
            s.panelMargin = _wtoi(value.c_str());
        else if (key == L"HistoryPanelFontSize")
        {
            int fs = _wtoi(value.c_str());
            if (fs > 0)
                s.historyPanelFontSize = fs;
        }
        else if (key == L"StripCommonPrefix")
            s.stripCommonPrefix = (value == L"true" || value == L"1" || value == L"yes");
        else if (key == L"HistoryDisplayMax")
        {
            int n = _wtoi(value.c_str());
            if (n >= 1)
                s.historyDisplayMax = n;
        }
        else if (key == L"EverythingPanel")
            s.searchPanelEnabled = (value == L"true" || value == L"1" || value == L"yes");
        else if (key == L"EverythingPanelWidth")
        {
            int n = _wtoi(value.c_str());
            if (n >= 200)
                s.searchPanelWidth = n;
        }
        else if (key == L"EverythingPanelHeight")
        {
            int n = _wtoi(value.c_str());
            if (n >= 80)
                s.searchPanelHeight = n;
        }
    }
}

// ── 热加载 ──
//
// 被注入的进程（记事本等）里也会跑一份 Dll1，启动时只读一次配置的话，
// 改完设置必须重启目标程序才生效。这里按"最后写入时间"判断配置有没有变，
// 由宿主线程的目录变更通知驱动。

static FILETIME g_lastSettingsWrite = {};
static bool g_settingsWriteTimeValid = false;

static bool GetSettingsWriteTime(FILETIME &out)
{
    std::wstring filePath = GetSettingsFilePath();
    if (filePath.empty())
        return false;

    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &info))
        return false;

    out = info.ftLastWriteTime;
    return true;
}

bool ReloadSettingsIfChanged(bool force)
{
    FILETIME current = {};
    if (!GetSettingsWriteTime(current))
        return false;

    if (!force && g_settingsWriteTimeValid &&
        current.dwLowDateTime == g_lastSettingsWrite.dwLowDateTime &&
        current.dwHighDateTime == g_lastSettingsWrite.dwHighDateTime)
        return false;

    g_lastSettingsWrite = current;
    g_settingsWriteTimeValid = true;

    Settings fresh;
    LoadSettings(fresh);
    g_settings = fresh;
    return true;
}

bool SaveSearchPanelSize(int width, int height)
{
    std::wstring filePath = GetSettingsFilePath();
    if (filePath.empty())
        return false;

    wchar_t buf[32] = {};

    _snwprintf_s(buf, 32, _TRUNCATE, L"%d", width);
    WritePrivateProfileStringW(L"Settings", L"EverythingPanelWidth", buf, filePath.c_str());

    _snwprintf_s(buf, 32, _TRUNCATE, L"%d", height);
    WritePrivateProfileStringW(L"Settings", L"EverythingPanelHeight", buf, filePath.c_str());

    // 同步内存值，免得自己写出来的变更又被当成"外部修改"来回弹
    g_settings.searchPanelWidth = width;
    g_settings.searchPanelHeight = height;
    return true;
}

static bool IsSystemDarkMode()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD value = 1;
        DWORD size = sizeof(value);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return value == 0;
        }
        RegCloseKey(hKey);
    }
    return false;
}

bool IsDarkTheme()
{
    if (g_settings.theme == L"dark")
        return true;
    if (g_settings.theme == L"auto")
        return IsSystemDarkMode();
    return false;
}
