#include "theme.h"

namespace
{
constexpr wchar_t PersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

DWORD ReadDword(const wchar_t* key, const wchar_t* name, DWORD fallback)
{
    DWORD value = fallback, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) return fallback;
    return value;
}

Argb Mix(Argb from, Argb to, float amount)
{
    auto channel = [&](int shift)
    {
        float a = float((from >> shift) & 0xFF), b = float((to >> shift) & 0xFF);
        return uint32_t(a + (b - a) * amount + 0.5f) << shift;
    };
    return 0xFF000000 | channel(16) | channel(8) | channel(0);
}
} // namespace

bool Theme::AppsDark() { return ReadDword(PersonalizeKey, L"AppsUseLightTheme", 1) == 0; }
bool Theme::SystemDark() { return ReadDword(PersonalizeKey, L"SystemUsesLightTheme", 1) == 0; }

bool Theme::HighContrast()
{
    HIGHCONTRASTW contrast{sizeof(contrast)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON);
}

Argb Theme::SystemColor(int index)
{
    COLORREF c = GetSysColor(index);
    return 0xFF000000 | (uint32_t(GetRValue(c)) << 16) | (uint32_t(GetGValue(c)) << 8) | GetBValue(c);
}

bool Theme::IsDarkColor(Argb color)
{
    return ((color >> 16) & 0xFF) * 299 + ((color >> 8) & 0xFF) * 587 + (color & 0xFF) * 114 < 128 * 1000;
}

Argb Theme::Accent()
{
    // DWM stores it as 0xAABBGGRR.
    DWORD abgr = ReadDword(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", 0xFFD47800);
    return 0xFF000000 | ((abgr & 0xFF) << 16) | (abgr & 0xFF00) | ((abgr >> 16) & 0xFF);
}

Argb Theme::AccentShade(int index)
{
    // AccentPalette: eight RGBA entries, lightest first; the accent itself is the fourth.
    uint8_t palette[32] = {};
    DWORD size = sizeof(palette);
    if (index >= 0 && index < 7 &&
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette", RRF_RT_REG_BINARY, nullptr,
                     palette, &size) == ERROR_SUCCESS &&
        size >= 28)
    {
        const uint8_t* p = palette + index * 4;
        return 0xFF000000 | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
    }
    Argb base = Accent();
    static constexpr float amounts[] = {0.75f, 0.5f, 0.25f, 0, 0.2f, 0.4f, 0.6f};
    index = std::clamp(index, 0, 6);
    return index < 3 ? Mix(base, 0xFFFFFFFF, amounts[index]) : Mix(base, 0xFF000000, amounts[index]);
}

void Theme::Load(float gamma, float grayscaleContrast, int choice, bool transparency)
{
    highContrast = HighContrast();
    accent = Accent();
    Argb* ink = palette.ink;
    selectedText = outline = 0;
    if (highContrast)
    {
        // The contrast theme's own colors, opaque: no backdrop, and outlines where the usual look relies on faint fills.
        tint = SystemColor(COLOR_WINDOW);
        dark = IsDarkColor(tint);
        ink[int(Ink::Primary)] = ink[int(Ink::Secondary)] = SystemColor(COLOR_WINDOWTEXT);
        ink[int(Ink::Tertiary)] = SystemColor(COLOR_GRAYTEXT);
        ink[int(Ink::Accent)] = SystemColor(COLOR_HOTLIGHT);
        ink[int(Ink::White)] = selectedText = SystemColor(COLOR_HIGHLIGHTTEXT);
        selected = selection = outline = SystemColor(COLOR_HIGHLIGHT);
        hover = keyCapBg = 0;
        divider = scrollThumb = keyCapBorder = ink[int(Ink::Primary)];
        accentFill = selectedText;
        palette.Build(gamma, grayscaleContrast);
        return;
    }
    dark = IsDark(choice);
    if (dark)
    {
        tint = 0xD01E1E1E;
        ink[int(Ink::Primary)] = 0xFFFFFFFF;
        ink[int(Ink::Secondary)] = 0xC5FFFFFF;
        ink[int(Ink::Tertiary)] = 0x87FFFFFF;
        ink[int(Ink::Accent)] = Mix(accent, 0xFFFFFFFF, 0.45f);
        hover = 0x0BFFFFFF;
        selected = 0x15FFFFFF;
        divider = 0x15FFFFFF;
        scrollThumb = 0x5AFFFFFF;
        keyCapBg = 0x0FFFFFFF;
        keyCapBorder = 0x1CFFFFFF;
        accentFill = Mix(accent, 0xFFFFFFFF, 0.25f);
    }
    else
    {
        tint = 0xDCF6F6F6;
        ink[int(Ink::Primary)] = 0xE4000000;
        ink[int(Ink::Secondary)] = 0x9E000000;
        ink[int(Ink::Tertiary)] = 0x72000000;
        ink[int(Ink::Accent)] = Mix(accent, 0xFF000000, 0.18f);
        hover = 0x08000000;
        selected = 0x10000000;
        divider = 0x12000000;
        scrollThumb = 0x50000000;
        keyCapBg = 0x09000000;
        keyCapBorder = 0x16000000;
        accentFill = accent;
    }
    if (!transparency) tint = dark ? 0xFF262626 : 0xFFF7F7F7;
    ink[int(Ink::White)] = 0xFFFFFFFF;
    selection = (accentFill & 0x00FFFFFF) | 0x66000000; // 40% of the accent behind selected text
    palette.Build(gamma, grayscaleContrast);
}
