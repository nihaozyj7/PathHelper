#pragma once

#include "framework.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Everything (voidtools) 集成
//
// 使用 Everything 的 IPC 协议（WM_COPYDATA + EVERYTHING_TASKBAR_NOTIFICATION
// 窗口），不依赖 Everything SDK 的 Everything64.dll，因此用户只需要装了
// Everything 本体即可。
// ---------------------------------------------------------------------------

struct EverythingResult
{
    std::wstring fullPath;
    bool isFolder = false;
};

// 本机是否安装了 Everything（正在运行，或能找到 Everything.exe）
bool EverythingIsInstalled();

// Everything 是否正在运行（会缓存窗口句柄）
bool EverythingIsRunning();

// 保证 Everything 可用；未运行时尝试启动（带节流），返回最终是否可用
bool EverythingEnsureAvailable();

// 发起一次异步查询。结果会以 WM_COPYDATA 回传到 replyWnd，
// 由 EverythingHandleReply() 解析。
bool EverythingQueryAsync(HWND replyWnd, const std::wstring &query, DWORD maxResults);

// 判断一条 WM_COPYDATA 是否是 Everything 的查询结果；是则解析到 out
bool EverythingHandleReply(COPYDATASTRUCT *cds, std::vector<EverythingResult> &out);
