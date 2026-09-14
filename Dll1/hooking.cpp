#include "pch.h"
#include "hooking.h"
#include "history.h"
#include "settings.h"

using PFN_IFileDialog_Show = HRESULT(STDMETHODCALLTYPE *)(void *, HWND);

static PFN_IFileDialog_Show True_IFileDialog_Show = nullptr;

std::vector<VTableHookInfo> g_hookedVtables;

static HRESULT SafeQueryInterface(IUnknown *pUnk, REFIID riid, void **ppv)
{
    if (!IsValidComObject(pUnk))
        return E_POINTER;

    return pUnk->QueryInterface(riid, ppv);
}

HRESULT STDMETHODCALLTYPE Hook_IFileDialog_Show(void *This, HWND hwndParent)
{
    void **vtable = *(void ***)This;
    PFN_IFileDialog_Show trueShow = nullptr;
    for (const auto &info : g_hookedVtables)
    {
        if (info.vtable == vtable)
        {
            trueShow = (PFN_IFileDialog_Show)info.originalShow;
            break;
        }
    }
    if (!trueShow)
        trueShow = True_IFileDialog_Show;
    if (!trueShow)
        return E_UNEXPECTED;

    IFileDialog *pDialog = (IFileDialog *)This;
    if (g_settings.autoToLatest)
    {
        std::vector<std::wstring> paths = LoadHistoryPaths();
        if (!paths.empty())
        {
            bool needNavigate = true;
            IShellItem *pCurFolder = nullptr;
            if (SUCCEEDED(pDialog->GetFolder(&pCurFolder)) && pCurFolder)
            {
                LPWSTR curPath = nullptr;
                if (SUCCEEDED(pCurFolder->GetDisplayName(SIGDN_FILESYSPATH, &curPath)) && curPath)
                {
                    if (NormalizePath(curPath) == NormalizePath(paths[0]))
                        needNavigate = false;
                    CoTaskMemFree(curPath);
                }
                pCurFolder->Release();
            }
            if (needNavigate)
            {
                IShellItem *pItem = nullptr;
                HRESULT hrFolder = SHCreateItemFromParsingName(paths[0].c_str(), nullptr, IID_IShellItem, (void **)&pItem);
                if (SUCCEEDED(hrFolder) && pItem)
                {
                    pDialog->SetFolder(pItem);
                    pItem->Release();
                }
            }
        }
    }

    EnterCriticalSection(&g_cs);
    g_threadFileDialogs[GetCurrentThreadId()] = pDialog;
    LeaveCriticalSection(&g_cs);

    HRESULT hr = trueShow(This, hwndParent);

    EnterCriticalSection(&g_cs);
    g_threadFileDialogs.erase(GetCurrentThreadId());
    LeaveCriticalSection(&g_cs);
    if (SUCCEEDED(hr))
    {
        IShellItemArray *pItems = nullptr;
        IFileOpenDialog *pOpenDialog = nullptr;
        bool recordedFromItemArray = false;
        if (SUCCEEDED(pDialog->QueryInterface(IID_IFileOpenDialog, (void **)&pOpenDialog)) && pOpenDialog)
        {
            if (SUCCEEDED(pOpenDialog->GetSelectedItems(&pItems)) && pItems)
            {
                DWORD count = 0;
                if (SUCCEEDED(pItems->GetCount(&count)) && count > 0)
                {
                    // 多选时要记录每个选中项所在目录，而不是只取最后一个
                    std::vector<std::wstring> selectedPaths;
                    selectedPaths.reserve(count);
                    for (DWORD i = 0; i < count; ++i)
                    {
                        IShellItem *pItem = nullptr;
                        if (SUCCEEDED(pItems->GetItemAt(i, &pItem)) && pItem)
                        {
                            LPWSTR path = nullptr;
                            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                            {
                                selectedPaths.push_back(path);
                                CoTaskMemFree(path);
                            }
                            else
                            {
                                LPWSTR parsingPath = nullptr;
                                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsingPath)) && parsingPath)
                                {
                                    selectedPaths.push_back(parsingPath);
                                    CoTaskMemFree(parsingPath);
                                }
                            }
                            pItem->Release();
                        }
                    }
                    if (!selectedPaths.empty())
                    {
                        WritePathsToHistory(selectedPaths);
                        recordedFromItemArray = true;
                    }
                }
                pItems->Release();
            }
            pOpenDialog->Release();
        }
        if (!recordedFromItemArray)
        {
            IShellItem *pItem = nullptr;
            HRESULT hrResult = pDialog->GetResult(&pItem);
            if (SUCCEEDED(hrResult) && pItem)
            {
                LPWSTR path = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    WritePathToHistory(path);
                    CoTaskMemFree(path);
                    recordedFromItemArray = true;
                }
                else
                {
                    LPWSTR parsingPath = nullptr;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsingPath)) && parsingPath)
                    {
                        WritePathToHistory(parsingPath);
                        CoTaskMemFree(parsingPath);
                        recordedFromItemArray = true;
                    }
                    else
                    {
                        IShellItem *pParent = nullptr;
                        if (SUCCEEDED(pItem->GetParent(&pParent)) && pParent)
                        {
                            LPWSTR parentPath = nullptr;
                            if (SUCCEEDED(pParent->GetDisplayName(SIGDN_FILESYSPATH, &parentPath)) && parentPath)
                            {
                                WriteFolderToHistory(parentPath);
                                CoTaskMemFree(parentPath);
                                recordedFromItemArray = true;
                            }
                            pParent->Release();
                        }
                    }
                }
                pItem->Release();
            }
        }

        // 兜底：选中项无法解析成文件系统路径时（网络位置 / Shell 命名空间项很常见），
        // 至少把对话框当前所在的文件夹记录下来，避免“什么都没记住”。
        if (!recordedFromItemArray)
        {
            IShellItem *pFolder = nullptr;
            if (SUCCEEDED(pDialog->GetFolder(&pFolder)) && pFolder)
            {
                LPWSTR folderPath = nullptr;
                if (SUCCEEDED(pFolder->GetDisplayName(SIGDN_FILESYSPATH, &folderPath)) && folderPath)
                {
                    WriteFolderToHistory(folderPath);
                    CoTaskMemFree(folderPath);
                }
                else
                {
                    LPWSTR parsingPath = nullptr;
                    if (SUCCEEDED(pFolder->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsingPath)) && parsingPath)
                    {
                        WriteFolderToHistory(parsingPath);
                        CoTaskMemFree(parsingPath);
                    }
                }
                pFolder->Release();
            }
        }
    }
    return hr;
}

struct FileDialogCombo
{
    const CLSID *clsid;
    const IID *iid;
    const wchar_t *name;
};

void HookAllFileDialogVtables()
{
    const FileDialogCombo combos[] = {
        {&CLSID_FileOpenDialog, &IID_IFileDialog, L"FileOpenDialog:IFileDialog"},
        {&CLSID_FileOpenDialog, &IID_IFileOpenDialog, L"FileOpenDialog:IFileOpenDialog"},
        {&CLSID_FileSaveDialog, &IID_IFileDialog, L"FileSaveDialog:IFileDialog"},
        {&CLSID_FileSaveDialog, &IID_IFileSaveDialog, L"FileSaveDialog:IFileSaveDialog"},
    };

    for (auto &c : combos)
    {
        IUnknown *pUnk = nullptr;
        HRESULT hr = CoCreateInstance(*c.clsid, nullptr, CLSCTX_INPROC_SERVER, *c.iid, (void **)&pUnk);
        if (FAILED(hr))
            continue;

        void **vtable = *(void ***)pUnk;
        void **showPtr = &vtable[IFileDialog_Show_Index];

        bool alreadyHooked = false;
        for (auto &info : g_hookedVtables)
        {
            if (info.vtable == vtable)
            {
                alreadyHooked = true;
                break;
            }
        }
        if (alreadyHooked)
        {
            pUnk->Release();
            continue;
        }

        void *originalShow = *showPtr;
        if (!True_IFileDialog_Show)
            True_IFileDialog_Show = (PFN_IFileDialog_Show)originalShow;

        DWORD oldProtect;
        if (VirtualProtect(showPtr, sizeof(void *), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            *showPtr = reinterpret_cast<void *>(Hook_IFileDialog_Show);
            VirtualProtect(showPtr, sizeof(void *), oldProtect, &oldProtect);
            g_hookedVtables.push_back({vtable, originalShow});
        }
        pUnk->Release();
    }
}
