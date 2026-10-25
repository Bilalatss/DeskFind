#include "settingsui.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <cmath>
#include <cwchar>

#include "everything.h"
#include "icons.h"
#include "lang.h"
#include "shell.h"
#include "theme.h"
#include "version.h"

namespace
{
constexpr wchar_t WindowClass[] = L"DeskFind.Settings";
constexpr wchar_t PopupClass[] = L"DeskFind.Popup";
constexpr UINT_PTR TimerAnim = 1;

// Sizes in device-independent pixels, after the Windows 11 Settings app.
constexpr float NavWidthDip = 264, NavItem = 36, NavGap = 4, NavTop = 76, NavPad = 8;
constexpr float PagePad = 36, HeaderHeight = 80, MaxContent = 1000;
constexpr float CardTall = 68, CardShort = 52, RowHeight = 52, CardGap = 4, RowGap = 2, SectionTop = 20, SectionHeight = 36;
constexpr float TileHeight = 96, HeroHeight = 112, NoteHeight = 32;
constexpr float CardPad = 16, TextLeft = 56, Radius = 4;
constexpr float ControlHeight = 32, ToggleWidth = 40, ToggleHeight = 20, ControlGap = 8, IconSize = 32, CapHeight = 24;
constexpr float PopupItem = 36, PopupPad = 4;
constexpr float Body = 14, Caption = 12, Title = 28;
constexpr double ToggleMs = 140;

const wchar_t* const NavGlyphs[] = {L"\xE713", L"\xE765", L"\xE721", L"\xE790", L"\xE9D2", L"\xE946"};
const wchar_t* const Urls[] = {
    L"" DESKFIND_URL,
    L"" DESKFIND_URL "/issues",
    L"" DESKFIND_URL "/blob/main/LICENSE",
    L"https://www.voidtools.com",
};

SettingsWindow* g_window = nullptr;

RECT Offset(RECT r, int dy)
{
    r.top += dy;
    r.bottom += dy;
    return r;
}

bool Inside(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

std::wstring Format(const wchar_t* format, const std::wstring& value)
{
    wchar_t buffer[256];
    swprintf_s(buffer, format, value.c_str());
    return buffer;
}

std::wstring FormatNumber(const wchar_t* format, uint32_t value)
{
    wchar_t buffer[128];
    swprintf_s(buffer, format, value);
    return buffer;
}

std::wstring FormatBytes(size_t bytes)
{
    wchar_t buffer[32];
    if (bytes >= 1024 * 1024) swprintf_s(buffer, L"%.1f MB", double(bytes) / (1024 * 1024));
    else swprintf_s(buffer, L"%zu KB", (bytes + 1023) / 1024);
    std::wstring text = buffer;
    if (Lang::Get().thousands == L'.') std::replace(text.begin(), text.end(), L'.', L','); // 1,4 MB in Turkish
    return text;
}

std::wstring FormatDate(int64_t unixSeconds)
{
    ULARGE_INTEGER ticks;
    ticks.QuadPart = (uint64_t(unixSeconds) + 11644473600ull) * 10000000ull;
    FILETIME utc{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME universal, local;
    if (!FileTimeToSystemTime(&utc, &universal) || !SystemTimeToTzSpecificLocalTime(nullptr, &universal, &local)) return {};
    wchar_t buffer[64];
    if (!GetDateFormatEx(Lang::Get().locale, 0, &local, L"d MMMM yyyy", buffer, ARRAYSIZE(buffer), nullptr)) return {};
    return buffer;
}
} // namespace

// ---------------------------------------------------------------- lifetime

void SettingsWindow::Open(SettingsHost* host)
{
    if (hwnd_)
    {
        if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
        return;
    }
    host_ = host;
    g_window = this;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        LoadIconWithScaleDown(instance, MAKEINTRESOURCEW(1), GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), &wc.hIconSm);
        wc.lpszClassName = WindowClass;
        RegisterClassExW(&wc);
        WNDCLASSEXW popup{sizeof(popup)};
        popup.lpfnWndProc = PopupProc;
        popup.hInstance = instance;
        popup.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        popup.lpszClassName = PopupClass;
        RegisterClassExW(&popup);
        registered = true;
    }

    // Centered on the screen with the mouse pointer, like the search box.
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    float s = float(dpiX) / 96.0f;
    const RECT& work = info.rcWork;
    int width = std::min(int(1040 * s), int((work.right - work.left) * 0.92f));
    int height = std::min(int(740 * s), int((work.bottom - work.top) * 0.92f));
    int x = work.left + (work.right - work.left - width) / 2, y = work.top + (work.bottom - work.top - height) / 2;

    page_ = Page::General;
    scroll_ = 0;
    focus_ = -1;
    hwnd_ = CreateWindowExW(0, WindowClass, Lang::Get().settingsTitle, WS_OVERLAPPEDWINDOW, x, y, width, height, nullptr, nullptr, instance, this);
    if (!hwnd_) return;
    dpi_ = GetDpiForWindow(hwnd_);
    scale_ = float(dpi_) / 96.0f;
    LoadColors();
    Rebuild();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::Close()
{
    if (hwnd_) DestroyWindow(hwnd_);
}

void SettingsWindow::Refresh()
{
    if (!hwnd_) return;
    ClosePopup(false);
    StopRecording();
    LoadColors();
    SetWindowTextW(hwnd_, Lang::Get().settingsTitle);
    Rebuild();
}

// ---------------------------------------------------------------- colors

void SettingsWindow::LoadColors()
{
    const Settings& s = host_->CurrentSettings();
    transparency_ = s.transparency;
    bool dark = Theme::IsDark(s.theme);
    Colors& c = colors_;
    c.dark = dark;
    // Windows 11's light and dark theme resources (Fluent design tokens).
    if (dark)
    {
        c.background = 0xFF202020;
        c.layer = 0x4D3A3A3A;
        c.layerStroke = 0x19000000;
        c.card = 0x0DFFFFFF;
        c.cardStroke = 0x19000000;
        c.cardHover = 0x15FFFFFF;
        c.control = 0x0FFFFFFF;
        c.controlHover = 0x15FFFFFF;
        c.controlPressed = 0x08FFFFFF;
        c.controlStroke = 0x12FFFFFF;
        c.strong = 0x8BFFFFFF;
        c.toggleOff = 0x19000000;
        c.toggleOffHover = 0x0BFFFFFF;
        c.onAccent = 0xFF000000;
        c.navHover = 0x0BFFFFFF;
        c.navSelected = 0x0FFFFFFF;
        c.focus = 0xFFFFFFFF;
        c.scrollThumb = 0x8BFFFFFF;
        c.warning = 0xFFFF99A4;
        c.popup = transparency_ ? 0xD82C2C2C : 0xFF2C2C2C;
        c.divider = 0x15FFFFFF;
        c.accent = Theme::AccentShade(1);
    }
    else
    {
        c.background = 0xFFF3F3F3;
        c.layer = 0x80FFFFFF;
        c.layerStroke = 0x0F000000;
        c.card = 0xB3FFFFFF;
        c.cardStroke = 0x0F000000;
        c.cardHover = 0x80F9F9F9;
        c.control = 0xB3FFFFFF;
        c.controlHover = 0x80F9F9F9;
        c.controlPressed = 0x4DF9F9F9;
        c.controlStroke = 0x29000000;
        c.strong = 0x72000000;
        c.toggleOff = 0x06000000;
        c.toggleOffHover = 0x0F000000;
        c.onAccent = 0xFFFFFFFF;
        c.navHover = 0x09000000;
        c.navSelected = 0x0F000000;
        c.focus = 0xE4000000;
        c.scrollThumb = 0x72000000;
        c.warning = 0xFFC42B1C;
        c.popup = transparency_ ? 0xE6FCFCFC : 0xFFFCFCFC;
        c.divider = 0x0F000000;
        c.accent = Theme::AccentShade(4);
    }
    c.accentHover = (c.accent & 0x00FFFFFF) | 0xE6000000;
    c.accentPressed = (c.accent & 0x00FFFFFF) | 0xCC000000;

    Argb* ink = palette_.ink;
    ink[int(Ink::Primary)] = dark ? 0xFFFFFFFF : 0xE4000000;
    ink[int(Ink::Secondary)] = dark ? 0xC5FFFFFF : 0x9E000000;
    ink[int(Ink::Tertiary)] = dark ? 0x87FFFFFF : 0x72000000;
    ink[int(Ink::Accent)] = dark ? Theme::AccentShade(0) : Theme::AccentShade(5);
    ink[int(Ink::White)] = c.onAccent;
    TextEngine& text = host_->Text();
    palette_.Build(text.Gamma(), text.GrayscaleContrast());

    if (!hwnd_) return;
    BOOL darkMode = dark;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    DWM_SYSTEMBACKDROP_TYPE backdrop = transparency_ ? DWMSBT_MAINWINDOW : DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    MARGINS margins = transparency_ ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
}

TextBlock SettingsWindow::Text(std::wstring_view text, float size, bool semibold, Ink ink, float maxWidth, Font font) const
{
    return host_->Text().Render(text, TextStyle{font, size * scale_, semibold, ink}, maxWidth);
}

// ---------------------------------------------------------------- pages

SettingsWindow::Item& SettingsWindow::AddSection(const wchar_t* title)
{
    Item item{Item::Kind::Section};
    item.title = title;
    items_.push_back(std::move(item));
    return items_.back();
}

SettingsWindow::Item& SettingsWindow::AddCard(const wchar_t* glyph, const std::wstring& title, const std::wstring& desc, Item::Kind kind)
{
    Item item{kind};
    item.glyph = glyph ? glyph : L"";
    item.title = title;
    item.desc = desc;
    items_.push_back(std::move(item));
    return items_.back();
}

SettingsWindow::Control& SettingsWindow::AddControl(Item& item, Control::Type type, Act act, int arg)
{
    Control control{type};
    control.act = act;
    control.arg = arg;
    item.controls.push_back(std::move(control));
    return item.controls.back();
}

void SettingsWindow::AddToggle(Item& item, Act act, bool on, int arg, bool enabled)
{
    Control& c = AddControl(item, Control::Type::Toggle, act, arg);
    c.on = on;
    c.enabled = enabled;
}

void SettingsWindow::AddCombo(Item& item, Act act, std::vector<std::wstring> options, int selected, int arg)
{
    Control& c = AddControl(item, Control::Type::Combo, act, arg);
    c.selected = std::clamp(selected, 0, int(options.size()) - 1);
    c.text = options[size_t(c.selected)];
    c.options = std::move(options);
}

void SettingsWindow::AddButton(Item& item, Act act, const wchar_t* text, int arg)
{
    AddControl(item, Control::Type::Button, act, arg).text = text;
}

void SettingsWindow::AddIcon(Item& item, Act act, const wchar_t* glyph, int arg, bool enabled)
{
    Control& c = AddControl(item, Control::Type::IconButton, act, arg);
    c.text = glyph;
    c.enabled = enabled;
}

void SettingsWindow::Rebuild()
{
    if (!hwnd_) return;
    items_.clear();
    switch (page_)
    {
    case Page::General: BuildGeneral(); break;
    case Page::Shortcuts: BuildShortcuts(); break;
    case Page::Search: BuildSearch(); break;
    case Page::Appearance: BuildAppearance(); break;
    case Page::Stats: BuildStats(); break;
    default: BuildAbout(); break;
    }
    const Lang& lang = Lang::Get();
    const wchar_t* names[] = {lang.navGeneral, lang.navShortcuts, lang.navSearch, lang.navAppearance, lang.navStats, lang.navAbout};
    navText_.clear();
    navGlyphs_.clear();
    for (int i = 0; i < int(Page::Count); i++)
    {
        navText_.push_back(Text(names[i], Body, false, Ink::Primary));
        navGlyphs_.push_back(Text(NavGlyphs[i], 16, false, Ink::Primary, 0, Font::Icons));
    }
    appName_ = Text(L"DeskFind", Body, true, Ink::Primary);
    appVersion_ = Text(L"v" DESKFIND_VERSION, Caption, false, Ink::Secondary);
    pageTitle_ = Text(names[int(page_)], Title, true, Ink::Primary);
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::BuildGeneral()
{
    const Lang& lang = Lang::Get();
    const Settings& s = host_->CurrentSettings();
    AddSection(lang.secStartup);
    AddToggle(AddCard(L"\xE7E8", lang.autostartTitle, lang.autostartDesc), Act::Autostart, Autostart::IsEnabled());
    AddToggle(AddCard(L"\xEDA2", lang.trayTitle, lang.trayDesc), Act::TrayIcon, s.trayIcon);
    AddSection(lang.secLanguage);
    AddCombo(AddCard(L"\xF2B7", lang.languageTitle, lang.languageDesc), Act::Language, {lang.languageSystem, L"English", L"Türkçe"}, s.language);
    AddSection(lang.secReset);
    AddButton(AddCard(L"\xE777", lang.resetTitle, lang.resetDesc), Act::ResetSettings, lang.resetButton);
}

void SettingsWindow::BuildShortcuts()
{
    const Lang& lang = Lang::Get();
    const Settings& s = host_->CurrentSettings();
    AddSection(lang.secOpen);
    struct KeyCard
    {
        const wchar_t *glyph, *title, *desc;
        Act edit, toggle;
        const Hotkey& key;
        bool on;
    };
    KeyCard cards[] = {
        {L"\xEC4E", lang.desktopKeyTitle, lang.desktopKeyDesc, Act::DesktopKey, Act::DesktopKeyOn, s.desktopKey, s.desktopKeyOn},
        {L"\xE774", lang.globalKeyTitle, lang.globalKeyDesc, Act::GlobalKey, Act::GlobalKeyOn, s.globalKey, s.globalKeyOn},
    };
    for (const KeyCard& k : cards)
    {
        Item& card = AddCard(k.glyph, k.title, k.desc);
        if (errorFor_ == k.edit && !recordError_.empty())
        {
            card.desc = recordError_;
            card.warning = true;
        }
        else if (k.edit == Act::GlobalKey && s.globalKeyOn && host_->GlobalKeyFailed())
        {
            card.desc = lang.keyTaken;
            card.warning = true;
        }
        Control& key = AddControl(card, Control::Type::Hotkey, k.edit);
        key.keys = k.key.Names();
        key.enabled = k.on;
        AddToggle(card, k.toggle, k.on);
    }

    AddSection(lang.secInBox);
    struct Row
    {
        const wchar_t* glyph;
        const wchar_t* title;
        std::vector<std::wstring> keys;
    };
    Row rows[] = {
        {L"\xE8CB", lang.inBoxMove, {L"↑", L"↓"}},
        {L"\xE8A7", lang.hintOpen, {L"Enter"}},
        {L"\xEC4E", lang.hintReveal, {L"Ctrl", L"Enter"}},
        {L"\xE8C8", lang.inBoxCopy, {L"Ctrl", L"Shift", L"C"}},
        {L"\xE721", lang.inBoxEverything, {L"Ctrl", L"E"}},
        {L"\xE72C", lang.inBoxAgain, {L"F5"}},
        {L"\xE713", lang.inBoxSettings, {L"Ctrl", L","}},
        {L"\xE711", lang.hintClose, {L"Esc"}},
    };
    for (const Row& r : rows)
    {
        Item& row = AddCard(r.glyph, r.title, L"", Item::Kind::Row);
        AddControl(row, Control::Type::Keys, Act::None).keys = r.keys;
    }
}

void SettingsWindow::BuildSearch()
{
    const Lang& lang = Lang::Get();
    const Settings& s = host_->CurrentSettings();
    AddSection(lang.secGroups);
    AddCard(L"\xE8CB", lang.groupsTitle, lang.groupsDesc);
    const wchar_t* groupNames[] = {lang.desktop, lang.desktopFolders, lang.elsewhere};
    const wchar_t* groupGlyphs[] = {L"\xEC4E", L"\xE8B7", L"\xE7F8"};
    std::vector<std::wstring> limits;
    for (uint32_t limit : Settings::LimitChoices) limits.push_back(FormatNumber(lang.limitFormat, limit));
    for (int position = 0; position < 3; position++)
    {
        int group = s.order[position];
        Item& row = AddCard(groupGlyphs[group], groupNames[group], L"", Item::Kind::Row);
        int selected = 0;
        for (int i = 0; i < int(std::size(Settings::LimitChoices)); i++)
            if (Settings::LimitChoices[i] <= s.limit[group]) selected = i;
        AddCombo(row, Act::GroupLimit, limits, selected, group);
        AddIcon(row, Act::GroupUp, L"\xE70E", position, position > 0);
        AddIcon(row, Act::GroupDown, L"\xE70D", position, position < 2);
        AddToggle(row, Act::GroupShow, s.show[group], group);
    }

    AddSection(lang.secWhat);
    AddCombo(AddCard(L"\xE890", lang.hiddenTitle, lang.hiddenDesc), Act::HiddenFiles, {lang.hiddenExplorer, lang.hiddenShow, lang.hiddenHide},
             s.hiddenFiles);
    AddToggle(AddCard(L"\xE943", lang.devTitle, lang.devDesc), Act::DevFolders, s.hideDevFolders);
    AddToggle(AddCard(L"\xEA18", lang.systemTitle, lang.systemDesc), Act::SystemFolders, s.hideSystemFolders);
    AddButton(AddCard(L"\xE8B7", lang.extraTitle, lang.extraDesc), Act::AddExtra, lang.addFolder);
    for (size_t i = 0; i < s.extraFolders.size(); i++)
        AddIcon(AddCard(L"\xE838", s.extraFolders[i], L"", Item::Kind::Row), Act::RemoveExtra, L"\xE74D", int(i));
    AddButton(AddCard(L"\xE71C", lang.excludedTitle, lang.excludedDesc), Act::AddExcluded, lang.addFolder);
    for (size_t i = 0; i < s.excludedFolders.size(); i++)
        AddIcon(AddCard(L"\xE838", s.excludedFolders[i], L"", Item::Kind::Row), Act::RemoveExcluded, L"\xE74D", int(i));

    AddSection(lang.secRanking);
    AddToggle(AddCard(L"\xE734", lang.boostTitle, lang.boostDesc), Act::BoostOpened, s.boostOpened);
    AddToggle(AddCard(L"\xE81C", lang.recentTitle, lang.recentDesc), Act::RecentWhenEmpty, s.recentWhenEmpty);
}

void SettingsWindow::BuildAppearance()
{
    const Lang& lang = Lang::Get();
    const Settings& s = host_->CurrentSettings();
    AddSection(lang.secWindow);
    AddCombo(AddCard(L"\xE7A1", lang.themeTitle, lang.themeDesc), Act::Theme, {lang.themeSystem, lang.themeLight, lang.themeDark}, s.theme);
    AddCombo(AddCard(L"\xE740", lang.widthTitle, lang.widthDesc), Act::Width, {lang.widthNarrow, lang.widthNormal, lang.widthWide}, s.width);
    AddCombo(AddCard(L"\xE7F4", lang.monitorTitle, lang.monitorDesc), Act::Monitor, {lang.monitorMouse, lang.monitorMain}, s.monitor);
    AddSection(lang.secEffects);
    AddToggle(AddCard(L"\xF573", lang.transparencyTitle, lang.transparencyDesc), Act::Transparency, s.transparency);
    AddToggle(AddCard(L"\xE768", lang.animationsTitle, lang.animationsDesc), Act::Animations, s.animations);
    AddToggle(AddCard(L"\xE765", lang.hintsTitle, lang.hintsDesc), Act::KeyHints, s.keyHints);
}

void SettingsWindow::BuildStats()
{
    const Lang& lang = Lang::Get();
    const Stats& stats = host_->CurrentStats();
    const History& history = host_->OpenedHistory();
    AddSection(lang.secUsage);
    wchar_t average[32] = L"–";
    if (stats.answers)
    {
        double ms = stats.answerMs / double(stats.answers);
        if (ms < 1) wcscpy_s(average, L"<1 ms");
        else swprintf_s(average, L"%.0f ms", ms);
    }
    struct Tile
    {
        const wchar_t *glyph, *title;
        std::wstring value;
    };
    Tile tiles[] = {
        {L"\xE721", lang.statSearches, FormatCount(stats.searches)},
        {L"\xE787", lang.statToday, FormatCount(stats.SearchesToday())},
        {L"\xE8A7", lang.statLaunches, FormatCount(stats.launches)},
        {L"\xE7C4", lang.statOpens, FormatCount(stats.opens)},
        {L"\xEC4A", lang.statAverage, average},
        {L"\xE81C", lang.statRemembered, FormatCount(history.Entries().size())},
    };
    for (Tile& t : tiles) AddCard(t.glyph, t.title, L"", Item::Kind::Tile).value = std::move(t.value);
    if (stats.since) AddCard(nullptr, Format(lang.statSince, FormatDate(stats.since)), L"", Item::Kind::Note);

    AddSection(lang.secTop);
    std::vector<const History::Entry*> top;
    for (const History::Entry& entry : history.Entries()) top.push_back(&entry);
    std::stable_sort(top.begin(), top.end(), [](const History::Entry* a, const History::Entry* b) { return a->count > b->count; });
    if (top.size() > 5) top.resize(5);
    if (top.empty()) AddCard(nullptr, lang.topEmpty, L"", Item::Kind::Note);
    for (const History::Entry* entry : top)
    {
        size_t slash = entry->path.rfind(L'\\');
        std::wstring_view name = slash == std::wstring::npos ? std::wstring_view(entry->path) : std::wstring_view(entry->path).substr(slash + 1);
        std::wstring_view dir = slash == std::wstring::npos ? std::wstring_view() : std::wstring_view(entry->path).substr(0, slash);
        if (dir.size() == 2) dir = std::wstring_view(entry->path).substr(0, 3); // "C:\"
        std::wstring display(name);
        if (!entry->folder && display.size() > 4 && (EqualsNoCase(display.substr(display.size() - 4), L".lnk") || EqualsNoCase(display.substr(display.size() - 4), L".url")))
            display.resize(display.size() - 4);
        Item& row = AddCard(entry->folder ? L"\xE8B7" : L"\xE8A5", display, host_->DescribeLocation(dir));
        AddControl(row, Control::Type::Label, Act::None).text =
            entry->count == 1 ? std::wstring(lang.timesOne) : FormatNumber(lang.timesFormat, entry->count);
    }

    AddSection(lang.secEverything);
    std::wstring version = Everything::Version();
    // An elevated Everything doesn't answer the version question (UIPI), yet it runs and searches.
    std::wstring state = !version.empty()            ? Format(lang.everythingRunning, version)
                         : Everything::IsRunning() ? std::wstring(lang.everythingRunningPlain)
                                                   : std::wstring(lang.everythingStopped);
    AddCard(L"\xE721", lang.everythingState, state);
    size_t entries = host_->IndexEntries();
    std::wstring index = lang.indexIdle;
    if (entries)
    {
        wchar_t buffer[160];
        swprintf_s(buffer, lang.indexReady, FormatCount(entries).c_str(), FormatBytes(host_->IndexBytes()).c_str());
        index = buffer;
    }
    AddCard(L"\xE8B7", lang.indexTitle, index);

    AddSection(lang.secData);
    AddButton(AddCard(L"\xE74D", lang.clearHistoryTitle, lang.clearHistoryDesc), Act::ClearHistory, lang.clearButton);
    AddButton(AddCard(L"\xE777", lang.resetStatsTitle, lang.resetStatsDesc), Act::ResetStats, lang.resetButton);
    Item& data = AddCard(L"\xE838", lang.openDataTitle, lang.openDataDesc);
    data.act = Act::OpenDataFolder;
}

void SettingsWindow::BuildAbout()
{
    const Lang& lang = Lang::Get();
    AddCard(nullptr, L"DeskFind", Format(lang.versionFormat, L"" DESKFIND_VERSION), Item::Kind::Hero).value = lang.aboutTagline;
    AddSection(lang.secLinks);
    struct Link
    {
        const wchar_t *glyph, *title;
    };
    Link links[] = {{L"\xE943", lang.linkGitHub}, {L"\xED15", lang.linkIssues}, {L"\xE8A5", lang.linkLicense}, {L"\xE721", lang.linkEverything}};
    for (int i = 0; i < 4; i++)
    {
        std::wstring url = Urls[i];
        size_t scheme = url.find(L"://");
        Item& card = AddCard(links[i].glyph, links[i].title, scheme == std::wstring::npos ? url : url.substr(scheme + 3));
        card.act = Act::OpenUrl;
        card.arg = i;
    }
    AddCard(nullptr, lang.aboutNote, L"", Item::Kind::Note);
}

// ---------------------------------------------------------------- layout

int SettingsWindow::NavWidth() const { return D(NavWidthDip); }
int SettingsWindow::HeaderBottom() const { return D(HeaderHeight); }

RECT SettingsWindow::Viewport() const
{
    RECT client{};
    if (hwnd_) GetClientRect(hwnd_, &client);
    return RECT{NavWidth(), HeaderBottom(), client.right, client.bottom};
}

int SettingsWindow::MaxScroll() const
{
    RECT vp = Viewport();
    return std::max(0, contentHeight_ - int(vp.bottom - vp.top));
}

float SettingsWindow::ControlWidth(const Control& c, bool recording) const
{
    const TextEngine& text = host_->Text();
    auto measure = [&](std::wstring_view s, float size) { return text.Measure(s, TextStyle{Font::Ui, size * scale_, false, Ink::Primary}); };
    const Lang& lang = Lang::Get();
    switch (c.type)
    {
    case Control::Type::Toggle: return std::max(measure(lang.on, Body), measure(lang.off, Body)) + D(12) + D(ToggleWidth);
    case Control::Type::Combo:
    {
        float widest = 0;
        for (const std::wstring& option : c.options) widest = std::max(widest, measure(option, Body));
        return std::max(float(D(120)), widest + D(11) + D(16) + D(12) + D(11));
    }
    case Control::Type::Button: return std::max(float(D(96)), measure(c.text, Body) + D(24));
    case Control::Type::IconButton: return float(D(IconSize));
    case Control::Type::Hotkey:
    {
        float width = float(D(10) + D(14) + D(10)); // the pencil
        if (recording) return std::max(width + measure(lang.keyRecording, Body) + D(12), float(D(160)));
        if (c.keys.empty()) return width + measure(lang.keyNone, Body) + D(10);
        for (const std::wstring& key : c.keys) width += measure(key, Caption) + D(16) + D(4);
        return width + D(6);
    }
    case Control::Type::Keys:
    {
        float width = 0;
        for (const std::wstring& key : c.keys) width += measure(key, Caption) + D(16) + D(4);
        return width - D(4);
    }
    case Control::Type::Label: return measure(c.text, Body);
    }
    return 0;
}

void SettingsWindow::Layout()
{
    RECT client;
    GetClientRect(hwnd_, &client);
    int left = NavWidth() + D(PagePad);
    int width = std::max(D(240), std::min(int(client.right) - left - D(PagePad), D(MaxContent)));
    int right = left + width;
    int y = 0;
    for (size_t i = 0; i < items_.size(); i++)
    {
        Item& item = items_[i];
        bool nextIsRow = i + 1 < items_.size() && items_[i + 1].kind == Item::Kind::Row;
        int gap = D(nextIsRow ? RowGap : CardGap);
        switch (item.kind)
        {
        case Item::Kind::Section:
            if (i > 0) y += D(SectionTop);
            item.rect = RECT{left, y, right, y + D(SectionHeight)};
            y += D(SectionHeight);
            break;
        case Item::Kind::Card:
        case Item::Kind::Row:
        {
            // A description wider than the room beside the controls wraps onto a second line, as in Settings.
            int controlsLeft = right - D(CardPad) - (item.act != Act::None ? D(28) : 0);
            for (size_t c = 0; c < item.controls.size(); c++)
                controlsLeft -= int(std::ceil(ControlWidth(item.controls[c], recording_ == Target{-1, int(i), int(c)}))) + D(ControlGap);
            int room = (item.controls.empty() ? controlsLeft : controlsLeft + D(ControlGap) - D(16)) - (left + D(TextLeft));
            item.wrap = !item.desc.empty() &&
                        host_->Text().Measure(item.desc, TextStyle{Font::Ui, Caption * scale_, false, Ink::Secondary}) > float(room);
            int h = D(item.kind == Item::Kind::Row ? RowHeight : item.desc.empty() ? CardShort : CardTall) + (item.wrap ? D(16) : 0);
            item.rect = RECT{left, y, right, y + h};
            y += h + gap;
            break;
        }
        case Item::Kind::Tile:
        {
            // Consecutive tiles form a grid.
            size_t end = i;
            while (end < items_.size() && items_[end].kind == Item::Kind::Tile) end++;
            int columns = width >= D(620) ? 3 : 2;
            int tileWidth = (width - (columns - 1) * D(CardGap)) / columns;
            for (size_t t = i; t < end; t++)
            {
                int column = int(t - i) % columns, row = int(t - i) / columns;
                int x = left + column * (tileWidth + D(CardGap)), top = y + row * (D(TileHeight) + D(CardGap));
                items_[t].rect = RECT{x, top, x + tileWidth, top + D(TileHeight)};
            }
            int rows = int((end - i + columns - 1) / columns);
            y += rows * (D(TileHeight) + D(CardGap));
            i = end - 1;
            continue;
        }
        case Item::Kind::Hero:
            item.rect = RECT{left, y, right, y + D(HeroHeight)};
            y += D(HeroHeight) + D(CardGap);
            break;
        case Item::Kind::Note:
            item.rect = RECT{left, y, right, y + D(NoteHeight)};
            y += D(NoteHeight);
            break;
        }
        item.renderedWidth = -1;
        int x = item.rect.right - D(CardPad) - (item.act != Act::None ? D(28) : 0);
        for (size_t c = item.controls.size(); c-- > 0;)
        {
            Control& control = item.controls[c];
            int w = int(std::ceil(ControlWidth(control, recording_ == Target{-1, int(i), int(c)})));
            int h = D(control.type == Control::Type::Keys ? CapHeight : ControlHeight);
            int top = (item.rect.top + item.rect.bottom - h) / 2;
            control.rect = RECT{x - w, top, x, top + h};
            control.rendered = false;
            x -= w + D(ControlGap);
        }
    }
    contentHeight_ = y + D(PagePad);
    scroll_ = std::clamp(scroll_, 0, MaxScroll());
}

// ---------------------------------------------------------------- painting

void SettingsWindow::Paint(HDC hdc)
{
    RECT client;
    GetClientRect(hwnd_, &client);
    if (!canvas_.Resize(client.right, client.bottom)) return;
    canvas_.ResetClip();
    canvas_.opacity = 1;
    // With Mica the window itself stays see-through; the content sits on a lighter layer, like Settings.
    canvas_.FillBackground(client, transparency_ ? 0x00000000 : colors_.background);
    int nav = NavWidth();
    canvas_.FillRoundRect(float(nav), 0, float(client.right + D(8)), float(client.bottom + D(8)), float(D(8)), colors_.layer);
    canvas_.StrokeRoundRect(float(nav), 0, float(client.right + D(8)), float(client.bottom + D(8)), float(D(8)), 1, colors_.layerStroke);
    PaintNav();

    canvas_.DrawText(pageTitle_, nav + D(PagePad), D(28), palette_);

    RECT vp = Viewport();
    canvas_.SetClip(vp);
    int dy = vp.top - scroll_;
    for (size_t i = 0; i < items_.size(); i++)
    {
        RECT r = Offset(items_[i].rect, dy);
        if (r.bottom < vp.top || r.top > vp.bottom) continue;
        PaintItem(items_[i], int(i), dy);
    }
    canvas_.ResetClip();
    if (int max = MaxScroll(); max > 0)
    {
        int track = vp.bottom - vp.top - D(8);
        int thumb = std::max(D(32), int(int64_t(track) * (vp.bottom - vp.top) / std::max(1, contentHeight_)));
        int top = vp.top + D(4) + int(int64_t(track - thumb) * scroll_ / max);
        int w = dragThumb_ ? D(6) : D(3);
        canvas_.FillRoundRect(float(client.right - D(4) - w), float(top), float(client.right - D(4)), float(top + thumb), float(w) / 2, colors_.scrollThumb);
    }
    PaintFocus(dy);
    BitBlt(hdc, 0, 0, client.right, client.bottom, canvas_.Dc(), 0, 0, SRCCOPY);
}

void SettingsWindow::PaintAppIcon(int x, int y, int size)
{
    auto found = std::find_if(appIcons_.begin(), appIcons_.end(), [&](const auto& entry) { return entry.first == size; });
    if (found == appIcons_.end())
    {
        appIcons_.emplace_back(size, IconLoader::AppIcon(size));
        found = appIcons_.end() - 1;
    }
    if (!found->second.empty()) canvas_.DrawImage(found->second.data(), size, size, x, y);
}

void SettingsWindow::PaintNav()
{
    int icon = D(28);
    PaintAppIcon(D(18), D(22), icon);
    canvas_.DrawText(appName_, D(18) + icon + D(12), D(20), palette_);
    canvas_.DrawText(appVersion_, D(18) + icon + D(12), D(20) + int(appName_.height), palette_);
    for (int i = 0; i < int(Page::Count); i++)
    {
        int top = D(NavTop) + i * (D(NavItem) + D(NavGap));
        RECT r{D(NavPad), top, NavWidth() - D(NavPad), top + D(NavItem)};
        bool selected = int(page_) == i, hot = hover_.nav == i;
        Argb fill = selected ? (hot ? colors_.cardHover : colors_.navSelected) : hot ? colors_.navHover : 0;
        if (pressed_.nav == i && hot) fill = colors_.controlPressed;
        if (fill) canvas_.FillRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), float(D(Radius)), fill);
        if (selected)
        {
            float middle = float(r.top + r.bottom) / 2;
            canvas_.FillRoundRect(float(r.left), middle - float(D(8)), float(r.left + D(3)), middle + float(D(8)), float(D(1.5f)), colors_.accent);
        }
        const TextBlock& glyph = navGlyphs_[size_t(i)];
        const TextBlock& text = navText_[size_t(i)];
        canvas_.DrawText(glyph, r.left + D(14), (r.top + r.bottom - int(glyph.height)) / 2, palette_);
        canvas_.DrawText(text, r.left + D(46), (r.top + r.bottom - int(text.height)) / 2, palette_);
    }
}

void SettingsWindow::PaintItem(Item& item, int index, int dy)
{
    RECT r = Offset(item.rect, dy);
    float radius = float(D(Radius));
    switch (item.kind)
    {
    case Item::Kind::Section:
    {
        if (item.renderedWidth < 0)
        {
            item.titleText = Text(item.title, Body, true, Ink::Primary);
            item.renderedWidth = 0;
        }
        canvas_.DrawText(item.titleText, r.left + D(2), r.bottom - int(item.titleText.height) - D(8), palette_);
        return;
    }
    case Item::Kind::Note:
    {
        if (item.renderedWidth != r.right - r.left)
        {
            item.titleText = Text(item.title, Caption, false, Ink::Secondary, float(r.right - r.left));
            item.renderedWidth = r.right - r.left;
        }
        canvas_.DrawText(item.titleText, r.left + D(2), (r.top + r.bottom - int(item.titleText.height)) / 2, palette_);
        return;
    }
    case Item::Kind::Tile:
    {
        canvas_.FillRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, colors_.card);
        canvas_.StrokeRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, 1, colors_.cardStroke);
        if (item.renderedWidth != r.right - r.left)
        {
            float room = float(r.right - r.left - D(CardPad) * 2);
            item.glyphText = Text(item.glyph, 16, false, Ink::Accent, 0, Font::Icons);
            item.titleText = Text(item.title, Caption, false, Ink::Secondary, room - D(24));
            item.valueText = Text(item.value, Title, true, Ink::Primary, room);
            item.renderedWidth = r.right - r.left;
        }
        int x = r.left + D(CardPad);
        canvas_.DrawText(item.glyphText, x, r.top + D(16), palette_);
        canvas_.DrawText(item.titleText, x + D(24), r.top + D(16) + (int(item.glyphText.height) - int(item.titleText.height)) / 2, palette_);
        canvas_.DrawText(item.valueText, x, r.bottom - D(12) - int(item.valueText.height), palette_);
        return;
    }
    case Item::Kind::Hero:
    {
        int icon = D(64);
        PaintAppIcon(r.left, r.top + (r.bottom - r.top - icon) / 2, icon);
        if (item.renderedWidth < 0)
        {
            item.titleText = Text(item.title, 20, true, Ink::Primary);
            item.descText = Text(item.desc, Body, false, Ink::Secondary);
            item.valueText = Text(item.value, Body, false, Ink::Secondary);
            item.renderedWidth = 0;
        }
        int x = r.left + icon + D(20);
        int stack = int(item.titleText.height + item.descText.height + item.valueText.height);
        int y = (r.top + r.bottom - stack) / 2;
        canvas_.DrawText(item.titleText, x, y, palette_);
        canvas_.DrawText(item.valueText, x, y + int(item.titleText.height), palette_);
        canvas_.DrawText(item.descText, x, y + int(item.titleText.height + item.valueText.height), palette_);
        return;
    }
    default: break;
    }

    // Cards and rows.
    Target self{-1, index, -1};
    bool link = item.act != Act::None;
    Argb fill = colors_.card;
    if (link && hover_ == self) fill = pressed_ == self ? colors_.controlPressed : colors_.cardHover;
    canvas_.FillRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, fill);
    canvas_.StrokeRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, 1, colors_.cardStroke);

    int textX = r.left + D(TextLeft);
    int textRight = item.controls.empty() ? r.right - D(CardPad) - (link ? D(28) : 0) : int(item.controls.front().rect.left) - D(16);
    int room = std::max(D(40), textRight - textX);
    if (item.renderedWidth != room)
    {
        item.glyphText = item.glyph.empty() ? TextBlock{} : Text(item.glyph, item.kind == Item::Kind::Row ? 16.0f : 20.0f, false, Ink::Primary, 0, Font::Icons);
        item.titleText = Text(item.title, Body, false, Ink::Primary, float(room));
        item.descText = item.descText2 = TextBlock{};
        if (item.wrap)
        {
            // Break after the last word that still fits; the rest goes on the second line.
            const TextEngine& engine = host_->Text();
            TextStyle style{Font::Ui, Caption * scale_, false, Ink::Secondary};
            size_t cut = 0;
            for (size_t at = item.desc.find(L' '); at != std::wstring::npos; at = item.desc.find(L' ', at + 1))
            {
                if (engine.Measure(std::wstring_view(item.desc).substr(0, at), style) > float(room)) break;
                cut = at;
            }
            if (cut == 0) cut = item.desc.size();
            item.descText = Text(std::wstring_view(item.desc).substr(0, cut), Caption, false, Ink::Secondary, float(room));
            if (cut < item.desc.size()) item.descText2 = Text(std::wstring_view(item.desc).substr(cut + 1), Caption, false, Ink::Secondary, float(room));
        }
        else if (!item.desc.empty())
        {
            item.descText = Text(item.desc, Caption, false, Ink::Secondary, float(room));
        }
        if (link) item.chevronText = Text(item.act == Act::OpenUrl ? L"\xE8A7" : L"\xE76C", 12, false, Ink::Primary, 0, Font::Icons);
        item.renderedWidth = room;
    }
    int middle = (r.top + r.bottom) / 2;
    if (!item.glyphText.masks.empty())
        canvas_.DrawText(item.glyphText, r.left + D(CardPad) + (D(20) - int(item.glyphText.width)) / 2, middle - int(item.glyphText.height) / 2, palette_);
    if (item.desc.empty())
    {
        canvas_.DrawText(item.titleText, textX, middle - int(item.titleText.height) / 2, palette_);
    }
    else
    {
        int stack = int(item.titleText.height + item.descText.height + item.descText2.height);
        int top = middle - stack / 2;
        Argb descColor = item.warning ? colors_.warning : 0;
        canvas_.DrawText(item.titleText, textX, top, palette_);
        canvas_.DrawText(item.descText, textX, top + int(item.titleText.height), palette_, descColor);
        canvas_.DrawText(item.descText2, textX, top + int(item.titleText.height + item.descText.height), palette_, descColor);
    }
    if (link) canvas_.DrawText(item.chevronText, r.right - D(CardPad) - int(item.chevronText.width), middle - int(item.chevronText.height) / 2, palette_);
    for (size_t c = 0; c < item.controls.size(); c++) PaintControl(item, index, item.controls[c], int(c), dy);
}

void SettingsWindow::PaintControl(Item&, int itemIndex, Control& c, int controlIndex, int dy)
{
    RECT r = Offset(c.rect, dy);
    Target self{-1, itemIndex, controlIndex};
    bool hot = hover_ == self && c.enabled, down = hot && pressed_ == self;
    bool open = popupFor_ == self, recording = recording_ == self;
    float radius = float(D(Radius));
    const Lang& lang = Lang::Get();
    canvas_.opacity = c.enabled ? 1.0f : 0.45f;
    int middle = (r.top + r.bottom) / 2;

    if (!c.rendered)
    {
        c.blocks.clear();
        switch (c.type)
        {
        case Control::Type::Toggle: c.blocks.push_back(Text(c.on ? lang.on : lang.off, Body, false, Ink::Primary)); break;
        case Control::Type::Combo:
            c.blocks.push_back(Text(c.text, Body, false, Ink::Primary, float(r.right - r.left - D(46))));
            c.blocks.push_back(Text(L"\xE70D", 12, false, Ink::Secondary, 0, Font::Icons));
            break;
        case Control::Type::Button: c.blocks.push_back(Text(c.text, Body, false, c.accent ? Ink::White : Ink::Primary)); break;
        case Control::Type::IconButton: c.blocks.push_back(Text(c.text, 16, false, Ink::Primary, 0, Font::Icons)); break;
        case Control::Type::Hotkey:
            c.blocks.push_back(Text(L"\xE70F", 14, false, Ink::Secondary, 0, Font::Icons));
            if (recording) c.blocks.push_back(Text(lang.keyRecording, Body, false, Ink::Secondary));
            else if (c.keys.empty()) c.blocks.push_back(Text(lang.keyNone, Body, false, Ink::Secondary));
            else
                for (const std::wstring& key : c.keys) c.blocks.push_back(Text(key, Caption, true, Ink::White));
            break;
        case Control::Type::Keys:
            for (const std::wstring& key : c.keys) c.blocks.push_back(Text(key, Caption, false, Ink::Primary));
            break;
        case Control::Type::Label: c.blocks.push_back(Text(c.text, Body, false, Ink::Secondary)); break;
        }
        c.rendered = true;
    }

    switch (c.type)
    {
    case Control::Type::Toggle:
    {
        float sx1 = float(r.right), sx0 = sx1 - D(ToggleWidth);
        float sy0 = float(middle - D(ToggleHeight) / 2), sy1 = sy0 + D(ToggleHeight);
        // The knob slides over after a click.
        float position = c.on ? 1.0f : 0.0f;
        for (const Anim& anim : anims_)
            if (anim.act == c.act && anim.arg == c.arg)
            {
                float t = float(std::clamp((PreciseMs() - anim.start) / ToggleMs, 0.0, 1.0));
                t = 1 - (1 - t) * (1 - t);
                position = c.on ? t : 1 - t;
            }
        bool lit = position >= 0.5f;
        if (lit)
        {
            canvas_.FillRoundRect(sx0, sy0, sx1, sy1, float(D(ToggleHeight)) / 2, down ? colors_.accentPressed : hot ? colors_.accentHover : colors_.accent);
        }
        else
        {
            canvas_.FillRoundRect(sx0, sy0, sx1, sy1, float(D(ToggleHeight)) / 2, hot ? colors_.toggleOffHover : colors_.toggleOff);
            canvas_.StrokeRoundRect(sx0, sy0, sx1, sy1, float(D(ToggleHeight)) / 2, 1, colors_.strong);
        }
        float knob = float(D(hot ? 14.0f : 12.0f)) / 2;
        float cx = sx0 + D(10) + position * D(20), cy = float(middle);
        Argb knobColor = lit ? colors_.onAccent : palette_.ink[int(Ink::Secondary)];
        canvas_.FillRoundRect(cx - knob, cy - knob, cx + knob, cy + knob, knob, knobColor);
        const TextBlock& label = c.blocks[0];
        canvas_.DrawText(label, int(sx0) - D(12) - int(std::ceil(label.width)), middle - int(label.height) / 2, palette_);
        break;
    }
    case Control::Type::Combo:
    case Control::Type::Button:
    {
        Argb fill = c.accent ? (down ? colors_.accentPressed : hot ? colors_.accentHover : colors_.accent)
                             : (down || open ? colors_.controlPressed : hot ? colors_.controlHover : colors_.control);
        canvas_.FillRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, fill);
        if (!c.accent) canvas_.StrokeRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, 1, colors_.controlStroke);
        const TextBlock& text = c.blocks[0];
        if (c.type == Control::Type::Button)
        {
            canvas_.DrawText(text, (r.left + r.right - int(text.width)) / 2, middle - int(text.height) / 2, palette_);
        }
        else
        {
            canvas_.DrawText(text, r.left + D(11), middle - int(text.height) / 2, palette_);
            const TextBlock& chevron = c.blocks[1];
            canvas_.DrawText(chevron, r.right - D(11) - int(chevron.width), middle - int(chevron.height) / 2 + (down ? D(1) : 0), palette_);
        }
        break;
    }
    case Control::Type::IconButton:
    {
        if (hot) canvas_.FillRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, down ? colors_.controlPressed : colors_.navHover);
        const TextBlock& glyph = c.blocks[0];
        canvas_.DrawText(glyph, (r.left + r.right - int(glyph.width)) / 2, middle - int(glyph.height) / 2, palette_);
        break;
    }
    case Control::Type::Hotkey:
    {
        Argb fill = down ? colors_.controlPressed : hot ? colors_.controlHover : colors_.control;
        canvas_.FillRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, fill);
        if (recording) canvas_.StrokeRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, float(D(2)), colors_.accent);
        else canvas_.StrokeRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, 1, colors_.controlStroke);
        const TextBlock& pencil = c.blocks[0];
        canvas_.DrawText(pencil, r.right - D(10) - int(pencil.width), middle - int(pencil.height) / 2, palette_);
        int x = r.left + D(6);
        if (recording || c.keys.empty())
        {
            const TextBlock& text = c.blocks[1];
            canvas_.DrawText(text, r.left + D(10), middle - int(text.height) / 2, palette_);
            break;
        }
        for (size_t k = 1; k < c.blocks.size(); k++)
        {
            const TextBlock& key = c.blocks[k];
            int w = int(std::ceil(key.width)) + D(16), h = D(CapHeight);
            int top = middle - h / 2;
            canvas_.FillRoundRect(float(x), float(top), float(x + w), float(top + h), radius, colors_.accent);
            canvas_.DrawText(key, x + D(8), middle - int(key.height) / 2, palette_);
            x += w + D(4);
        }
        break;
    }
    case Control::Type::Keys:
    {
        int x = r.left;
        for (const TextBlock& key : c.blocks)
        {
            int w = int(std::ceil(key.width)) + D(16);
            canvas_.FillRoundRect(float(x), float(r.top), float(x + w), float(r.bottom), radius, colors_.control);
            canvas_.StrokeRoundRect(float(x), float(r.top), float(x + w), float(r.bottom), radius, 1, colors_.controlStroke);
            canvas_.DrawText(key, x + D(8), middle - int(key.height) / 2, palette_);
            x += w + D(4);
        }
        break;
    }
    case Control::Type::Label:
    {
        const TextBlock& text = c.blocks[0];
        canvas_.DrawText(text, r.right - int(std::ceil(text.width)), middle - int(text.height) / 2, palette_);
        break;
    }
    }
    canvas_.opacity = 1;
}

void SettingsWindow::PaintFocus(int dy)
{
    if (!focusVisible_ || focus_ < 0) return;
    std::vector<Target> list = Focusables();
    if (focus_ >= int(list.size())) return;
    const Target& t = list[size_t(focus_)];
    RECT r{};
    if (t.nav >= 0)
    {
        int top = D(NavTop) + t.nav * (D(NavItem) + D(NavGap));
        r = RECT{D(NavPad), top, NavWidth() - D(NavPad), top + D(NavItem)};
    }
    else
    {
        const Item& item = items_[size_t(t.item)];
        r = Offset(t.control >= 0 ? item.controls[size_t(t.control)].rect : item.rect, dy);
        if (t.control >= 0 && item.controls[size_t(t.control)].type == Control::Type::Toggle) r.left = r.right - D(ToggleWidth);
        canvas_.SetClip(Viewport());
    }
    int grow = D(3);
    canvas_.StrokeRoundRect(float(r.left - grow), float(r.top - grow), float(r.right + grow), float(r.bottom + grow), float(D(Radius) + grow), float(D(2)),
                            colors_.focus);
    canvas_.ResetClip();
}

void SettingsWindow::PaintPopup(HDC hdc)
{
    RECT client;
    GetClientRect(popup_, &client);
    if (!popupCanvas_.Resize(client.right, client.bottom)) return;
    popupCanvas_.ResetClip();
    popupCanvas_.FillBackground(client, colors_.popup);
    Control* combo = ControlAt(popupFor_);
    if (combo)
    {
        for (size_t i = 0; i < combo->options.size() && i < popupText_.size(); i++)
        {
            int top = D(PopupPad) + int(i) * D(PopupItem);
            RECT r{D(PopupPad), top + D(2), client.right - D(PopupPad), top + D(PopupItem) - D(2)};
            bool selected = int(i) == combo->selected, hot = int(i) == popupHover_;
            Argb fill = hot ? colors_.cardHover : selected ? colors_.navSelected : 0;
            if (fill) popupCanvas_.FillRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), float(D(Radius)), fill);
            if (selected)
            {
                float middle = float(r.top + r.bottom) / 2;
                popupCanvas_.FillRoundRect(float(r.left), middle - float(D(8)), float(r.left + D(3)), middle + float(D(8)), float(D(1.5f)), colors_.accent);
            }
            const TextBlock& text = popupText_[i];
            popupCanvas_.DrawText(text, r.left + D(12), (r.top + r.bottom - int(text.height)) / 2, palette_);
        }
    }
    BitBlt(hdc, 0, 0, client.right, client.bottom, popupCanvas_.Dc(), 0, 0, SRCCOPY);
}

// ---------------------------------------------------------------- input

SettingsWindow::Target SettingsWindow::HitTest(int x, int y) const
{
    Target t;
    if (x < NavWidth())
    {
        for (int i = 0; i < int(Page::Count); i++)
        {
            int top = D(NavTop) + i * (D(NavItem) + D(NavGap));
            if (y >= top && y < top + D(NavItem) && x >= D(NavPad) && x < NavWidth() - D(NavPad)) t.nav = i;
        }
        return t;
    }
    RECT vp = Viewport();
    if (y < vp.top) return t;
    int cy = y - vp.top + scroll_;
    for (size_t i = 0; i < items_.size(); i++)
    {
        const Item& item = items_[i];
        if (!Inside(item.rect, x, cy)) continue;
        for (size_t c = 0; c < item.controls.size(); c++)
        {
            const Control& control = item.controls[c];
            if (control.type == Control::Type::Keys || control.type == Control::Type::Label) continue;
            if (Inside(control.rect, x, cy)) return Target{-1, int(i), int(c)};
        }
        if (item.act != Act::None) t.item = int(i);
        return t;
    }
    return t;
}

void SettingsWindow::SetHover(const Target& target)
{
    if (target == hover_) return;
    hover_ = target;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

SettingsWindow::Control* SettingsWindow::ControlAt(const Target& target)
{
    if (target.item < 0 || target.item >= int(items_.size())) return nullptr;
    Item& item = items_[size_t(target.item)];
    if (target.control < 0 || target.control >= int(item.controls.size())) return nullptr;
    return &item.controls[size_t(target.control)];
}

std::vector<SettingsWindow::Target> SettingsWindow::Focusables() const
{
    std::vector<Target> list;
    for (int i = 0; i < int(Page::Count); i++) list.push_back(Target{i, -1, -1});
    for (size_t i = 0; i < items_.size(); i++)
    {
        const Item& item = items_[i];
        if (item.act != Act::None) list.push_back(Target{-1, int(i), -1});
        for (size_t c = 0; c < item.controls.size(); c++)
        {
            const Control& control = item.controls[c];
            bool interactive = control.type != Control::Type::Keys && control.type != Control::Type::Label;
            if (interactive && (control.enabled || control.type == Control::Type::Hotkey)) list.push_back(Target{-1, int(i), int(c)});
        }
    }
    return list;
}

void SettingsWindow::MoveFocus(int delta)
{
    std::vector<Target> list = Focusables();
    if (list.empty()) return;
    int count = int(list.size());
    focus_ = focus_ < 0 ? (delta > 0 ? 0 : count - 1) : (focus_ + delta + count) % count;
    focusVisible_ = true;
    EnsureFocusVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::EnsureFocusVisible()
{
    std::vector<Target> list = Focusables();
    if (focus_ < 0 || focus_ >= int(list.size()) || list[size_t(focus_)].item < 0) return;
    const RECT& r = items_[size_t(list[size_t(focus_)].item)].rect;
    RECT vp = Viewport();
    int view = vp.bottom - vp.top;
    if (r.top - D(8) < scroll_) ScrollTo(r.top - D(8));
    else if (r.bottom + D(8) > scroll_ + view) ScrollTo(r.bottom + D(8) - view);
}

void SettingsWindow::ScrollTo(int value)
{
    value = std::clamp(value, 0, MaxScroll());
    if (value == scroll_) return;
    scroll_ = value;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::Navigate(Page page)
{
    ClosePopup(false);
    StopRecording();
    errorFor_ = Act::None;
    page_ = page;
    scroll_ = 0;
    Rebuild();
}

void SettingsWindow::Activate(const Target& target)
{
    if (target.nav >= 0)
    {
        Navigate(Page(target.nav));
        return;
    }
    if (target.item < 0 || target.item >= int(items_.size())) return;
    Item& item = items_[size_t(target.item)];
    if (target.control < 0)
    {
        if (item.act != Act::None) Act_(item.act, item.arg);
        return;
    }
    Control& c = item.controls[size_t(target.control)];
    if (!c.enabled && c.type != Control::Type::Hotkey) return;
    switch (c.type)
    {
    case Control::Type::Toggle:
        anims_.push_back(Anim{c.act, c.arg, PreciseMs()});
        SetTimer(hwnd_, TimerAnim, USER_TIMER_MINIMUM, nullptr);
        Act_(c.act, c.arg, c.on ? 0 : 1);
        break;
    case Control::Type::Combo: OpenPopup(target); break;
    case Control::Type::Button:
    case Control::Type::IconButton: Act_(c.act, c.arg); break;
    case Control::Type::Hotkey: StartRecording(target); break;
    default: break;
    }
}

// Every change goes to the app at once; the page is then rebuilt from the new settings.
void SettingsWindow::Act_(Act act, int arg, int option)
{
    Settings s = host_->CurrentSettings();
    auto erase = [&](std::vector<std::wstring>& list)
    {
        if (arg >= 0 && arg < int(list.size())) list.erase(list.begin() + arg);
    };
    auto add = [&](std::vector<std::wstring>& list, bool rootAllowed)
    {
        std::wstring folder;
        if (!PickFolder(folder)) return false;
        if (!rootAllowed && folder.size() <= 3) return false; // a whole drive can be excluded, not searched like a desktop
        for (const std::wstring& existing : list)
            if (EqualsNoCase(existing, folder)) return false;
        list.push_back(folder);
        return true;
    };
    switch (act)
    {
    case Act::Autostart:
        Autostart::Set(option == 1);
        Rebuild();
        return;
    case Act::TrayIcon: s.trayIcon = option == 1; break;
    case Act::Language: s.language = option; break;
    case Act::ResetSettings: s = Settings{}; break;
    case Act::DesktopKeyOn: s.desktopKeyOn = option == 1; break;
    case Act::GlobalKeyOn: s.globalKeyOn = option == 1; break;
    case Act::GroupShow: s.show[arg] = option == 1; break;
    case Act::GroupLimit: s.limit[arg] = Settings::LimitChoices[size_t(option)]; break;
    case Act::GroupUp:
        if (arg > 0) std::swap(s.order[arg], s.order[arg - 1]);
        break;
    case Act::GroupDown:
        if (arg < 2) std::swap(s.order[arg], s.order[arg + 1]);
        break;
    case Act::HiddenFiles: s.hiddenFiles = option; break;
    case Act::DevFolders: s.hideDevFolders = option == 1; break;
    case Act::SystemFolders: s.hideSystemFolders = option == 1; break;
    case Act::AddExtra:
        if (!add(s.extraFolders, false)) return;
        break;
    case Act::RemoveExtra: erase(s.extraFolders); break;
    case Act::AddExcluded:
        if (!add(s.excludedFolders, true)) return;
        break;
    case Act::RemoveExcluded: erase(s.excludedFolders); break;
    case Act::BoostOpened: s.boostOpened = option == 1; break;
    case Act::RecentWhenEmpty: s.recentWhenEmpty = option == 1; break;
    case Act::Theme: s.theme = option; break;
    case Act::Width: s.width = option; break;
    case Act::Monitor: s.monitor = option; break;
    case Act::Transparency: s.transparency = option == 1; break;
    case Act::Animations: s.animations = option == 1; break;
    case Act::KeyHints: s.keyHints = option == 1; break;
    case Act::ClearHistory:
        host_->ClearHistory();
        Rebuild();
        return;
    case Act::ResetStats:
        host_->ResetStats();
        Rebuild();
        return;
    case Act::OpenDataFolder: ShellExecuteW(hwnd_, L"open", LocalDataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return;
    case Act::OpenUrl:
        if (arg >= 0 && arg < int(std::size(Urls))) ShellExecuteW(hwnd_, L"open", Urls[arg], nullptr, nullptr, SW_SHOWNORMAL);
        return;
    default: return;
    }
    host_->ApplySettings(s); // the app calls Refresh() back when the look or the language changed
    Rebuild();
}

bool SettingsWindow::PickFolder(std::wstring& folder)
{
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (dialog->Show(hwnd_) != S_OK) return false;
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return false;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return false;
    folder = path;
    CoTaskMemFree(path);
    return !folder.empty();
}

// ---------------------------------------------------------------- drop-downs

void SettingsWindow::OpenPopup(const Target& target)
{
    ClosePopup(false);
    Control* combo = ControlAt(target);
    if (!combo || combo->options.empty()) return;
    RECT vp = Viewport();
    RECT r = Offset(combo->rect, vp.top - scroll_);
    POINT corner{r.left, r.top};
    ClientToScreen(hwnd_, &corner);

    popupText_.clear();
    float widest = 0;
    for (const std::wstring& option : combo->options)
    {
        popupText_.push_back(Text(option, Body, false, Ink::Primary));
        widest = std::max(widest, popupText_.back().width);
    }
    int width = std::max(int(r.right - r.left) + D(PopupPad) * 2, int(widest) + D(48));
    int count = int(combo->options.size());
    int height = D(PopupPad) * 2 + count * D(PopupItem);
    // The selected option opens over the box itself, as in Windows 11.
    int x = corner.x - D(PopupPad);
    int y = corner.y - D(PopupPad) - combo->selected * D(PopupItem) + (int(r.bottom - r.top) - D(PopupItem)) / 2;
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info);
    x = std::clamp(x, int(info.rcWork.left), int(info.rcWork.right) - width);
    y = std::clamp(y, int(info.rcWork.top), int(info.rcWork.bottom) - height);

    popupFor_ = target;
    popupHover_ = combo->selected;
    popup_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, PopupClass, L"", WS_POPUP, x, y, width, height, hwnd_, nullptr,
                             GetModuleHandleW(nullptr), this);
    if (!popup_)
    {
        popupFor_ = {};
        return;
    }
    BOOL dark = colors_.dark;
    DwmSetWindowAttribute(popup_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corner_ = DWMWCP_ROUND;
    DwmSetWindowAttribute(popup_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_, sizeof(corner_));
    if (transparency_)
    {
        DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TRANSIENTWINDOW;
        DwmSetWindowAttribute(popup_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
        MARGINS margins{-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(popup_, &margins);
    }
    ShowWindow(popup_, SW_SHOWNOACTIVATE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ClosePopup(bool apply)
{
    if (!popup_) return;
    Control* combo = ControlAt(popupFor_);
    Act act = combo ? combo->act : Act::None;
    int arg = combo ? combo->arg : 0, option = popupHover_;
    bool changed = combo && option >= 0 && option != combo->selected;
    HWND popup = popup_;
    popup_ = nullptr;
    popupFor_ = {};
    popupText_.clear();
    DestroyWindow(popup);
    popupCanvas_.Release();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (apply && changed) Act_(act, arg, option);
}

// ---------------------------------------------------------------- shortcut recording

void SettingsWindow::StartRecording(const Target& target)
{
    StopRecording();
    recording_ = target;
    errorFor_ = Act::None;
    recordError_.clear();
    host_->SuspendHotkeys(true); // so the old shortcut reaches this window like any other key
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::StopRecording()
{
    if (!recording_.Valid()) return;
    recording_ = {};
    host_->SuspendHotkeys(false);
    if (hwnd_)
    {
        Layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool SettingsWindow::Record(WPARAM key)
{
    Control* control = ControlAt(recording_);
    if (!control)
    {
        StopRecording();
        return false;
    }
    bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    bool win = GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;
    switch (key)
    {
    case VK_CONTROL:
    case VK_MENU:
    case VK_SHIFT:
    case VK_LWIN:
    case VK_RWIN:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LSHIFT:
    case VK_RSHIFT: return true; // waiting for the key itself
    case VK_ESCAPE:
        if (!ctrl && !alt && !shift && !win)
        {
            StopRecording();
            return true;
        }
        break;
    case VK_TAB:
        if (!ctrl && !alt && !win) return false; // Tab still moves on
        break;
    case VK_F4:
    case VK_SPACE:
        if (alt && !ctrl && !win)
        {
            // Alt+F4 closes and Alt+Space opens the window menu, as everywhere; neither becomes a shortcut.
            StopRecording();
            return false;
        }
        break;
    }
    UINT mods = (ctrl ? MOD_CONTROL : 0) | (alt ? MOD_ALT : 0) | (shift ? MOD_SHIFT : 0) | (win ? MOD_WIN : 0);
    bool function = key >= VK_F1 && key <= VK_F24;
    Act act = control->act;
    if (!(mods & ~MOD_SHIFT) && !function)
    {
        // A plain key (or Shift with one) would take that character away from every app.
        recordError_ = Lang::Get().keyNeedsModifier;
        errorFor_ = act;
    }
    else
    {
        Hotkey hotkey{mods, UINT(key)};
        if (!host_->HotkeyFree(hotkey))
        {
            recordError_ = Lang::Get().keyTaken;
            errorFor_ = act;
        }
        else
        {
            Settings s = host_->CurrentSettings();
            if (act == Act::GlobalKey)
            {
                s.globalKey = hotkey;
                s.globalKeyOn = true;
            }
            else
            {
                s.desktopKey = hotkey;
                s.desktopKeyOn = true;
            }
            errorFor_ = Act::None;
            recordError_.clear();
            recording_ = {};
            host_->SuspendHotkeys(false);
            host_->ApplySettings(s);
            Rebuild();
            return true;
        }
    }
    // Show why it didn't take, and keep listening.
    Rebuild();
    return true;
}

// ---------------------------------------------------------------- messages

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto* self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    // After WM_DESTROY hwnd_ is cleared: the last messages (WM_NCDESTROY) take the default path.
    return self && self->hwnd_ == hwnd ? self->Handle(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::PopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && hwnd == self->popup_) return self->HandlePopup(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandlePopup(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_DESTROY: // with its owner, when the settings window closes while it is open
        popup_ = nullptr;
        popupFor_ = {};
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(popup_, &ps);
        PaintPopup(hdc);
        EndPaint(popup_, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        int index = (GET_Y_LPARAM(lParam) - D(PopupPad)) / D(PopupItem);
        Control* combo = ControlAt(popupFor_);
        if (!combo || index < 0 || index >= int(combo->options.size())) index = -1;
        if (index != popupHover_ && index >= 0)
        {
            popupHover_ = index;
            InvalidateRect(popup_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        int index = (GET_Y_LPARAM(lParam) - D(PopupPad)) / D(PopupItem);
        Control* combo = ControlAt(popupFor_);
        if (combo && index >= 0 && index < int(combo->options.size()))
        {
            popupHover_ = index;
            ClosePopup(true);
        }
        return 0;
    }
    }
    return DefWindowProcW(popup_, msg, wParam, lParam);
}

LRESULT SettingsWindow::Handle(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);
        Paint(hdc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_SIZE:
        ClosePopup(false);
        if (!items_.empty()) Layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_MOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCRBUTTONDOWN: ClosePopup(false); break;
    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = POINT{D(720), D(520)};
        return 0;
    }
    case WM_DPICHANGED:
    {
        dpi_ = HIWORD(wParam);
        scale_ = float(dpi_) / 96.0f;
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Rebuild();
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            ClosePopup(false);
            StopRecording();
        }
        break;
    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (!tracking_)
        {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            tracking_ = TrackMouseEvent(&track) != FALSE;
        }
        if (dragThumb_)
        {
            RECT vp = Viewport();
            int track = vp.bottom - vp.top - D(8);
            int thumb = std::max(D(32), int(int64_t(track) * (vp.bottom - vp.top) / std::max(1, contentHeight_)));
            if (track > thumb) ScrollTo(int(int64_t(y - dragOffset_ - vp.top - D(4)) * MaxScroll() / (track - thumb)));
            return 0;
        }
        SetHover(HitTest(x, y));
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        SetHover(Target{});
        return 0;
    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (popup_)
        {
            ClosePopup(false);
            return 0;
        }
        if (recording_.Valid() && HitTest(x, y) != recording_) StopRecording();
        focusVisible_ = false;
        SetCapture(hwnd_);
        RECT client;
        GetClientRect(hwnd_, &client);
        if (MaxScroll() > 0 && x >= client.right - D(14) && y >= HeaderBottom())
        {
            RECT vp = Viewport();
            int track = vp.bottom - vp.top - D(8);
            int thumb = std::max(D(32), int(int64_t(track) * (vp.bottom - vp.top) / std::max(1, contentHeight_)));
            int top = vp.top + D(4) + int(int64_t(track - thumb) * scroll_ / MaxScroll());
            if (y >= top && y < top + thumb)
            {
                dragThumb_ = true;
                dragOffset_ = y - top;
            }
            else
            {
                ScrollTo(scroll_ + (y < top ? -1 : 1) * (vp.bottom - vp.top));
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        pressed_ = HitTest(x, y);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (dragThumb_)
        {
            dragThumb_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        Target target = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        Target pressed = pressed_;
        pressed_ = {};
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (target.Valid() && target == pressed)
        {
            std::vector<Target> list = Focusables();
            auto found = std::find(list.begin(), list.end(), target);
            if (found != list.end()) focus_ = int(found - list.begin());
            Activate(target);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        dragThumb_ = false;
        return 0;
    case WM_MOUSEWHEEL:
        ClosePopup(false);
        ScrollTo(scroll_ - GET_WHEEL_DELTA_WPARAM(wParam) * D(100) / WHEEL_DELTA);
        SetHover(Target{});
        return 0;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        if (recording_.Valid() && Record(wParam)) return 0;
        if (popup_)
        {
            Control* combo = ControlAt(popupFor_);
            int count = combo ? int(combo->options.size()) : 0;
            switch (wParam)
            {
            case VK_UP: popupHover_ = std::max(0, popupHover_ - 1); break;
            case VK_DOWN: popupHover_ = std::min(count - 1, popupHover_ + 1); break;
            case VK_HOME: popupHover_ = 0; break;
            case VK_END: popupHover_ = count - 1; break;
            case VK_RETURN:
            case VK_SPACE: ClosePopup(true); return 0;
            case VK_ESCAPE:
            case VK_TAB: ClosePopup(false); return 0;
            default: return 0;
            }
            InvalidateRect(popup_, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_SYSKEYDOWN) break; // Alt+F4, Alt+Space
        std::vector<Target> list = Focusables();
        Target focused = focus_ >= 0 && focus_ < int(list.size()) ? list[size_t(focus_)] : Target{};
        switch (wParam)
        {
        case VK_TAB: MoveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1); return 0;
        case VK_RETURN:
        case VK_SPACE:
            if (focused.Valid())
            {
                focusVisible_ = true;
                Activate(focused);
            }
            return 0;
        case VK_UP:
        case VK_DOWN:
            if (focused.nav >= 0)
            {
                int page = std::clamp(focused.nav + (wParam == VK_UP ? -1 : 1), 0, int(Page::Count) - 1);
                focus_ = page;
                focusVisible_ = true;
                Navigate(Page(page));
            }
            else
            {
                MoveFocus(wParam == VK_UP ? -1 : 1);
            }
            return 0;
        case VK_PRIOR: ScrollTo(scroll_ - (Viewport().bottom - Viewport().top)); return 0;
        case VK_NEXT: ScrollTo(scroll_ + (Viewport().bottom - Viewport().top)); return 0;
        case VK_HOME: ScrollTo(0); return 0;
        case VK_END: ScrollTo(MaxScroll()); return 0;
        }
        break;
    }
    case WM_SYSCHAR:
    case WM_SYSKEYUP:
        if (recording_.Valid()) return 0; // Alt in a shortcut must not open the window menu
        break;
    case WM_TIMER:
        if (wParam == TimerAnim)
        {
            double now = PreciseMs();
            anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [&](const Anim& a) { return now - a.start > ToggleMs + 20; }), anims_.end());
            if (anims_.empty()) KillTimer(hwnd_, TimerAnim);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) Refresh();
        break;
    case WM_DWMCOLORIZATIONCOLORCHANGED: Refresh(); break;
    case WM_CLOSE: DestroyWindow(hwnd_); return 0;
    case WM_DESTROY:
    {
        ClosePopup(false);
        StopRecording();
        KillTimer(hwnd_, TimerAnim);
        anims_.clear();
        items_.clear();
        items_.shrink_to_fit();
        navText_.clear();
        navGlyphs_.clear();
        appName_ = appVersion_ = pageTitle_ = TextBlock{};
        canvas_.Release();
        popupCanvas_.Release();
        appIcons_.clear();
        hwnd_ = nullptr;
        hover_ = pressed_ = {};
        tracking_ = focusVisible_ = dragThumb_ = false;
        focus_ = -1;
        if (host_) host_->SettingsClosed();
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}
