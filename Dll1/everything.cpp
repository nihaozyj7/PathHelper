#include "pch.h"
#include "everything.h"

#include <tlhelp32.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace
{

// Everything IPC 常量（与 voidtools SDK 的 everything_ipc.h 保持一致）
constexpr wchar_t EV_WNDCLASS[] = L"EVERYTHING_TASKBAR_NOTIFICATION";
constexpr DWORD EV_COPYDATA_QUERYW = 2;      // EVERYTHING_IPC_COPYDATAQUERYW
constexpr DWORD EV_ITEM_FOLDER = 0x00000001; // EVERYTHING_IPC_FOLDER
constexpr DWORD EV_REPLY_MAGIC = 0x50484556; // 'PHEV' —— 我们自己约定的 reply dwData

#pragma pack(push, 1)
struct EvQueryW
{
    DWORD replyHwnd;
    DWORD replyMessage;
    DWORD searchFlags;
    DWORD offset;
    DWORD maxResults;
    wchar_t searchString[1];
};

struct EvItemW
{
    DWORD flags;
    DWORD filenameOffset;
    DWORD pathOffset;
};

struct EvListW
{
    DWORD totFolders;
    DWORD totFiles;
    DWORD totItems;
    DWORD numFolders;
    DWORD numFiles;
    DWORD numItems;
    DWORD offset;
    EvItemW items[1];
};
#pragma pack(pop)

HWND g_hwndEverything = nullptr;
DWORD g_lastStartAttempt = 0;
int g_installedCache = -1; // -1 未知, 0 否, 1 是

HWND FindEverythingWindow()
{
    if (g_hwndEverything && IsWindow(g_hwndEverything))
        return g_hwndEverything;

    g_hwndEverything = FindWindowW(EV_WNDCLASS, nullptr);
    return g_hwndEverything;
}

std::wstring ReadRegString(HKEY root, const wchar_t *subKey, const wchar_t *valueName)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return L"";

    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    LSTATUS st = RegQueryValueExW(hKey, valueName, nullptr, &type,
                                  reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(hKey);
    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return L"";
    buf[MAX_PATH * 2 - 1] = L'\0';
    return buf;
}

// 从 "C:\\Program Files\\Everything\\Everything.exe,0" 这类 DisplayIcon 里取出路径
std::wstring StripIconSuffix(std::wstring value)
{
    size_t comma = value.find(L',');
    if (comma != std::wstring::npos)
        value.resize(comma);
    if (!value.empty() && value.front() == L'"' && value.back() == L'"')
        value = value.substr(1, value.size() - 2);
    return value;
}

bool FileExists(const std::wstring &path)
{
    if (path.empty())
        return false;
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring JoinPath(const std::wstring &dir, const wchar_t *name)
{
    if (dir.empty())
        return L"";
    std::wstring result = dir;
    if (result.back() != L'\\')
        result += L'\\';
    result += name;
    return result;
}

std::wstring FindEverythingExeInProcesses()
{
    std::wstring result;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, L"Everything.exe") != 0)
                continue;

            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!hProc)
                continue;

            wchar_t path[MAX_PATH * 2] = {};
            DWORD size = ARRAYSIZE(path);
            if (QueryFullProcessImageNameW(hProc, 0, path, &size) && size > 0)
                result = path;
            CloseHandle(hProc);
            if (!result.empty())
                break;
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return result;
}

// 通过注册表 + 常见安装位置查找 Everything.exe
std::wstring FindEverythingExeByInstall()
{
    static const HKEY roots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    static const wchar_t *uninstallKeys[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Everything",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Everything",
        L"SOFTWARE\\voidtools\\Everything",
    };

    for (HKEY root : roots)
    {
        for (const wchar_t *subKey : uninstallKeys)
        {
            std::wstring loc = ReadRegString(root, subKey, L"InstallLocation");
            if (!loc.empty())
            {
                std::wstring exe = JoinPath(loc, L"Everything.exe");
                if (FileExists(exe))
                    return exe;
            }
            std::wstring icon = ReadRegString(root, subKey, L"DisplayIcon");
            if (!icon.empty())
            {
                std::wstring exe = StripIconSuffix(icon);
                if (FileExists(exe))
                    return exe;
            }
        }
    }

    // App Paths
    for (HKEY root : roots)
    {
        std::wstring exe = ReadRegString(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\Everything.exe", L"");
        if (FileExists(exe))
            return exe;
    }

    // 常见安装目录
    static const wchar_t *envDirs[] = {L"ProgramFiles", L"ProgramFiles(x86)", L"LOCALAPPDATA", L"APPDATA"};
    const wchar_t *subDirs[] = {L"Everything", L"Programs\\Everything"};

    for (const wchar_t *env : envDirs)
    {
        wchar_t base[MAX_PATH] = {};
        DWORD len = GetEnvironmentVariableW(env, base, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            continue;
        for (const wchar_t *sub : subDirs)
        {
            std::wstring exe = JoinPath(std::wstring(base) + L"\\" + sub, L"Everything.exe");
            if (FileExists(exe))
                return exe;
        }
    }

    return L"";
}

std::wstring FindEverythingExe()
{
    std::wstring exe = FindEverythingExeInProcesses();
    if (!exe.empty())
        return exe;
    return FindEverythingExeByInstall();
}

// 从 list 缓冲区里安全取一个以 NUL 结尾的宽字符串
const wchar_t *SafeWString(const unsigned char *base, DWORD cbData, DWORD offset)
{
    if (offset >= cbData)
        return nullptr;
    const wchar_t *str = reinterpret_cast<const wchar_t *>(base + offset);
    // 剩余空间必须能容纳至少一个字符，并且能找到终止符
    size_t remain = (cbData - offset) / sizeof(wchar_t);
    for (size_t i = 0; i < remain; ++i)
    {
        if (str[i] == L'\0')
            return str;
    }
    return nullptr;
}

} // namespace

bool EverythingIsRunning()
{
    return FindEverythingWindow() != nullptr;
}

bool EverythingIsInstalled()
{
    if (EverythingIsRunning())
    {
        g_installedCache = 1;
        return true;
    }
    if (g_installedCache == 1)
        return true;
    bool found = !FindEverythingExe().empty();
    g_installedCache = found ? 1 : 0;
    return found;
}

bool EverythingEnsureAvailable()
{
    if (FindEverythingWindow())
        return true;

    DWORD now = GetTickCount();
    if (g_lastStartAttempt != 0 && (DWORD)(now - g_lastStartAttempt) < 15000)
        return false;
    g_lastStartAttempt = now;

    std::wstring exe = FindEverythingExe();
    if (exe.empty())
        return false;

    HINSTANCE hi = ShellExecuteW(nullptr, L"open", exe.c_str(), L"-startup", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(hi) <= 32)
        return false;

    // 最多等待约 1.2 秒，避免长时间卡住调用线程
    for (int i = 0; i < 12; ++i)
    {
        Sleep(100);
        if (FindEverythingWindow())
            return true;
    }
    return false;
}

bool EverythingQueryAsync(HWND replyWnd, const std::wstring &query, DWORD maxResults)
{
    if (!replyWnd || !IsWindow(replyWnd) || query.empty())
        return false;

    if (!EverythingEnsureAvailable())
        return false;

    HWND hEv = FindEverythingWindow();
    if (!hEv)
        return false;

    const size_t queryBytes = (query.size() + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(EvQueryW) - sizeof(wchar_t) + queryBytes, 0);

    EvQueryW *q = reinterpret_cast<EvQueryW *>(buffer.data());
    q->replyHwnd = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(replyWnd));
    q->replyMessage = EV_REPLY_MAGIC;
    q->searchFlags = 0;
    q->offset = 0;
    q->maxResults = (maxResults == 0) ? 64 : maxResults;
    memcpy(q->searchString, query.c_str(), queryBytes);

    COPYDATASTRUCT cds = {};
    cds.dwData = EV_COPYDATA_QUERYW;
    cds.cbData = static_cast<DWORD>(buffer.size());
    cds.lpData = buffer.data();

    DWORD_PTR result = 0;
    LRESULT ok = SendMessageTimeoutW(hEv, WM_COPYDATA,
                                     reinterpret_cast<WPARAM>(replyWnd),
                                     reinterpret_cast<LPARAM>(&cds),
                                     SMTO_ABORTIFHUNG | SMTO_NORMAL, 2000, &result);
    if (ok == 0)
    {
        // 窗口可能在查询过程中被销毁/重建
        g_hwndEverything = nullptr;
        return false;
    }
    return result != 0;
}

bool EverythingHandleReply(COPYDATASTRUCT *cds, std::vector<EverythingResult> &out)
{
    if (!cds || cds->dwData != EV_REPLY_MAGIC || !cds->lpData || cds->cbData < sizeof(EvListW))
        return false;

    const unsigned char *base = reinterpret_cast<const unsigned char *>(cds->lpData);
    const EvListW *list = reinterpret_cast<const EvListW *>(base);

    size_t maxBySize = (cds->cbData - offsetof(EvListW, items)) / sizeof(EvItemW);
    DWORD count = list->numItems;
    if (count > maxBySize)
        count = static_cast<DWORD>(maxBySize);

    out.clear();
    out.reserve(count);

    for (DWORD i = 0; i < count; ++i)
    {
        const wchar_t *path = SafeWString(base, cds->cbData, list->items[i].pathOffset);
        const wchar_t *name = SafeWString(base, cds->cbData, list->items[i].filenameOffset);
        if (!path && !name)
            continue;

        std::wstring fullPath = path ? path : L"";
        if (name && name[0] != L'\0')
        {
            if (!fullPath.empty() && fullPath.back() != L'\\')
                fullPath += L'\\';
            fullPath += name;
        }
        if (fullPath.empty())
            continue;

        EverythingResult r;
        r.fullPath = std::move(fullPath);
        r.isFolder = (list->items[i].flags & EV_ITEM_FOLDER) != 0;
        out.push_back(std::move(r));
    }

    return true;
}
