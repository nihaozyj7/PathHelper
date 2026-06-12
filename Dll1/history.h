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
void WritePathToHistory(const std::wstring &path);

std::wstring GetBookmarkFilePath();
std::vector<BookmarkEntry> LoadBookmarks();
void SaveBookmarks(const std::vector<BookmarkEntry> &bookmarks);

std::wstring GetExplorerPathsFilePath();
std::vector<std::wstring> LoadExplorerPaths();
