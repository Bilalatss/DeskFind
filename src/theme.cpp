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

Argb Theme::Accent()
{
    // DWM stores it as 0xAABBGGRR.
    DWORD abgr = ReadDword(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", 0xFFD47800);
    return 0xFF000000 | ((abgr & 0xFF) << 16) | (abgr & 0xFF00) | ((abgr >> 16) & 0xFF);
}

void Theme::Load(float gamma, float grayscaleContrast)
{
    dark = AppsDark();
    accent = Accent();
    Argb* ink = palette.ink;
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
    ink[int(Ink::White)] = 0xFFFFFFFF;
    selection = (accentFill & 0x00FFFFFF) | 0x66000000; // 40% of the accent behind selected text
    palette.Build(gamma, grayscaleContrast);
}
