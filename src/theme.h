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
    static Argb Accent();
    void Load(float gamma, float grayscaleContrast);
};
