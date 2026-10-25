// Notification area icon and its menu.
#pragma once
#include "common.h"
#include "text.h"

enum : UINT
{
    TrayCmdSearch = 1,
    TrayCmdAutostart,
    TrayCmdAbout,
    TrayCmdExit,
};

class Tray
{
public:
    void Create(HWND owner, const TextEngine* text);
    void Destroy();
    void Refresh(); // accent or theme changed, or Explorer restarted
    UINT ShowMenu(POINT at);
    void Notify(const wchar_t* title, const wchar_t* text); // a balloon; a toast on Windows 11
    static void EnableDarkMenus(HWND owner);
    static void ThemeChanged();

private:
    HICON Render(int size) const;
    bool Add(bool modify);

    HWND owner_ = nullptr;
    const TextEngine* text_ = nullptr;
    HICON icon_ = nullptr, balloonIcon_ = nullptr;
    bool added_ = false;
};
