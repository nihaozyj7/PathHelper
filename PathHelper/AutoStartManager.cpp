#include "framework.h"
#include "AutoStartManager.h"

#define AUTOSTART_TASK_NAME L"PathHelper_AutoStart"

static bool RunSchtasks(const WCHAR *args)
{
    WCHAR schtasks[MAX_PATH];
    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\schtasks.exe", schtasks, MAX_PATH);

    WCHAR cmdLine[3 * MAX_PATH + 256];
    _snwprintf_s(cmdLine, _TRUNCATE, L"schtasks.exe %s", args);

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(schtasks, cmdLine, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return false;

    WaitForSingleObject(pi.hProcess, 10000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0;
}

bool EnableAutoStartToTray()
{
    WCHAR exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH))
        return false;

    WCHAR args[2 * MAX_PATH + 256];
    _snwprintf_s(args, _TRUNCATE,
                 L"/create /tn \"%s\" /tr \"\\\"%s\\\" --autostart\" /sc onlogon /rl highest /f",
                 AUTOSTART_TASK_NAME, exePath);

    return RunSchtasks(args);
}

bool DisableAutoStartToTray()
{
    WCHAR args[256];
    _snwprintf_s(args, _TRUNCATE,
                 L"/delete /tn \"%s\" /f",
                 AUTOSTART_TASK_NAME);

    return RunSchtasks(args);
}