#include "tray.h"

#include <commctrl.h>
#include <shellapi.h>

#include "lang.h"
#include "shell.h"
#include "version.h"

namespace
{
constexpr UINT TrayId = 1;

// uxtheme exports without names (Windows 10 1903+): what Explorer and Notepad use for dark menus.
enum class AppMode : int
{
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
};
using SetPreferredAppModeFn = AppMode(WINAPI*)(AppMode);
using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
using FlushMenuThemesFn = void(WINAPI*)();

HMODULE UxTheme()
{
    static HMODULE module = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module;
}
} // namespace

void Tray::EnableDarkMenus(HWND owner)
{
    HMODULE ux = UxTheme();
    if (!ux) return;
    if (auto set = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)))) set(AppMode::AllowDark);
    if (auto allow = reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133)))) allow(owner, true);
    ThemeChanged();
}

void Tray::SetMenuTheme(int choice)
{
    HMODULE ux = UxTheme();
    if (!ux) return;
    if (auto set = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135))))
        set(choice == 1 ? AppMode::ForceLight : choice == 2 ? AppMode::ForceDark : AppMode::AllowDark);
    ThemeChanged();
}

void Tray::ThemeChanged()
{
    HMODULE ux = UxTheme();
    if (!ux) return;
    if (auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)))) flush();
}

bool Tray::Add(bool modify)
{
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = owner_;
    data.uID = TrayId;
    data.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    data.uCallbackMessage = WM_APP_TRAY;
    data.hIcon = icon_;
    wcsncpy_s(data.szTip, tip_.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(modify ? NIM_MODIFY : NIM_ADD, &data)) return false;
    if (!modify)
    {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
    return true;
}

void Tray::Create(HWND owner)
{
    owner_ = owner;
    EnableDarkMenus(owner);
    Refresh();
}

void Tray::Refresh()
{
    // The app icon at the tray's size: its small entries are a simpler drawing made for it (tools/make-icon.py).
    HICON old = icon_;
    icon_ = nullptr;
    int size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());
    LoadIconWithScaleDown(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), size, size, &icon_);
    // After an Explorer restart the old icon is gone, so modifying fails and it is added again.
    if (!added_ || !Add(true)) added_ = Add(false);
    if (old) DestroyIcon(old);
}

void Tray::Destroy()
{
    if (added_)
    {
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = owner_;
        data.uID = TrayId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        added_ = false;
    }
    if (icon_) DestroyIcon(icon_);
    if (balloonIcon_) DestroyIcon(balloonIcon_);
    icon_ = balloonIcon_ = nullptr;
}

void Tray::Notify(const wchar_t* title, const wchar_t* text)
{
    if (!added_) return;
    if (!balloonIcon_)
        LoadIconWithScaleDown(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), GetSystemMetricsForDpi(SM_CXICON, GetDpiForSystem()),
                              GetSystemMetricsForDpi(SM_CYICON, GetDpiForSystem()), &balloonIcon_);
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = owner_;
    data.uID = TrayId;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = balloonIcon_ ? NIIF_USER | NIIF_LARGE_ICON : NIIF_INFO;
    data.hBalloonIcon = balloonIcon_;
    wcscpy_s(data.szInfoTitle, title);
    wcscpy_s(data.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

UINT Tray::ShowMenu(POINT at, const std::wstring& searchKeys)
{
    HMENU menu = CreatePopupMenu();
    const Lang& lang = Lang::Get();
    std::wstring search = std::wstring(lang.menuSearch) + (searchKeys.empty() ? L"" : L"\t" + searchKeys);
    std::wstring about = std::wstring(lang.menuAbout) + L"\tv" DESKFIND_VERSION;
    AppendMenuW(menu, MF_STRING, TrayCmdSearch, search.c_str());
    AppendMenuW(menu, MF_STRING, TrayCmdSettings, lang.menuSettings);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (Autostart::IsEnabled() ? MF_CHECKED : 0), TrayCmdAutostart, lang.menuAutostart);
    AppendMenuW(menu, MF_STRING, TrayCmdAbout, about.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayCmdExit, lang.menuExit);
    SetMenuDefaultItem(menu, TrayCmdSearch, FALSE);
    // The menu only closes on an outside click if its owner is the foreground window.
    HWND previous = GetForegroundWindow();
    SetForegroundWindow(owner_);
    UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_BOTTOMALIGN;
    UINT command = UINT(TrackPopupMenuEx(menu, flags, at.x, at.y, owner_, nullptr));
    PostMessageW(owner_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    // Our window is hidden: keyboard focus must not stay with it after the menu closes.
    if (command != TrayCmdSearch && GetForegroundWindow() == owner_ && previous && previous != owner_ && IsWindow(previous))
        SetForegroundWindow(previous);
    return command;
}
