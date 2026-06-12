#include "framework.h"
#include <tlhelp32.h>
#include "Injector.h"

WCHAR g_szDllPath[260];

void ResolveDllPath(const WCHAR *exeDir)
{
    _snwprintf_s(g_szDllPath, 260, _TRUNCATE, L"%s\\Dll1.dll", exeDir);
    if (GetFileAttributesW(g_szDllPath) != INVALID_FILE_ATTRIBUTES)
        return;

    WCHAR parentDir[260];
    wcscpy_s(parentDir, exeDir);
    WCHAR *p = wcsrchr(parentDir, L'\\');
    if (p)
    {
        *p = L'\0';
        _snwprintf_s(g_szDllPath, 260, _TRUNCATE, L"%s\\Dll1.dll", parentDir);
        if (GetFileAttributesW(g_szDllPath) != INVALID_FILE_ATTRIBUTES)
            return;

        p = wcsrchr(parentDir, L'\\');
        if (p)
        {
            *p = L'\0';
            _snwprintf_s(g_szDllPath, 260, _TRUNCATE, L"%s\\Dll1.dll", parentDir);
            if (GetFileAttributesW(g_szDllPath) != INVALID_FILE_ATTRIBUTES)
                return;
        }
    }

    _snwprintf_s(g_szDllPath, 260, _TRUNCATE, L"%s\\Dll1.dll", exeDir);
}

BOOL InjectDll(DWORD pid)
{
    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess)
        return FALSE;

    size_t dllPathLen = (wcslen(g_szDllPath) + 1) * sizeof(WCHAR);
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, NULL, dllPathLen, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteMem)
    {
        CloseHandle(hProcess);
        return FALSE;
    }

    if (!WriteProcessMemory(hProcess, pRemoteMem, g_szDllPath, dllPathLen, NULL))
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
