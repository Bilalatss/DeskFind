#include "shell.h"

#include <objbase.h>
#include <exdisp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shldisp.h>
#include <shobjidl.h>

#include "lang.h"

bool ShellOps::IsDesktopWindow(HWND hwnd)
{
    if (!hwnd) return false;
    wchar_t name[32] = {};
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    if (wcscmp(name, L"Progman") == 0) return true;
    // After Win+D (or with some wallpapers) the icons live in a top-level WorkerW instead.
    return wcscmp(name, L"WorkerW") == 0 && FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr;
}

bool ShellOps::Open(const std::wstring& path, const std::wstring& dir, std::wstring& error)
{
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpFile = path.c_str();
    info.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) return true;
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), 0,
                   reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    error = message ? message : Lang::Get().unknownError;
    LocalFree(message);
    while (!error.empty() && (error.back() == L'\n' || error.back() == L'\r' || error.back() == L'.')) error.pop_back();
    return false;
}

bool ShellOps::Reveal(const std::wstring& path, std::wstring& error)
{
    // Reuses an Explorer window already showing the folder when there is one.
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str());
    if (!item)
    {
        error = Lang::Get().itemNotFound;
        return false;
    }
    HRESULT hr = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    if (FAILED(hr))
    {
        error = Lang::Get().explorerFailed;
        return false;
    }
    return true;
}

namespace
{
// IShellWindows -> the desktop's IShellBrowser -> its active IShellView, which lives in Explorer.
bool DesktopView(ComPtr<IShellView>& view, long& hwnd)
{
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows)))) return false;
    VARIANT location{}, root{};
    location.vt = VT_I4;
    location.lVal = CSIDL_DESKTOP;
    root.vt = VT_EMPTY;
    ComPtr<IDispatch> dispatch;
    if (windows->FindWindowSW(&location, &root, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &dispatch) != S_OK || !dispatch) return false;
    ComPtr<IServiceProvider> provider;
    if (FAILED(dispatch->QueryInterface(IID_PPV_ARGS(&provider)))) return false;
    ComPtr<IShellBrowser> browser;
    if (FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) return false;
    return SUCCEEDED(browser->QueryActiveShellView(&view));
}
} // namespace

// The desktop's view, then select by parsing name.
bool ShellOps::SelectOnDesktop(const std::wstring& path)
{
    ComPtr<IShellView> view;
    long hwnd = 0;
    if (!DesktopView(view, hwnd)) return false;
    ComPtr<IFolderView> folderView;
    if (FAILED(view->QueryInterface(IID_PPV_ARGS(&folderView)))) return false;
    ComPtr<IShellFolder> folder;
    if (FAILED(folderView->GetFolder(IID_PPV_ARGS(&folder)))) return false;

    int count = 0;
    folderView->ItemCount(SVGIO_ALLVIEW, &count);
    for (int i = 0; i < count; i++)
    {
        PITEMID_CHILD child = nullptr;
        if (FAILED(folderView->Item(i, &child)) || !child) continue;
        STRRET name{};
        wchar_t parsing[MAX_PATH * 2] = {};
        bool match = SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_FORPARSING, &name)) &&
                     SUCCEEDED(StrRetToBufW(&name, child, parsing, ARRAYSIZE(parsing))) && EqualsNoCase(parsing, path);
        CoTaskMemFree(child);
        if (!match) continue;
        folderView->SelectItem(i, SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_ENSUREVISIBLE | SVSI_FOCUSED);
        SetForegroundWindow(reinterpret_cast<HWND>(LongToHandle(hwnd)));
        return true;
    }
    return false;
}

// Explorer's own IShellDispatch2::ShellExecute, so the new process is Explorer's child, not ours.
bool ShellOps::RunFromExplorer(const std::wstring& exe, const std::wstring& args, const std::wstring& dir)
{
    ComPtr<IShellView> view;
    long hwnd = 0;
    if (!DesktopView(view, hwnd)) return false;
    ComPtr<IDispatch> background;
    if (FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)))) return false;
    ComPtr<IShellFolderViewDual> folderView;
    if (FAILED(background->QueryInterface(IID_PPV_ARGS(&folderView)))) return false;
    ComPtr<IDispatch> application;
    if (FAILED(folderView->get_Application(&application))) return false;
    ComPtr<IShellDispatch2> shell;
    if (FAILED(application->QueryInterface(IID_PPV_ARGS(&shell)))) return false;
    BSTR file = SysAllocString(exe.c_str());
    VARIANT arguments{}, folder{}, operation{}, show{};
    arguments.vt = VT_BSTR;
    arguments.bstrVal = SysAllocString(args.c_str());
    folder.vt = VT_BSTR;
    folder.bstrVal = SysAllocString(dir.c_str());
    show.vt = VT_I4;
    show.lVal = SW_SHOWNORMAL;
    HRESULT hr = shell->ShellExecute(file, arguments, folder, operation, show);
    SysFreeString(file);
    VariantClear(&arguments);
    VariantClear(&folder);
    return SUCCEEDED(hr);
}

bool ShellOps::CopyText(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (memory)
    {
        if (void* locked = GlobalLock(memory))
        {
            memcpy(locked, text.c_str(), bytes);
            GlobalUnlock(memory);
            ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        }
        if (!ok) GlobalFree(memory);
    }
    CloseClipboard();
    return ok;
}

namespace
{
constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t RunValue[] = L"DeskFind";
constexpr wchar_t SettingsKey[] = L"Software\\DeskFind";

std::wstring Command()
{
    wchar_t exe[MAX_PATH * 2];
    DWORD length = GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    return L"\"" + std::wstring(exe, length) + L"\" --background"; // at sign-in DeskFind waits in the tray
}
} // namespace

bool Autostart::IsEnabled()
{
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, RunKey, RunValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
}

void Autostart::Set(bool enabled)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    if (enabled)
    {
        std::wstring command = Command();
        RegSetValueExW(key, RunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), DWORD((command.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(key, RunValue);
    }
    RegCloseKey(key);
}

bool Autostart::Initialize()
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SettingsKey, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    DWORD value = 0, size = sizeof(value);
    bool first = RegQueryValueExW(key, L"Initialized", nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS;
    if (first)
    {
        Set(true);
        value = 1;
        RegSetValueExW(key, L"Initialized", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }
    else if (IsEnabled())
    {
        Set(true);
    }
    RegCloseKey(key);
    return first;
}
