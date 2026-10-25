// Notification area icon and its menu.
#pragma once
#include "common.h"

enum : UINT
{
    TrayCmdSearch = 1,
    TrayCmdSettings,
    TrayCmdAutostart,
    TrayCmdAbout,
    TrayCmdExit,
};

class Tray
{
public:
    void Create(HWND owner);
    void SetTip(std::wstring tip) { tip_ = std::move(tip); } // shown from the next Refresh
    void Destroy();
    void Refresh(); // the tip or the DPI changed, or Explorer restarted
    UINT ShowMenu(POINT at, const std::wstring& searchKeys);
    void Notify(const wchar_t* title, const wchar_t* text); // a balloon; a toast on Windows 11
    static void EnableDarkMenus(HWND owner);
    static void ThemeChanged();
    static void SetMenuTheme(int choice); // Settings::theme: menus follow it like the windows do

private:
    bool Add(bool modify);

    HWND owner_ = nullptr;
    HICON icon_ = nullptr, balloonIcon_ = nullptr;
    std::wstring tip_ = L"DeskFind";
    bool added_ = false;
};
