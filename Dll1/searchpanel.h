#pragma once

#include "framework.h"
#include <string>

// ---------------------------------------------------------------------------
// Everything 搜索结果浮层
//
// 悬浮在文件对话框搜索框正下方，展示 Everything 的匹配结果：
//   * 单击结果 → 打开
//   * 按住拖动 → CF_HDROP 拖出，可拖到资源管理器 / 其它程序 / 对话框本身
// ---------------------------------------------------------------------------

void RegisterSearchPanelClass();

HWND CreateSearchPanel(HWND hwndDialog, HWND hwndOwnerPanel);
void DestroySearchPanel(HWND hwndSearchPanel);
void SearchPanelSetQuery(HWND hwndSearchPanel, const std::wstring &query);
void SearchPanelReposition(HWND hwndSearchPanel);
void SearchPanelHide(HWND hwndSearchPanel);
