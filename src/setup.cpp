#include "setup.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "lang.h"
#include "shell.h"
#include "version.h"

namespace
{
constexpr wchar_t UninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DeskFind";

std::wstring InstallDir() { return KnownFolder(FOLDERID_UserProgramFiles) + L"\\DeskFind"; }
std::wstring InstalledExe() { return InstallDir() + L"\\DeskFind.exe"; }
std::wstring ShortcutPath() { return KnownFolder(FOLDERID_Programs) + L"\\DeskFind.lnk"; }

// The Start menu entry starts DeskFind again after Exit; while it runs, starting it opens the search box.
void CreateShortcut(const std::wstring& exe, const std::wstring& dir)
{
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return;
    link->SetPath(exe.c_str());
    link->SetWorkingDirectory(dir.c_str());
    ComPtr<IPersistFile> file;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) file->Save(ShortcutPath().c_str(), TRUE);
}

std::wstring SelfPath()
{
    wchar_t path[MAX_PATH * 2];
    DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    return std::wstring(path, length);
}

void SetString(HKEY key, const wchar_t* name, const std::wstring& value)
{
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
}

void SetDword(HKEY key, const wchar_t* name, DWORD value) { RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)); }

// The entry Settings > Apps lists; winget reads the same one.
void Register(const std::wstring& exe)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, UninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    WIN32_FILE_ATTRIBUTE_DATA file{};
    GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &file);
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t date[16];
    swprintf_s(date, L"%04d%02d%02d", now.wYear, now.wMonth, now.wDay);
    std::wstring quoted = L"\"" + exe + L"\"";
    SetString(key, L"DisplayName", L"DeskFind");
    SetString(key, L"DisplayVersion", L"" DESKFIND_VERSION);
    SetString(key, L"Publisher", L"" DESKFIND_PUBLISHER);
    SetString(key, L"DisplayIcon", exe + L",0");
    SetString(key, L"InstallLocation", InstallDir());
    SetString(key, L"InstallDate", date);
    SetString(key, L"URLInfoAbout", L"" DESKFIND_URL);
    SetString(key, L"UninstallString", quoted + L" --uninstall");
    SetString(key, L"QuietUninstallString", quoted + L" --uninstall --quiet");
    SetDword(key, L"NoModify", 1);
    SetDword(key, L"NoRepair", 1);
    SetDword(key, L"EstimatedSize", (file.nFileSizeLow + 1023) / 1024);
    RegCloseKey(key);
}

// Asks a running DeskFind (maybe the older copy being replaced) to quit, and waits for it.
bool StopRunning()
{
    HWND window = FindWindowW(MainWindowClass, nullptr);
    if (!window) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
    PostMessageW(window, WM_APP_QUIT, 0, 0);
    bool stopped;
    if (process)
    {
        stopped = WaitForSingleObject(process, 5000) == WAIT_OBJECT_0;
        if (!stopped && TerminateProcess(process, 0)) stopped = WaitForSingleObject(process, 2000) == WAIT_OBJECT_0;
        CloseHandle(process);
    }
    else
    {
        for (int i = 0; i < 50 && IsWindow(window); i++) Sleep(100);
        stopped = !IsWindow(window);
    }
    return stopped;
}

void DeleteFolder(const std::wstring& dir)
{
    std::wstring from = dir;
    from.push_back(L'\0'); // the list ends with two nulls
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}
} // namespace

int Setup::Install()
{
    std::wstring self = SelfPath(), dir = InstallDir(), exe = InstalledExe();
    if (!StopRunning()) return 1;
    if (!EqualsNoCase(self, exe))
    {
        SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        bool copied = false;
        // The old exe can stay locked for a moment after its process has ended.
        for (int attempt = 0; attempt < 30 && !copied; attempt++)
        {
            copied = CopyFileW(self.c_str(), exe.c_str(), FALSE) != FALSE;
            if (!copied) Sleep(100);
        }
        if (!copied) return 2;
    }
    Register(exe);
    CreateShortcut(exe, dir);

    // Started by Explorer: outside any job the installer runs in (PowerShell's Start-Process -Wait waits for the whole
    // process tree) and never elevated, even from an admin prompt. Autostart and the welcome are the new copy's business.
    if (ShellOps::RunFromExplorer(exe, L"--background", dir)) return 0;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + exe + L"\" --background";
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_BREAKAWAY_FROM_JOB, nullptr, dir.c_str(), &startup, &process) &&
        !CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &startup, &process))
        return 3;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

int Setup::Uninstall(bool quiet)
{
    StopRunning();
    Autostart::Set(false);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\DeskFind");
    RegDeleteTreeW(HKEY_CURRENT_USER, UninstallKey);
    DeleteFileW(ShortcutPath().c_str());
    DeleteFolder(KnownFolder(FOLDERID_LocalAppData) + L"\\DeskFind"); // history and logs
    if (!quiet) MessageBoxW(nullptr, Lang::Get().uninstalled, L"DeskFind", MB_OK | MB_ICONINFORMATION);

    std::wstring dir = InstallDir(), exe = InstalledExe();
    if (!EqualsNoCase(SelfPath(), exe))
    {
        DeleteFileW(exe.c_str());
        RemoveDirectoryW(dir.c_str());
        return 0;
    }
    // A running exe can't delete itself: a hidden cmd does it a moment after this process has ended.
    // Only DeskFind.exe goes, and the folder only if nothing else was put there.
    wchar_t system[MAX_PATH], temp[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    GetTempPathW(MAX_PATH, temp);
    std::wstring cmd = std::wstring(system) + L"\\cmd.exe";
    std::wstring command = L"\"" + cmd + L"\" /d /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + exe + L"\" & rd \"" + dir + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (CreateProcessW(cmd.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, temp, &startup, &process))
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return 0;
}
