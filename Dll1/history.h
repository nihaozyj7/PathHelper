#pragma once
#include <string>
#include <vector>

struct BookmarkEntry
{
    std::wstring path;
    std::wstring note;
    std::wstring timestamp;
};

std::vector<std::wstring> LoadHistoryPaths();
// 记录“选中项”（可能是文件也可能是文件夹）所在的文件夹
void WritePathToHistory(const std::wstring &path);
// 记录一批选中项所在的所有文件夹（多选打开时使用）
void WritePathsToHistory(const std::vector<std::wstring> &paths);
// 已知参数就是文件夹本身时使用，避免因网络路径无法 stat 而被多截一层
void WriteFolderToHistory(const std::wstring &folder);

std::wstring GetBookmarkFilePath();
std::vector<BookmarkEntry> LoadBookmarks();
void SaveBookmarks(const std::vector<BookmarkEntry> &bookmarks);

std::wstring GetExplorerPathsFilePath();
std::vector<std::wstring> LoadExplorerPaths();
