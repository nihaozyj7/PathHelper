#include "pch.h"
#include "history.h"
#include "settings.h"
#include <unordered_set>
#include <cwctype>

std::vector<HistoryEntry> g_historyCache;
bool g_historyCacheValid = false;

static std::wstring GetHistoryFilePath()
{
    std::wstring dir = GetDataDir();
    if (dir.empty())
        return L"";

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring name(exePath);
    size_t pos = name.rfind(L'\\');
    if (pos != std::wstring::npos)
        name = name.substr(pos + 1);
    pos = name.rfind(L'.');
    if (pos != std::wstring::npos)
        name = name.substr(0, pos);

    return dir + L"\\" + name + L".jsonl";
}

// ── 路径归一化（正确处理 UNC / 映射驱动器 / 根目录） ──
//
// 旧实现直接用 rfind('\\') 截断，对网络路径有两个问题：
//   1. 网络共享/离线路径 GetFileAttributes 会失败，此时即使传入的已经是文件夹也会被多截一层；
//   2. \\server\share 这类共享根会被退化成 \\server。
// 这里先区分“文件 / 文件夹 / 未知”，并且绝不上溯超过根目录。

static bool IsDriveRootPath(const std::wstring &p)
{
    if (p.size() == 2 && p[1] == L':' && iswalpha(p[0]))
        return true;
    if (p.size() == 3 && p[1] == L':' && p[2] == L'\\' && iswalpha(p[0]))
        return true;
    return false;
}

static bool IsUncShareRootPath(const std::wstring &p)
{
    if (p.size() < 3 || p[0] != L'\\' || p[1] != L'\\')
        return false;
    size_t s1 = p.find(L'\\', 2);
    if (s1 == std::wstring::npos)
        return true; // \\server
    size_t s2 = p.find(L'\\', s1 + 1);
    if (s2 == std::wstring::npos)
        return true; // \\server\share
    return s2 + 1 == p.size(); // 形如 \\server\share 且仅末尾多一个分隔符
}

// 统一分隔符并去掉末尾分隔符，但保留 "C:\\" / "\\server\share" 这类根
static std::wstring TrimTrailingSeparators(std::wstring p)
{
    for (auto &c : p)
    {
        if (c == L'/')
            c = L'\\';
    }
    while (p.size() > 1 && p.back() == L'\\')
    {
        if (IsDriveRootPath(p) || IsUncShareRootPath(p))
            break;
        p.pop_back();
    }
    return p;
}

static std::wstring ParentFolderOf(const std::wstring &path)
{
    std::wstring p = TrimTrailingSeparators(path);
    if (p.empty())
        return p;
    if (IsDriveRootPath(p) || IsUncShareRootPath(p))
        return p;

    size_t pos = p.rfind(L'\\');
    if (pos == std::wstring::npos || pos == 0)
        return p;

    std::wstring parent = p.substr(0, pos);
    if (parent.size() == 2 && parent[1] == L':' && iswalpha(parent[0]))
        parent += L'\\'; // "Z:" -> "Z:\\"
    return parent;
}

static std::wstring LastComponentOf(const std::wstring &path)
{
    std::wstring p = TrimTrailingSeparators(path);
    size_t pos = p.rfind(L'\\');
    if (pos == std::wstring::npos)
        return p;
    return p.substr(pos + 1);
}

// 仅在无法访问路径（网络共享不可达/无权限）时使用的兜底判断
static bool LooksLikeFileName(const std::wstring &name)
{
    if (name.empty() || name == L"." || name == L"..")
        return false;
    size_t dot = name.rfind(L'.');
    return dot != std::wstring::npos && dot > 0 && dot + 1 < name.size();
}

static std::wstring DeriveFolderPath(const std::wstring &path)
{
    if (path.empty())
        return L"";

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if (attrs & FILE_ATTRIBUTE_DIRECTORY)
            return TrimTrailingSeparators(path);
        return ParentFolderOf(path);
    }

    if (LooksLikeFileName(LastComponentOf(path)))
        return ParentFolderOf(path);
    return TrimTrailingSeparators(path);
}

// 调用前需持有 g_cs
static void WriteHistoryFileLocked(const std::wstring &filePath)
{
    std::string output;
    for (size_t i = 0; i < g_historyCache.size(); ++i)
    {
        std::string utf8Path = WstrToUTF8(g_historyCache[i].path);
        if (utf8Path.empty())
            continue;
        std::string utf8Ts = g_historyCache[i].timestamp.empty() ? "" : WstrToUTF8(g_historyCache[i].timestamp);
        nlohmann::json j = {{"path", utf8Path}, {"timestamp", utf8Ts}};
        output += j.dump() + "\n";
    }

    std::wstring tmpPath = filePath + L".tmp";
    HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(hFile, output.data(), (DWORD)output.size(), &written, NULL);
        FlushFileBuffers(hFile);
        CloseHandle(hFile);
        if (!MoveFileExW(tmpPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING))
            DeleteFileW(tmpPath.c_str());
    }
}

// 一次写入多个文件夹（多选打开时用），只落盘一次
void WriteFoldersToHistory(const std::vector<std::wstring> &folders)
{
    if (folders.empty())
        return;

    std::wstring filePath = GetHistoryFilePath();
    if (filePath.empty())
        return;

    size_t pos = filePath.rfind(L'\\');
    if (pos != std::wstring::npos)
        SHCreateDirectoryExW(NULL, filePath.substr(0, pos).c_str(), NULL);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timestamp[32];
    swprintf_s(timestamp, L"%04d-%02d-%02dT%02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    EnterCriticalSection(&g_cs);

    if (!g_historyCacheValid)
    {
        LeaveCriticalSection(&g_cs);
        LoadHistoryPaths();
        EnterCriticalSection(&g_cs);
    }

    for (const auto &folder : folders)
    {
        if (folder.empty())
            continue;

        std::wstring normPath = NormalizePath(folder);
        g_historyCache.erase(std::remove_if(g_historyCache.begin(), g_historyCache.end(), [&](const HistoryEntry &e) {
            return NormalizePath(e.path) == normPath;
        }), g_historyCache.end());

        g_historyCache.insert(g_historyCache.begin(), HistoryEntry{folder, timestamp});
    }

    if (g_historyCache.size() > (size_t)MAX_HISTORY_ENTRIES)
        g_historyCache.resize(MAX_HISTORY_ENTRIES);

    WriteHistoryFileLocked(filePath);

    LeaveCriticalSection(&g_cs);
}

void WriteFolderToHistory(const std::wstring &folder)
{
    std::wstring f = TrimTrailingSeparators(folder);
    if (f.empty())
        return;
    WriteFoldersToHistory({f});
}

void WritePathToHistory(const std::wstring &path)
{
    std::wstring folderPath = DeriveFolderPath(path);
    if (folderPath.empty())
        return;
    WriteFoldersToHistory({folderPath});
}

// 多选打开：把每个选中项所在目录都记录下来。
// 同一批文件通常在同一目录，这里复用上一次的 stat 结果，避免逐个访问网络路径。
void WritePathsToHistory(const std::vector<std::wstring> &paths)
{
    std::vector<std::wstring> folders;
    folders.reserve(paths.size());

    std::wstring cachedParent;
    std::wstring cachedFolder;
    bool haveCached = false;

    for (const auto &p : paths)
    {
        if (p.empty())
            continue;

        std::wstring parent = ParentFolderOf(p);
        std::wstring folder;

        if (haveCached && cachedFolder == cachedParent && parent == cachedParent)
            folder = cachedParent;
        else
        {
            folder = DeriveFolderPath(p);
            cachedParent = parent;
            cachedFolder = folder;
            haveCached = true;
        }

        if (!folder.empty())
            folders.push_back(std::move(folder));
    }

    WriteFoldersToHistory(folders);
}

std::wstring GetExplorerPathsFilePath()
{
    std::wstring dir = GetDataDir();
    if (dir.empty())
        return L"";
    return dir + L"\\ExplorerPaths.jsonl";
}

std::vector<std::wstring> LoadExplorerPaths()
{
    std::vector<std::wstring> paths;
    std::wstring filePath = GetExplorerPathsFilePath();
    if (filePath.empty())
        return paths;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return paths;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE || fileSize > 1024 * 1024)
    {
        CloseHandle(hFile);
        return paths;
    }

    std::string content(fileSize, 0);
    DWORD read = 0;
    ReadFile(hFile, &content[0], fileSize, &read, NULL);
    CloseHandle(hFile);

    size_t pos = 0;
    while (pos < content.size())
    {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos)
            end = content.size();
        std::string line = content.substr(pos, end - pos);
        pos = end + 1;

        if (line.empty())
            continue;

        try
        {
            auto j = nlohmann::json::parse(line);
            if (j.contains("path") && j["path"].is_string())
            {
                std::wstring path = UTF8ToWstr(j["path"].get<std::string>());
                if (!path.empty())
                {
                    paths.push_back(path);
                    if (paths.size() >= (size_t)EXPLORER_MAX)
                        break;
                }
            }
        }
        catch (...)
        {
        }
    }

    return paths;
}

std::vector<std::wstring> LoadHistoryPaths()
{
    EnterCriticalSection(&g_cs);
    if (g_historyCacheValid)
    {
        std::vector<std::wstring> paths;
        paths.reserve(g_historyCache.size());
        for (const auto &e : g_historyCache)
            paths.push_back(e.path);
        LeaveCriticalSection(&g_cs);
        return paths;
    }
    LeaveCriticalSection(&g_cs);

    std::vector<HistoryEntry> entries;
    std::wstring filePath = GetHistoryFilePath();
    if (filePath.empty())
    {
        g_historyCache = entries;
        g_historyCacheValid = true;
        return {};
    }

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        g_historyCache = entries;
        g_historyCacheValid = true;
        return {};
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        g_historyCache = entries;
        g_historyCacheValid = true;
        return {};
    }

    if (fileSize > 1024 * 1024)
    {
        CloseHandle(hFile);
        g_historyCache = entries;
        g_historyCacheValid = true;
        return {};
    }

    std::string content(fileSize, 0);
    DWORD read = 0;
    ReadFile(hFile, &content[0], fileSize, &read, NULL);
    CloseHandle(hFile);

    size_t pos = 0;
    while (pos < content.size())
    {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos)
            end = content.size();
        std::string line = content.substr(pos, end - pos);
        pos = end + 1;

        if (line.empty())
            continue;

        try
        {
            auto j = nlohmann::json::parse(line);
            if (j.contains("path") && j["path"].is_string())
            {
                HistoryEntry entry;
                entry.path = UTF8ToWstr(j["path"].get<std::string>());
                if (!entry.path.empty())
                {
                    if (j.contains("timestamp") && j["timestamp"].is_string())
                        entry.timestamp = UTF8ToWstr(j["timestamp"].get<std::string>());
                    entries.push_back(std::move(entry));
                }
            }
        }
        catch (...)
        {
        }
    }

    std::unordered_set<std::wstring> seen;
    seen.reserve(entries.size());
    std::vector<HistoryEntry> deduped;
    deduped.reserve(entries.size());
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
    {
        std::wstring np = it->path;
        if (!np.empty() && np.back() == L'\\')
            np.pop_back();
        std::transform(np.begin(), np.end(), np.begin(), ::towlower);
        if (seen.insert(np).second)
            deduped.push_back(*it);
    }
    std::reverse(deduped.begin(), deduped.end());

    if (deduped.size() > (size_t)MAX_HISTORY_ENTRIES)
        deduped.resize(MAX_HISTORY_ENTRIES);

    EnterCriticalSection(&g_cs);
    g_historyCache = std::move(deduped);
    g_historyCacheValid = true;

    std::vector<std::wstring> paths;
    paths.reserve(g_historyCache.size());
    for (const auto &e : g_historyCache)
        paths.push_back(e.path);
    LeaveCriticalSection(&g_cs);
    return paths;
}

std::wstring GetBookmarkFilePath()
{
    std::wstring dir = GetDataDir();
    if (dir.empty())
        return L"";

    return dir + L"\\Favorites.jsonl";
}

static const wchar_t BM_MUTEX_NAME[] = L"Local\\PathHelper_Favorites_Mutex";

static HANDLE AcquireBMMutex(DWORD timeoutMs = 5000)
{
    HANDLE hMutex = CreateMutexW(NULL, FALSE, BM_MUTEX_NAME);
    if (!hMutex)
        return NULL;
    DWORD r = WaitForSingleObject(hMutex, timeoutMs);
    if (r != WAIT_OBJECT_0)
    {
        CloseHandle(hMutex);
        return NULL;
    }
    return hMutex;
}

static void ReleaseBMMutex(HANDLE hMutex)
{
    if (hMutex)
    {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
}

std::vector<BookmarkEntry> LoadBookmarks()
{
    std::vector<BookmarkEntry> result;
    std::wstring filePath = GetBookmarkFilePath();
    if (filePath.empty())
        return result;

    HANDLE hMutex = AcquireBMMutex();
    if (!hMutex)
        return result;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ReleaseBMMutex(hMutex);
        return result;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE || fileSize > 1024 * 1024)
    {
        CloseHandle(hFile);
        ReleaseBMMutex(hMutex);
        return result;
    }

    std::string content(fileSize, 0);
    DWORD read = 0;
    ReadFile(hFile, &content[0], fileSize, &read, NULL);
    CloseHandle(hFile);
    ReleaseBMMutex(hMutex);

    size_t pos = 0;
    while (pos < content.size())
    {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos)
            end = content.size();
        std::string line = content.substr(pos, end - pos);
        pos = end + 1;

        if (line.empty())
            continue;

        try
        {
            auto item = nlohmann::json::parse(line);
            if (!item.contains("path") || !item["path"].is_string())
                continue;

            BookmarkEntry entry;
            entry.path = UTF8ToWstr(item["path"].get<std::string>());
            if (entry.path.empty())
                continue;
            if (item.contains("note") && item["note"].is_string())
                entry.note = UTF8ToWstr(item["note"].get<std::string>());
            if (item.contains("timestamp") && item["timestamp"].is_string())
                entry.timestamp = UTF8ToWstr(item["timestamp"].get<std::string>());

            result.push_back(std::move(entry));
        }
        catch (...)
        {
        }
    }

    return result;
}

void SaveBookmarks(const std::vector<BookmarkEntry> &bookmarks)
{
    std::wstring filePath = GetBookmarkFilePath();
    if (filePath.empty())
        return;

    HANDLE hMutex = AcquireBMMutex();
    if (!hMutex)
        return;

    size_t pos = filePath.rfind(L'\\');
    if (pos != std::wstring::npos)
        SHCreateDirectoryExW(NULL, filePath.substr(0, pos).c_str(), NULL);

    std::string output;
    for (const auto &entry : bookmarks)
    {
        nlohmann::json item;
        std::string utf8Path = WstrToUTF8(entry.path);
        if (utf8Path.empty())
            continue;
        item["path"] = utf8Path;
        if (!entry.note.empty())
            item["note"] = WstrToUTF8(entry.note);
        if (!entry.timestamp.empty())
            item["timestamp"] = WstrToUTF8(entry.timestamp);

        output += item.dump() + "\n";
    }

    std::wstring tmpPath = filePath + L".tmp";
    HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(hFile, output.data(), (DWORD)output.size(), &written, NULL);
        FlushFileBuffers(hFile);
        CloseHandle(hFile);
        MoveFileExW(tmpPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    ReleaseBMMutex(hMutex);
}
