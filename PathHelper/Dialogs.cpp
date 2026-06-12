#include "framework.h"
#include <tlhelp32.h>
#include "Dialogs.h"
#include "ProcessManager.h"
#include "Resource.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>

std::wstring g_dlgSelectedProcess;
static HFONT g_hDlgFont = NULL;
static HFONT g_hDlgListFont = NULL;

static void CreateDialogFonts()
{
    g_hDlgFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_hDlgListFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

static void ApplyDialogFonts(HWND hDlg)
{
    SendMessageW(hDlg, WM_SETFONT, reinterpret_cast<WPARAM>(g_hDlgFont), TRUE);
    EnumChildWindows(hDlg, [](HWND child, LPARAM lParam) -> BOOL {
        if (GetWindowLongPtrW(child, GWLP_ID) == IDC_PROCESS_LIST)
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_hDlgListFont), TRUE);
        else
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_hDlgFont), TRUE);
        return TRUE;
    }, 0);
}

INT_PTR CALLBACK ProcessDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    static std::vector<std::wstring> s_allNames;

    switch (message)
    {
    case WM_INITDIALOG:
    {
        CreateDialogFonts();
        ApplyDialogFonts(hDlg);

        s_allNames.clear();

        HWND hList = GetDlgItem(hDlg, IDC_PROCESS_LIST);
        SendMessageW(hList, LB_SETITEMHEIGHT, 0, 20);

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE)
        {
            std::unordered_set<std::wstring> names;
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(PROCESSENTRY32W);
            if (Process32FirstW(hSnap, &pe))
            {
                do
                {
                    std::wstring name(pe.szExeFile);
                    size_t dot = name.rfind(L'.');
                    if (dot != std::wstring::npos)
                        name = name.substr(0, dot);

                    if (name.empty())
                        continue;
                    if (names.find(name) == names.end())
                    {
                        names.insert(name);
                        s_allNames.push_back(name);
                        SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }
        return static_cast<INT_PTR>(TRUE);

    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == IDC_PROCESS_SEARCH)
        {
            WCHAR filter[256];
            GetDlgItemTextW(hDlg, IDC_PROCESS_SEARCH, filter, 256);
            std::wstring filterStr(filter);
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::towlower);

            HWND hList = GetDlgItem(hDlg, IDC_PROCESS_LIST);
            SendMessageW(hList, LB_RESETCONTENT, 0, 0);

            for (const auto &name : s_allNames)
            {
                if (filterStr.empty())
                {
                    SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
                }
                else
                {
                    std::wstring lower = name;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                    if (lower.find(filterStr) != std::wstring::npos)
                        SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
                }
            }
            return static_cast<INT_PTR>(TRUE);
        }

        if (LOWORD(wParam) == IDOK)
        {
            HWND hList = GetDlgItem(hDlg, IDC_PROCESS_LIST);
            int idx = static_cast<int>(SendMessageW(hList, LB_GETCURSEL, 0, 0));
            if (idx != LB_ERR)
            {
                WCHAR buf[260];
                SendMessageW(hList, LB_GETTEXT, idx, reinterpret_cast<LPARAM>(buf));
                g_dlgSelectedProcess = buf;
            }
            EndDialog(hDlg, IDOK);
            return static_cast<INT_PTR>(TRUE);
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return static_cast<INT_PTR>(TRUE);
        }
        break;

    case WM_NCDESTROY:
        if (g_hDlgFont) { DeleteObject(g_hDlgFont); g_hDlgFont = NULL; }
        if (g_hDlgListFont) { DeleteObject(g_hDlgListFont); g_hDlgListFont = NULL; }
        s_allNames.clear();
        return static_cast<INT_PTR>(FALSE);
    }
    return static_cast<INT_PTR>(FALSE);
}


