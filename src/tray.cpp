#include "tray.h"

#include <commctrl.h>
#include <shellapi.h>

#include <cmath>

#include "canvas.h"
#include "lang.h"
#include "shell.h"
#include "theme.h"
#include "version.h"

namespace
{
constexpr UINT TrayId = 1;

// uxtheme exports without names (Windows 10 1903+): what Explorer and Notepad use for dark menus.
enum class AppMode : int
{
    Default,
    AllowDark,
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

void Tray::ThemeChanged()
{
    HMODULE ux = UxTheme();
    if (!ux) return;
    if (auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)))) flush();
}

// Accent rounded square with a white search glyph, drawn at the tray's icon size.
HICON Tray::Render(int size) const
{
    Canvas canvas;
    if (!canvas.Resize(size, size)) return nullptr;
    RECT all{0, 0, size, size};
    canvas.FillBackground(all, 0);
    float s = float(size);
    canvas.FillRoundRect(0, 0, s, s, s * 0.22f, Theme::Accent());
    if (text_)
    {
        Palette palette;
        palette.ink[int(Ink::White)] = 0xFFFFFFFF;
        palette.Build(text_->Gamma(), text_->GrayscaleContrast());
        TextStyle style{Font::Icons, std::round(s * 0.58f), false, Ink::White};
        TextBlock glyph = text_->Render(L"", style);
        int x = int(std::lround((s - glyph.width) / 2)), y = int(std::lround((s - glyph.height) / 2 + s * 0.02f));
        canvas.DrawText(glyph, x, y, palette);
    }

    // Icons take straight alpha.
    std::vector<uint32_t> pixels(canvas.Pixels(), canvas.Pixels() + size * size);
    for (uint32_t& p : pixels)
    {
        uint32_t a = p >> 24;
        if (a == 0 || a == 255) continue;
        uint32_t r = std::min(255u, ((p >> 16) & 0xFF) * 255 / a), g = std::min(255u, ((p >> 8) & 0xFF) * 255 / a), b = std::min(255u, (p & 0xFF) * 255 / a);
        p = (a << 24) | (r << 16) | (g << 8) | b;
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return nullptr;
    memcpy(bits, pixels.data(), pixels.size() * 4);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

bool Tray::Add(bool modify)
{
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = owner_;
    data.uID = TrayId;
    data.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    data.uCallbackMessage = WM_APP_TRAY;
    data.hIcon = icon_;
    wcscpy_s(data.szTip, Lang::Get().trayTip);
    if (!Shell_NotifyIconW(modify ? NIM_MODIFY : NIM_ADD, &data)) return false;
    if (!modify)
    {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
    return true;
}

void Tray::Create(HWND owner, const TextEngine* text)
{
    owner_ = owner;
    text_ = text;
    EnableDarkMenus(owner);
    Refresh();
}

void Tray::Refresh()
{
    HICON old = icon_;
    icon_ = Render(GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem()));
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

UINT Tray::ShowMenu(POINT at)
{
    HMENU menu = CreatePopupMenu();
    const Lang& lang = Lang::Get();
    std::wstring about = std::wstring(lang.menuAbout) + L"\tv" DESKFIND_VERSION;
    AppendMenuW(menu, MF_STRING, TrayCmdSearch, lang.menuSearch);
    AppendMenuW(menu, MF_STRING | (Autostart::IsEnabled() ? MF_CHECKED : 0), TrayCmdAutostart, lang.menuAutostart);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayCmdAbout, about.c_str());
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
