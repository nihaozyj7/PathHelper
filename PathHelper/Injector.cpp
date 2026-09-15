#include "framework.h"
#include <tlhelp32.h>
#include "Injector.h"

WCHAR g_szDllPath[260];
WCHAR g_szFavDllPath[260];

// 依次在 exeDir、上一级、上上级目录里查找 dllName
static void ResolveDllInDirs(const WCHAR *exeDir, const WCHAR *dllName, WCHAR *outPath)
{
    _snwprintf_s(outPath, 260, _TRUNCATE, L"%s\\%s", exeDir, dllName);
    if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES)
        return;

    WCHAR parentDir[260];
    wcscpy_s(parentDir, exeDir);
    WCHAR *p = wcsrchr(parentDir, L'\\');
    if (p)
    {
        *p = L'\0';
        _snwprintf_s(outPath, 260, _TRUNCATE, L"%s\\%s", parentDir, dllName);
        if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES)
            return;

        p = wcsrchr(parentDir, L'\\');
        if (p)
        {
            *p = L'\0';
            _snwprintf_s(outPath, 260, _TRUNCATE, L"%s\\%s", parentDir, dllName);
            if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES)
                return;
        }
    }

    _snwprintf_s(outPath, 260, _TRUNCATE, L"%s\\%s", exeDir, dllName);
}

void ResolveDllPath(const WCHAR *exeDir)
{
    ResolveDllInDirs(exeDir, L"Dll1.dll", g_szDllPath);
    ResolveDllInDirs(exeDir, L"Dll2.dll", g_szFavDllPath);
}

static BOOL InjectDllTo(DWORD pid, const WCHAR *dllPath)
{
    if (!dllPath || dllPath[0] == L'\0')
        return FALSE;

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess)
        return FALSE;

    size_t dllPathLen = (wcslen(dllPath) + 1) * sizeof(WCHAR);
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, NULL, dllPathLen, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteMem)
    {
        CloseHandle(hProcess);
        return FALSE;
    }

    if (!WriteProcessMemory(hProcess, pRemoteMem, dllPath, dllPathLen, NULL))
    {
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }

    LPTHREAD_START_ROUTINE pLoadLibrary =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLibrary, pRemoteMem, 0, NULL);
    if (!hThread)
    {
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }

    if (WaitForSingleObject(hThread, 5000) == WAIT_TIMEOUT)
    {
        TerminateThread(hThread, 0);
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return TRUE;
}

BOOL InjectDll(DWORD pid)
{
    return InjectDllTo(pid, g_szDllPath);
}

BOOL InjectFavoritesDll(DWORD pid)
{
    return InjectDllTo(pid, g_szFavDllPath);
}
