// What the user can change, kept in HKCU\Software\DeskFind and applied the moment it changes; and the usage numbers
// the Statistics page shows. Autostart is not in here: the Run entry itself is the setting.
#pragma once
#include "common.h"
#include "search.h"

struct Hotkey
{
    UINT mods = 0; // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    UINT vk = 0;
    bool operator==(const Hotkey&) const = default;
    // Key names in order, as key caps show them: {"Ctrl", "Shift", "F"}.
    std::vector<std::wstring> Names() const;
    std::wstring Text() const; // "Ctrl+Shift+F"
};

struct Settings
{
    // General.
    bool trayIcon = true;
    int language = 0; // 0 Windows display language, 1 English, 2 Turkish

    // Shortcuts.
    bool desktopKeyOn = true;
    Hotkey desktopKey{MOD_CONTROL, 'F'};
    bool globalKeyOn = false;
    Hotkey globalKey{MOD_WIN | MOD_SHIFT, 'F'};

    // Search. The three result groups are indexed by their Tier value; `order` lists them as shown.
    uint8_t order[3] = {0, 1, 2};
    bool show[3] = {true, true, true};
    uint32_t limit[3] = {50, 40, 80};
    int hiddenFiles = 0; // 0 as File Explorer, 1 show, 2 hide
    bool hideDevFolders = true;
    bool hideSystemFolders = true;
    bool boostOpened = true;
    bool recentWhenEmpty = true;
    std::vector<std::wstring> extraFolders;    // searched like the desktop
    std::vector<std::wstring> excludedFolders; // never shown

    // Appearance.
    int theme = 0;   // 0 Windows setting, 1 light, 2 dark
    int width = 1;   // 0 narrow, 1 normal, 2 wide
    int monitor = 0; // 0 under the mouse pointer, 1 the main screen
    bool transparency = true;
    bool animations = true;
    bool keyHints = true;

    static Settings Load();
    void Save() const;
    SearchOptions Search() const; // the part the search itself follows
    static constexpr uint32_t LimitChoices[] = {10, 20, 40, 50, 80, 150};
};

struct Stats
{
    uint64_t opens = 0;    // the search box was shown
    uint64_t searches = 0; // it closed with something typed
    uint64_t launches = 0; // items opened or shown from it
    uint64_t answers = 0;  // finished answers, and their summed time
    double answerMs = 0;
    int64_t since = 0;         // unix seconds of the first count
    uint32_t day = 0;          // yyyymmdd of todaySearches
    uint32_t todaySearches = 0;

    static Stats Load();
    void Save() const;
    void CountSearch(); // keeps the per-day number on today
    uint32_t SearchesToday() const;
    void Reset();
};
