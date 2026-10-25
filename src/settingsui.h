// The settings window: a Windows 11 Settings look (Mica, a navigation pane, cards with toggles, drop-downs and
// shortcut recorders), drawn with the same software renderer as the search box. It exists only while it is open.
#pragma once
#include "common.h"

#include "canvas.h"
#include "history.h"
#include "settings.h"
#include "text.h"

// What the window needs from the app; every change goes through ApplySettings and takes effect at once.
class SettingsHost
{
public:
    virtual const Settings& CurrentSettings() const = 0;
    virtual void ApplySettings(const Settings& next) = 0;
    virtual const Stats& CurrentStats() const = 0;
    virtual void ResetStats() = 0;
    virtual const History& OpenedHistory() = 0;
    virtual void ClearHistory() = 0;
    virtual std::wstring DescribeLocation(std::wstring_view dir) const = 0;
    virtual size_t IndexEntries() const = 0; // 0 while the desktop folder index isn't built
    virtual size_t IndexBytes() const = 0;
    virtual bool GlobalKeyFailed() const = 0;               // the saved "everywhere" shortcut couldn't be registered
    virtual bool HotkeyFree(const Hotkey& key) = 0;         // nobody else has registered it
    virtual void SuspendHotkeys(bool suspend) = 0;          // while a new shortcut is being typed
    virtual TextEngine& Text() = 0;
    virtual void SettingsClosed() = 0;

protected:
    ~SettingsHost() = default;
};

class SettingsWindow
{
public:
    void Open(SettingsHost* host);
    void Close();
    bool IsOpen() const { return hwnd_ != nullptr; }
    void Refresh(); // a setting changed elsewhere (the tray menu) or the system theme changed

private:
    enum class Page : uint8_t
    {
        General,
        Shortcuts,
        Search,
        Appearance,
        Stats,
        About,
        Count,
    };

    enum class Act : uint8_t
    {
        None,
        Autostart,
        TrayIcon,
        Language,
        ResetSettings,
        DesktopKeyOn,
        DesktopKey,
        GlobalKeyOn,
        GlobalKey,
        GroupShow,
        GroupLimit,
        GroupUp,
        GroupDown,
        HiddenFiles,
        DevFolders,
        SystemFolders,
        AddExtra,
        RemoveExtra,
        AddExcluded,
        RemoveExcluded,
        BoostOpened,
        RecentWhenEmpty,
        Theme,
        Width,
        Monitor,
        Transparency,
        Animations,
        KeyHints,
        ClearHistory,
        ResetStats,
        OpenDataFolder,
        OpenUrl,
    };

    struct Control
    {
        enum class Type : uint8_t
        {
            Toggle,
            Combo,
            Button,
            IconButton,
            Hotkey,
            Keys,  // key caps, only shown
            Label, // a short secondary text, only shown
        } type;
        Act act = Act::None;
        int arg = 0;
        bool on = false;
        bool enabled = true;
        bool accent = false;
        std::wstring text; // button text, icon glyph, label, or the combo's selection
        std::vector<std::wstring> options;
        int selected = 0;
        std::vector<std::wstring> keys;
        RECT rect{}; // content coordinates
        std::vector<TextBlock> blocks; // rendered text, built when first drawn
        bool rendered = false;
    };

    struct Item
    {
        enum class Kind : uint8_t
        {
            Section,
            Card,
            Row, // belongs to the card above it
            Tile,
            Hero,
            Note,
        } kind;
        std::wstring glyph, title, desc, value;
        bool warning = false;
        bool wrap = false;   // the description takes two lines
        Act act = Act::None; // a card that is a link
        int arg = 0;
        std::vector<Control> controls;
        RECT rect{};
        TextBlock glyphText, titleText, descText, descText2, valueText, chevronText;
        int renderedWidth = -1;
    };

    // What the mouse is over or the keyboard is on.
    struct Target
    {
        int nav = -1, item = -1, control = -1;
        bool operator==(const Target&) const = default;
        bool Valid() const { return nav >= 0 || item >= 0; }
    };

    struct Colors
    {
        bool dark = true;
        Argb background, layer, layerStroke, card, cardStroke, cardHover, control, controlHover, controlPressed, controlStroke;
        Argb strong, toggleOff, toggleOffHover, accent, accentHover, accentPressed, onAccent, navHover, navSelected, focus;
        Argb scrollThumb, warning, popup, divider;
    };

    struct Anim // a toggle's knob sliding over
    {
        Act act;
        int arg;
        double start;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandlePopup(UINT msg, WPARAM wParam, LPARAM lParam);

    // Building and laying out the current page.
    void Rebuild();
    void BuildGeneral();
    void BuildShortcuts();
    void BuildSearch();
    void BuildAppearance();
    void BuildStats();
    void BuildAbout();
    Item& AddSection(const wchar_t* title);
    Item& AddCard(const wchar_t* glyph, const std::wstring& title, const std::wstring& desc, Item::Kind kind = Item::Kind::Card);
    Control& AddControl(Item& item, Control::Type type, Act act, int arg = 0);
    void AddToggle(Item& item, Act act, bool on, int arg = 0, bool enabled = true);
    void AddCombo(Item& item, Act act, std::vector<std::wstring> options, int selected, int arg = 0);
    void AddButton(Item& item, Act act, const wchar_t* text, int arg = 0);
    void AddIcon(Item& item, Act act, const wchar_t* glyph, int arg = 0, bool enabled = true);
    void Layout();
    float ControlWidth(const Control& control, bool recording) const;

    // Painting.
    void LoadColors();
    void Paint(HDC hdc);
    void PaintNav();
    void PaintItem(Item& item, int index, int dy);
    void PaintControl(Item& item, int itemIndex, Control& control, int controlIndex, int dy);
    void PaintFocus(int dy);
    void PaintAppIcon(int x, int y, int size);
    void PaintPopup(HDC hdc);
    TextBlock Text(std::wstring_view text, float size, bool semibold, Ink ink, float maxWidth = 0, Font font = Font::Ui) const;

    // Input.
    Target HitTest(int x, int y) const;
    void SetHover(const Target& target);
    void Activate(const Target& target);
    void Act_(Act act, int arg, int option = -1);
    void MoveFocus(int delta);
    void EnsureFocusVisible();
    void ScrollTo(int value);
    void Navigate(Page page);
    std::vector<Target> Focusables() const;
    Control* ControlAt(const Target& target);
    void OpenPopup(const Target& target);
    void ClosePopup(bool apply);
    void StartRecording(const Target& target);
    void StopRecording();
    bool Record(WPARAM key);
    bool PickFolder(std::wstring& folder);

    // Geometry, in pixels at the window's DPI.
    int D(float dip) const { return int(dip * scale_ + 0.5f); }
    int NavWidth() const;
    int HeaderBottom() const;
    RECT Viewport() const;
    int MaxScroll() const;

    SettingsHost* host_ = nullptr;
    HWND hwnd_ = nullptr, popup_ = nullptr;
    Canvas canvas_, popupCanvas_;
    UINT dpi_ = 96;
    float scale_ = 1;
    Colors colors_{};
    Palette palette_;
    bool transparency_ = true;

    Page page_ = Page::General;
    std::vector<Item> items_;
    int contentHeight_ = 0, scroll_ = 0;
    Target hover_, pressed_;
    int focus_ = -1;
    bool focusVisible_ = false;
    bool tracking_ = false;
    bool dragThumb_ = false;
    int dragOffset_ = 0;
    std::vector<Anim> anims_;
    std::vector<TextBlock> navText_, navGlyphs_;
    TextBlock appName_, appVersion_, pageTitle_;
    std::vector<std::pair<int, std::vector<uint32_t>>> appIcons_; // by size

    // The open drop-down.
    Target popupFor_;
    int popupHover_ = -1;
    std::vector<TextBlock> popupText_;

    // The shortcut being recorded.
    Target recording_;
    std::wstring recordError_;
    Act errorFor_ = Act::None;
};
