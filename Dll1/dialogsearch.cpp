#include "pch.h"
#include "dialogsearch.h"

#include <uiautomationclient.h>

#pragma comment(lib, "uiautomationcore.lib")

namespace
{

constexpr UINT POLL_INTERVAL_MS = 250;
// 连续这么多次找不到搜索框就放弃监听（约 60 秒）
constexpr int MAX_MISSING_SEARCHBOX_TICKS = 240;

struct WatcherContext
{
    HWND dialog = nullptr;
    HWND notify = nullptr;
    HANDLE stopEvent = nullptr;
};

struct WatcherEntry
{
    HANDLE thread = nullptr;
    HANDLE stopEvent = nullptr;
};

CRITICAL_SECTION g_watchCs;
bool g_watchCsReady = false;

void EnsureWatchCs()
{
    if (!g_watchCsReady)
    {
        InitializeCriticalSection(&g_watchCs);
        g_watchCsReady = true;
    }
}

std::map<HWND, WatcherEntry> g_watchers;

HWND FindDescendantByClass(HWND parent, const wchar_t *className)
{
    HWND found = FindWindowExW(parent, nullptr, className, nullptr);
    if (found)
        return found;

    HWND child = FindWindowExW(parent, nullptr, nullptr, nullptr);
    while (child)
    {
        found = FindDescendantByClass(child, className);
        if (found)
            return found;
        child = FindWindowExW(parent, child, nullptr, nullptr);
    }
    return nullptr;
}

// 在 wrapper 子树里找到真正承载文本的 UIA 元素
IUIAutomationElement *FindSearchEditElement(IUIAutomation *uia, IUIAutomationElement *root)
{
    IUIAutomationElement *edit = nullptr;

    // 1) 首选 AutomationId（与语言无关）
    VARIANT id;
    VariantInit(&id);
    id.vt = VT_BSTR;
    id.bstrVal = SysAllocString(L"SearchEditBox");

    IUIAutomationCondition *cond = nullptr;
    if (SUCCEEDED(uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &cond)) && cond)
    {
        root->FindFirst(TreeScope_Descendants, cond, &edit);
        cond->Release();
        cond = nullptr;
    }
    VariantClear(&id);

    // 2) 退而求其次：子树里第一个支持 ValuePattern 的 Edit
    if (!edit)
    {
        VARIANT ct;
        VariantInit(&ct);
        ct.vt = VT_I4;
        ct.lVal = UIA_EditControlTypeId;
        if (SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, ct, &cond)) && cond)
        {
            root->FindFirst(TreeScope_Descendants, cond, &edit);
            cond->Release();
        }
        VariantClear(&ct);
    }

    return edit;
}

std::wstring ReadSearchBoxText(IUIAutomation *uia, HWND dialog)
{
    HWND wrapper = FindDescendantByClass(dialog, L"SearchEditBoxWrapperClass");
    if (!wrapper)
        wrapper = FindDescendantByClass(dialog, L"Search Box");
    if (!wrapper)
        wrapper = FindDescendantByClass(dialog, L"UniversalSearchBand");
    if (!wrapper)
        return L"";

    IUIAutomationElement *root = nullptr;
    if (FAILED(uia->ElementFromHandle(wrapper, &root)) || !root)
        return L"";

    std::wstring text;
    IUIAutomationElement *edit = FindSearchEditElement(uia, root);
    if (edit)
    {
        IUIAutomationValuePattern *valuePattern = nullptr;
        if (SUCCEEDED(edit->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern))) && valuePattern)
        {
            BSTR value = nullptr;
            if (SUCCEEDED(valuePattern->get_CurrentValue(&value)) && value)
            {
                text = value;
                SysFreeString(value);
            }
            valuePattern->Release();
        }
        edit->Release();
    }

    root->Release();
    return text;
}

DWORD WINAPI WatchThreadProc(LPVOID param)
{
    WatcherContext *ctx = static_cast<WatcherContext *>(param);

    IUIAutomation *uia = nullptr;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comReady = SUCCEEDED(hr);

    if (comReady)
    {
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&uia))))
            uia = nullptr;
    }

    std::wstring lastText;
    bool first = true;
    int notFoundTicks = 0;

    while (IsWindow(ctx->dialog) && IsWindow(ctx->notify) &&
           WaitForSingleObject(ctx->stopEvent, 0) != WAIT_OBJECT_0)
    {
        std::wstring text;
        HWND wrapper = uia ? FindDescendantByClass(ctx->dialog, L"SearchEditBoxWrapperClass") : nullptr;
        if (wrapper)
        {
            notFoundTicks = 0;
            text = ReadSearchBoxText(uia, ctx->dialog);
        }
        else if (++notFoundTicks > MAX_MISSING_SEARCHBOX_TICKS)
        {
            // 这台机器上的对话框没有这个搜索框（或 UIA 不可用），没必要继续轮询
            break;
        }

        if (first || text != lastText)
        {
            lastText = text;
            first = false;

            std::wstring *payload = new std::wstring(text);
            if (!PostMessageW(ctx->notify, WM_EVERYTHING_SEARCH_TEXT, 0,
                              reinterpret_cast<LPARAM>(payload)))
                delete payload;
        }

        WaitForSingleObject(ctx->stopEvent, POLL_INTERVAL_MS);
    }

    if (uia)
        uia->Release();
    if (comReady)
        CoUninitialize();

    return 0;
}

} // namespace

void StartDialogSearchWatch(HWND hwndDialog, HWND notifyWnd)
{
    if (!hwndDialog || !notifyWnd)
        return;

    EnsureWatchCs();

    EnterCriticalSection(&g_watchCs);
    auto it = g_watchers.find(hwndDialog);
    if (it != g_watchers.end())
    {
        LeaveCriticalSection(&g_watchCs);
        return;
    }

    auto *ctx = new WatcherContext();
    ctx->dialog = hwndDialog;
    ctx->notify = notifyWnd;
    ctx->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    WatcherEntry entry;
    entry.stopEvent = ctx->stopEvent;
    entry.thread = CreateThread(nullptr, 0, WatchThreadProc, ctx, 0, nullptr);

    if (!entry.thread)
    {
        if (ctx->stopEvent)
            CloseHandle(ctx->stopEvent);
        delete ctx;
        LeaveCriticalSection(&g_watchCs);
        return;
    }

    g_watchers[hwndDialog] = entry;
    LeaveCriticalSection(&g_watchCs);
}

void StopDialogSearchWatch(HWND hwndDialog)
{
    if (!hwndDialog || !g_watchCsReady)
        return;

    HANDLE stopEvent = nullptr;
    HANDLE thread = nullptr;

    EnterCriticalSection(&g_watchCs);
    auto it = g_watchers.find(hwndDialog);
    if (it != g_watchers.end())
    {
        stopEvent = it->second.stopEvent;
        thread = it->second.thread;
        g_watchers.erase(it);
    }
    LeaveCriticalSection(&g_watchCs);

    if (stopEvent)
        SetEvent(stopEvent);
    if (thread)
    {
        WaitForSingleObject(thread, 3000);
        CloseHandle(thread);
    }
    if (stopEvent)
        CloseHandle(stopEvent);
}

void StopAllDialogSearchWatches()
{
    if (!g_watchCsReady)
        return;

    std::vector<HWND> dialogs;

    EnterCriticalSection(&g_watchCs);
    for (const auto &kv : g_watchers)
        dialogs.push_back(kv.first);
    LeaveCriticalSection(&g_watchCs);

    for (HWND h : dialogs)
        StopDialogSearchWatch(h);
}
