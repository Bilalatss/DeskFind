// Follows the Windows app theme and accent color.
#pragma once
#include "canvas.h"

struct Theme
{
    bool dark = true;
    Argb accent = 0xFF0078D4;
    Argb tint, hover, selected, divider, scrollThumb, keyCapBg, keyCapBorder, accentFill, selection;
    Palette palette;

    static bool AppsDark();
    static bool SystemDark(); // taskbar and tray
    static bool IsDark(int choice) { return choice == 0 ? AppsDark() : choice == 2; } // Settings::theme
    static Argb Accent();
    // Windows' own accent shades: 0 lightest, 3 the accent itself, 6 darkest.
    static Argb AccentShade(int index);
    // `choice` is Settings::theme; without transparency the background is opaque.
    void Load(float gamma, float grayscaleContrast, int choice = 0, bool transparency = true);
};
