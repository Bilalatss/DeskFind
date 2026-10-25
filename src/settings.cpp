#include "settings.h"

#include <ctime>

#include "lang.h"

namespace
{
constexpr wchar_t SettingsKey[] = L"Software\\DeskFind\\Settings";
constexpr wchar_t StatsKey[] = L"Software\\DeskFind\\Stats";

class Reg
{
public:
    Reg(const wchar_t* path, bool write)
    {
        if (write) RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key_, nullptr);
        else RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &key_);
    }
    ~Reg()
    {
        if (key_) RegCloseKey(key_);
    }
    explicit operator bool() const { return key_ != nullptr; }

    DWORD Get(const wchar_t* name, DWORD fallback) const
    {
        DWORD value = fallback, size = sizeof(value);
        if (!key_ || RegGetValueW(key_, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) return fallback;
        return value;
    }
    uint64_t Get64(const wchar_t* name, uint64_t fallback) const
    {
        uint64_t value = fallback;
        DWORD size = sizeof(value);
        if (!key_ || RegGetValueW(key_, nullptr, name, RRF_RT_REG_QWORD, nullptr, &value, &size) != ERROR_SUCCESS) return fallback;
        return value;
    }
    bool Get(const wchar_t* name, bool fallback) const { return Get(name, DWORD(fallback)) != 0; }
    int Get(const wchar_t* name, int fallback, int max) const
    {
        DWORD value = Get(name, DWORD(fallback));
        return value <= DWORD(max) ? int(value) : fallback;
    }
    std::vector<std::wstring> GetList(const wchar_t* name) const
    {
        std::vector<std::wstring> list;
        DWORD size = 0;
        if (!key_ || RegGetValueW(key_, nullptr, name, RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size < 4) return list;
        std::wstring buffer(size / sizeof(wchar_t), L'\0');
        if (RegGetValueW(key_, nullptr, name, RRF_RT_REG_MULTI_SZ, nullptr, buffer.data(), &size) != ERROR_SUCCESS) return list;
        for (size_t at = 0; at < buffer.size() && buffer[at];)
        {
            size_t end = buffer.find(L'\0', at);
            if (end == std::wstring::npos) end = buffer.size();
            list.emplace_back(buffer, at, end - at);
            at = end + 1;
        }
        return list;
    }

    void Set(const wchar_t* name, DWORD value) const { RegSetValueExW(key_, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)); }
    void Set64(const wchar_t* name, uint64_t value) const { RegSetValueExW(key_, name, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)); }
    void SetList(const wchar_t* name, const std::vector<std::wstring>& list) const
    {
        std::wstring buffer;
        for (const std::wstring& item : list)
        {
            buffer += item;
            buffer.push_back(L'\0');
        }
        buffer.push_back(L'\0');
        RegSetValueExW(key_, name, 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(buffer.data()), DWORD(buffer.size() * sizeof(wchar_t)));
    }

private:
    HKEY key_ = nullptr;
};

DWORD Pack(const Hotkey& key) { return (key.mods << 16) | (key.vk & 0xFFFF); }
Hotkey Unpack(DWORD value, Hotkey fallback)
{
    Hotkey key{(value >> 16) & 0x0F, value & 0xFF};
    return key.vk ? key : fallback;
}

uint32_t Today()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    return uint32_t(t.wYear) * 10000 + t.wMonth * 100 + t.wDay;
}
} // namespace

std::vector<std::wstring> Hotkey::Names() const
{
    std::vector<std::wstring> names;
    if (!vk) return names;
    if (mods & MOD_WIN) names.push_back(L"Win");
    if (mods & MOD_CONTROL) names.push_back(L"Ctrl");
    if (mods & MOD_ALT) names.push_back(L"Alt");
    if (mods & MOD_SHIFT) names.push_back(L"Shift");
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
    {
        names.push_back(std::wstring(1, wchar_t(vk)));
    }
    else if (vk >= VK_F1 && vk <= VK_F24)
    {
        names.push_back(L"F" + std::to_wstring(vk - VK_F1 + 1));
    }
    else if (vk == VK_SPACE)
    {
        names.push_back(Lang::Get().keySpace);
    }
    else
    {
        // Keys whose scan code is ambiguous without the extended flag (the arrows, Home, Insert...).
        bool extended = (vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE || vk == VK_DIVIDE || vk == VK_NUMLOCK;
        LONG scan = LONG(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16;
        if (extended) scan |= 1 << 24;
        wchar_t name[64] = {};
        if (GetKeyNameTextW(scan, name, ARRAYSIZE(name)) > 0)
        {
            std::wstring text = name;
            // Windows spells some of them in capitals ("BOŞLUK", "SAYFA AŞAĞI"); keep the first letter only.
            for (size_t i = 1; i < text.size(); i++)
                if (text[i - 1] != L' ') text[i] = LowerChar(text[i]);
            names.push_back(text);
        }
        else
        {
            names.push_back(L"#" + std::to_wstring(vk));
        }
    }
    return names;
}

std::wstring Hotkey::Text() const
{
    std::wstring text;
    for (const std::wstring& name : Names())
    {
        if (!text.empty()) text += L'+';
        text += name;
    }
    return text;
}

SearchOptions Settings::Search() const
{
    SearchOptions o;
    memcpy(o.order, order, sizeof(o.order));
    for (int g = 0; g < 3; g++)
    {
        o.show[g] = show[g];
        o.limit[g] = limit[g];
    }
    o.hiddenFiles = hiddenFiles;
    o.hideDevFolders = hideDevFolders;
    o.hideSystemFolders = hideSystemFolders;
    o.boostOpened = boostOpened;
    o.recentWhenEmpty = recentWhenEmpty;
    o.extraFolders = extraFolders;
    o.excludedFolders = excludedFolders;
    return o;
}

Settings Settings::Load()
{
    Settings s;
    Reg reg(SettingsKey, false);
    if (!reg) return s;
    s.trayIcon = reg.Get(L"TrayIcon", s.trayIcon);
    s.language = reg.Get(L"Language", s.language, 2);
    s.desktopKeyOn = reg.Get(L"DesktopKeyOn", s.desktopKeyOn);
    s.desktopKey = Unpack(reg.Get(L"DesktopKey", Pack(s.desktopKey)), s.desktopKey);
    s.globalKeyOn = reg.Get(L"GlobalKeyOn", s.globalKeyOn);
    s.globalKey = Unpack(reg.Get(L"GlobalKey", Pack(s.globalKey)), s.globalKey);

    // The order is three distinct groups or the default.
    DWORD order = reg.Get(L"GroupOrder", DWORD(0x020100));
    uint8_t parsed[3] = {uint8_t(order & 0xFF), uint8_t((order >> 8) & 0xFF), uint8_t((order >> 16) & 0xFF)};
    bool seen[3] = {};
    bool valid = true;
    for (uint8_t g : parsed)
    {
        if (g > 2 || seen[g]) valid = false;
        else seen[g] = true;
    }
    if (valid) memcpy(s.order, parsed, sizeof(parsed));
    DWORD shown = reg.Get(L"GroupShow", DWORD(7));
    for (int g = 0; g < 3; g++)
    {
        s.show[g] = (shown >> g) & 1;
        const wchar_t* names[] = {L"LimitDesktop", L"LimitDesktopFolders", L"LimitElsewhere"};
        s.limit[g] = std::clamp<DWORD>(reg.Get(names[g], DWORD(s.limit[g])), 1, 500);
    }
    s.hiddenFiles = reg.Get(L"HiddenFiles", s.hiddenFiles, 2);
    s.hideDevFolders = reg.Get(L"HideDevFolders", s.hideDevFolders);
    s.hideSystemFolders = reg.Get(L"HideSystemFolders", s.hideSystemFolders);
    s.boostOpened = reg.Get(L"BoostOpened", s.boostOpened);
    s.recentWhenEmpty = reg.Get(L"RecentWhenEmpty", s.recentWhenEmpty);
    s.extraFolders = reg.GetList(L"ExtraFolders");
    s.excludedFolders = reg.GetList(L"ExcludedFolders");

    s.theme = reg.Get(L"Theme", s.theme, 2);
    s.width = reg.Get(L"Width", s.width, 2);
    s.monitor = reg.Get(L"Monitor", s.monitor, 1);
    s.transparency = reg.Get(L"Transparency", s.transparency);
    s.animations = reg.Get(L"Animations", s.animations);
    s.keyHints = reg.Get(L"KeyHints", s.keyHints);
    return s;
}

void Settings::Save() const
{
    Reg reg(SettingsKey, true);
    if (!reg) return;
    reg.Set(L"TrayIcon", trayIcon);
    reg.Set(L"Language", DWORD(language));
    reg.Set(L"DesktopKeyOn", desktopKeyOn);
    reg.Set(L"DesktopKey", Pack(desktopKey));
    reg.Set(L"GlobalKeyOn", globalKeyOn);
    reg.Set(L"GlobalKey", Pack(globalKey));
    reg.Set(L"GroupOrder", DWORD(order[0]) | DWORD(order[1]) << 8 | DWORD(order[2]) << 16);
    reg.Set(L"GroupShow", DWORD(show[0]) | DWORD(show[1]) << 1 | DWORD(show[2]) << 2);
    reg.Set(L"LimitDesktop", limit[0]);
    reg.Set(L"LimitDesktopFolders", limit[1]);
    reg.Set(L"LimitElsewhere", limit[2]);
    reg.Set(L"HiddenFiles", DWORD(hiddenFiles));
    reg.Set(L"HideDevFolders", hideDevFolders);
    reg.Set(L"HideSystemFolders", hideSystemFolders);
    reg.Set(L"BoostOpened", boostOpened);
    reg.Set(L"RecentWhenEmpty", recentWhenEmpty);
    reg.SetList(L"ExtraFolders", extraFolders);
    reg.SetList(L"ExcludedFolders", excludedFolders);
    reg.Set(L"Theme", DWORD(theme));
    reg.Set(L"Width", DWORD(width));
    reg.Set(L"Monitor", DWORD(monitor));
    reg.Set(L"Transparency", transparency);
    reg.Set(L"Animations", animations);
    reg.Set(L"KeyHints", keyHints);
}

Stats Stats::Load()
{
    Stats s;
    Reg reg(StatsKey, false);
    if (!reg) return s;
    s.opens = reg.Get64(L"Opens", 0);
    s.searches = reg.Get64(L"Searches", 0);
    s.launches = reg.Get64(L"Launches", 0);
    s.answers = reg.Get64(L"Answers", 0);
    s.answerMs = double(reg.Get64(L"AnswerMicroseconds", 0)) / 1000.0;
    s.since = int64_t(reg.Get64(L"Since", 0));
    s.day = reg.Get(L"Day", DWORD(0));
    s.todaySearches = reg.Get(L"TodaySearches", DWORD(0));
    return s;
}

void Stats::Save() const
{
    Reg reg(StatsKey, true);
    if (!reg) return;
    reg.Set64(L"Opens", opens);
    reg.Set64(L"Searches", searches);
    reg.Set64(L"Launches", launches);
    reg.Set64(L"Answers", answers);
    reg.Set64(L"AnswerMicroseconds", uint64_t(answerMs * 1000.0));
    reg.Set64(L"Since", uint64_t(since));
    reg.Set(L"Day", DWORD(day));
    reg.Set(L"TodaySearches", DWORD(todaySearches));
}

void Stats::CountSearch()
{
    searches++;
    uint32_t today = Today();
    if (day != today)
    {
        day = today;
        todaySearches = 0;
    }
    todaySearches++;
}

uint32_t Stats::SearchesToday() const { return day == Today() ? todaySearches : 0; }

void Stats::Reset()
{
    *this = Stats{};
    since = int64_t(std::time(nullptr));
}
