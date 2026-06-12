#include "framework.h"
#include <ole2.h>
#include <exdisp.h>
#include "ExplorerMonitor.h"
#include "ProcessManager.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")

struct ExplorerEntry {
    std::wstring path;
    FILETIME activationTime;
};

static std::map<HWND, FILETIME> g_explorerActivationTimes;
static bool g_comInitialized = false;
static HWINEVENTHOOK g_hookActivate = NULL;
static HWINEVENTHOOK g_hookObjectDestroy = NULL;
static HWINEVENTHOOK g_hookObjectNameChange = NULL;
static std::string g_lastWrittenContent;
static volatile LONG g_explorerDirty = 1;

static FILETIME GetCurrentFileTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ft;
}

static ULONGLONG FileTimeToUll(const FILETIME &ft) {
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static std::string WideToUtf8(const std::wstring &wide) {
    if (wide.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &result[0], len, NULL, NULL);
    return result;
}

static std::string EscapeJson(const std::string &s) {
    std::string result;
    result.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        switch (c) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n";  break;
        case '\r': result += "\\r";  break;
        case '\t': result += "\\t";  break;
        default:   result += c;       break;
        }
    }
    return result;
}

static std::wstring UrlToPath(const std::wstring &url) {
    if (url.size() >= 8 && url.compare(0, 8, L"file:///") == 0) {
        std::wstring rest = url.substr(8);
        std::wstring decoded;
        for (size_t i = 0; i < rest.size(); i++) {
            if (rest[i] == L'%' && i + 2 < rest.size()) {
                WCHAR hex[3] = {rest[i + 1], rest[i + 2], 0};
                decoded += static_cast<WCHAR>(wcstol(hex, NULL, 16));
                i += 2;
            } else if (rest[i] == L'/') {
                decoded += L'\\';
            } else {
                decoded += rest[i];
            }
        }
        if (decoded.size() > 3 && decoded.back() == L'\\')
            decoded.pop_back();
        bool isLocalPath = decoded.size() >= 3 &&
            ((decoded[0] >= L'A' && decoded[0] <= L'Z') || (decoded[0] >= L'a' && decoded[0] <= L'z')) &&
            decoded[1] == L':' && decoded[2] == L'\\';
        if (!isLocalPath)
            return L"\\\\" + decoded;
        return decoded;
    }
    if (url.size() >= 7 && url.compare(0, 7, L"file://") == 0) {
        std::wstring rest = url.substr(7);
        std::wstring decoded;
        for (size_t i = 0; i < rest.size(); i++) {
            if (rest[i] == L'%' && i + 2 < rest.size()) {
                WCHAR hex[3] = {rest[i + 1], rest[i + 2], 0};
                decoded += static_cast<WCHAR>(wcstol(hex, NULL, 16));
                i += 2;
            } else if (rest[i] == L'/') {
                decoded += L'\\';
            } else {
                decoded += rest[i];
            }
        }
        return L"\\\\" + decoded;
    }
    if (url.find(L"://") != std::wstring::npos)
        return url;

    return L"";
}

static bool IsExplorerWindow(HWND hwnd) {
    WCHAR className[64];
    GetClassNameW(hwnd, className, 64);
    return (_wcsicmp(className, L"CabinetWClass") == 0 ||
            _wcsicmp(className, L"ExploreWClass") == 0);
}

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild, DWORD dwEventThread,
                           DWORD dwmsEventTime) {
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || hwnd == NULL)
        return;

    if (event == EVENT_SYSTEM_FOREGROUND) {
        if (IsExplorerWindow(hwnd)) {
            g_explorerActivationTimes[hwnd] = GetCurrentFileTime();
            InterlockedExchange(&g_explorerDirty, 1);
        }
    } else if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_NAMECHANGE) {
        if (IsExplorerWindow(hwnd))
            InterlockedExchange(&g_explorerDirty, 1);
    }
}

void InitExplorerMonitor() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == S_OK)
        g_comInitialized = true;

    g_hookActivate = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                     NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    g_hookObjectDestroy = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
                                           NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    g_hookObjectNameChange = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
                                              NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
}

void UpdateExplorerPaths() {
    if (!g_comInitialized)
        return;

    if (InterlockedCompareExchange(&g_explorerDirty, 0, 1) != 1)
        return;

    IShellWindows *pShellWindows = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER,
                                  IID_IShellWindows, (void **)&pShellWindows);
    if (FAILED(hr) || !pShellWindows)
        return;

    std::vector<ExplorerEntry> entries;
    std::set<HWND> currentWindows;

    long count = 0;
    pShellWindows->get_Count(&count);

    for (long i = 0; i < count; i++) {
        VARIANT vIndex;
        VariantInit(&vIndex);
        vIndex.vt = VT_I4;
        vIndex.lVal = i;

        IDispatch *pDispatch = NULL;
        hr = pShellWindows->Item(vIndex, &pDispatch);
        VariantClear(&vIndex);

        if (FAILED(hr) || !pDispatch)
            continue;

        IWebBrowser2 *pBrowser = NULL;
        hr = pDispatch->QueryInterface(IID_IWebBrowser2, (void **)&pBrowser);
        pDispatch->Release();

        if (FAILED(hr) || !pBrowser)
            continue;

        SHANDLE_PTR hwndPtr = 0;
        pBrowser->get_HWND(&hwndPtr);
        HWND browserHwnd = reinterpret_cast<HWND>(hwndPtr);

        BSTR locationUrl = NULL;
        hr = pBrowser->get_LocationURL(&locationUrl);

        std::wstring path;
        if (SUCCEEDED(hr) && locationUrl) {
            path = UrlToPath(locationUrl);
            SysFreeString(locationUrl);
        }

        pBrowser->Release();

        if (path.empty() || browserHwnd == NULL)
            continue;

        if (!IsExplorerWindow(browserHwnd))
            continue;

        currentWindows.insert(browserHwnd);

        if (g_explorerActivationTimes.find(browserHwnd) == g_explorerActivationTimes.end())
            g_explorerActivationTimes[browserHwnd] = GetCurrentFileTime();

        ExplorerEntry entry;
        entry.path = path;
        entry.activationTime = g_explorerActivationTimes[browserHwnd];
        entries.push_back(entry);
    }

    pShellWindows->Release();

    for (auto it = g_explorerActivationTimes.begin(); it != g_explorerActivationTimes.end();) {
        if (!IsWindow(it->first))
            it = g_explorerActivationTimes.erase(it);
        else
            ++it;
    }

    std::sort(entries.begin(), entries.end(), [](const ExplorerEntry &a, const ExplorerEntry &b) {
        return FileTimeToUll(a.activationTime) > FileTimeToUll(b.activationTime);
    });

    std::string content;
    for (const auto &entry : entries) {
        std::string utf8Path = WideToUtf8(entry.path);
        std::string escapedPath = EscapeJson(utf8Path);

        SYSTEMTIME st;
        FileTimeToSystemTime(&entry.activationTime, &st);

        char line[8192];
        int len = sprintf_s(line, sizeof(line),
                           "{\"path\":\"%s\",\"time\":\"%04d-%02d-%02dT%02d:%02d:%02d\"}\n",
                           escapedPath.c_str(),
                           st.wYear, st.wMonth, st.wDay,
                           st.wHour, st.wMinute, st.wSecond);

        if (len > 0)
            content.append(line, len);
    }

    if (content == g_lastWrittenContent)
        return;

    g_lastWrittenContent = content;

    WCHAR jsonlPath[MAX_PATH];
    _snwprintf_s(jsonlPath, MAX_PATH, _TRUNCATE, L"%s\\ExplorerPaths.jsonl", g_szDataDir);

    HANDLE hFile = CreateFileW(jsonlPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, content.c_str(), (DWORD)content.size(), &written, NULL);
        CloseHandle(hFile);
    }
}

void CleanupExplorerMonitor() {
    if (g_hookActivate) {
        UnhookWinEvent(g_hookActivate);
        g_hookActivate = NULL;
    }
    if (g_hookObjectDestroy) {
        UnhookWinEvent(g_hookObjectDestroy);
        g_hookObjectDestroy = NULL;
    }
    if (g_hookObjectNameChange) {
        UnhookWinEvent(g_hookObjectNameChange);
        g_hookObjectNameChange = NULL;
    }
    if (g_comInitialized) {
        CoUninitialize();
        g_comInitialized = false;
    }
}