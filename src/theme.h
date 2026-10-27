// Follows the Windows app theme and accent color, or a contrast theme's system colors.
#pragma once
#include "canvas.h"

struct Theme
{
    bool dark = true;
    bool highContrast = false;
    Argb accent = 0xFF0078D4;
    Argb tint, hover, selected, divider, scrollThumb, keyCapBg, keyCapBorder, accentFill, selection;
    // Contrast themes only (0 otherwise): text on `selected` and `selection`, and the outline that shows hover.
    Argb selectedText = 0, outline = 0;
    Palette palette;

    static bool AppsDark();
    static bool SystemDark(); // taskbar and tray
    static bool IsDark(int choice) { return choice == 0 ? AppsDark() : choice == 2; } // Settings::theme
    static bool HighContrast(); // a contrast theme is on (Settings > Accessibility > Contrast themes)
    static Argb SystemColor(int index); // GetSysColor, opaque
    static bool IsDarkColor(Argb color);
    static Argb Accent();
    // Windows' own accent shades: 0 lightest, 3 the accent itself, 6 darkest.
    static Argb AccentShade(int index);
    // `choice` is Settings::theme; without transparency the background is opaque.
    void Load(float gamma, float grayscaleContrast, int choice = 0, bool transparency = true);
};
