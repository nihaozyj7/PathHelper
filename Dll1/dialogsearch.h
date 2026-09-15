#pragma once

#include "framework.h"

// ---------------------------------------------------------------------------
// 监听文件对话框自带的搜索框
//
// Windows 10/11 的通用文件对话框里，搜索框是 DirectUI 实现的
// （UniversalSearchBand -> Search Box -> SearchEditBoxWrapperClass ->
//   SearchEditBox），普通 Edit 消息拿不到内容，因此这里用 UI Automation
// 轮询读取它的 Value。
//
// 文本变化时向 notifyWnd 发送 WM_EVERYTHING_SEARCH_TEXT，
// lParam 是一个 new std::wstring*，接收方负责 delete。
// ---------------------------------------------------------------------------

void StartDialogSearchWatch(HWND hwndDialog, HWND notifyWnd);
void StopDialogSearchWatch(HWND hwndDialog);
void StopAllDialogSearchWatches();
