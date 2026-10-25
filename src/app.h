// The search window, tray icon and desktop hotkey: one window, one UI thread.
#pragma once
#include "common.h"

#include <unordered_map>

#include "canvas.h"
#include "icons.h"
#include "search.h"
#include "settings.h"
#include "settingsui.h"
#include "text.h"
#include "theme.h"
#include "tray.h"

class App : public SettingsHost
{
public:
    int Run(int start); // 0 in the background, 1 with the search box open, 2 with the settings open

    // SettingsHost
    const Settings& CurrentSettings() const override { return settings_; }
    void ApplySettings(const Settings& next) override;
    const Stats& CurrentStats() const override { return stats_; }
    void ResetStats() override;
    const History& OpenedHistory() override { return search_.Recent(); }
    void ClearHistory() override;
    std::wstring DescribeLocation(std::wstring_view dir) const override { return search_.DescribeLocation(dir); }
    size_t IndexEntries() const override { return search_.IndexEntries(); }
    size_t IndexBytes() const override { return search_.IndexReady() ? search_.IndexBytes() : 0; }
    bool GlobalKeyFailed() const override { return globalFailed_; }
    bool HotkeyFree(const Hotkey& key) override;
    void SuspendHotkeys(bool suspend) override;
    TextEngine& Text() override { return text_; }
    void SettingsClosed() override;

private:
    struct Row
    {
        bool header = false;
        Tier tier = Tier::Desktop;
        std::wstring title, count; // header
        Result result;             // item
        std::wstring display, location, kind, iconKey;
        std::vector<Span> spans;
        int top = 0, height = 0; // pixels, in list content coordinates

        // Rendered text, kept only while the row is near the viewport.
        bool rendered = false;
        int renderedWidth = 0;
        TextBlock first, second, third; // name/location/kind, or title/count
    };

    struct IconEntry
    {
        int size = 0;
        std::vector<uint32_t> pixels;
        uint64_t used = 0;
        bool loaded = false;
    };

    struct Editor
    {
        struct Snapshot
        {
            std::wstring text;
            int caret, anchor;
        };
        std::wstring text;
        int caret = 0, anchor = 0;
        std::vector<Snapshot> undo, redo;
        bool typing = false;
        int SelStart() const { return std::min(caret, anchor); }
        int SelEnd() const { return std::max(caret, anchor); }
        bool HasSelection() const { return caret != anchor; }
    };

    enum class Drag
    {
        None,
        Box,
        Row,
        Thumb,
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    LRESULT Handle(UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateMainWindow();
    void ApplyDwm();
    void ShowSearch();
    void HideSearch();
    void Dismiss();
    void ReleaseMemory();
    static void SetIdle(bool idle);
    void Exit();
    void OpenSettings();
    void UpdateHotkeys();
    void UpdateTray();
    void CountAnswer();

    // Layout, all in pixels at the current DPI.
    int D(float dip) const;
    int Line() const { return std::max(1, D(1)); }
    float WidthDip() const;
    HMONITOR TargetMonitor() const;
    void SetDpi(UINT dpi);
    void ResetText(); // every rendered text is stale: size, language or theme changed
    void LoadTheme();
    void Relayout();
    int ListHeight() const;
    int WindowHeight() const;
    void ApplyWindowSize();
    void PlaceOnCursorMonitor();
    RECT SearchRect() const;
    RECT ListRect() const;     // the list area, padding included
    RECT ViewportRect() const; // where rows are visible
    RECT FooterRect() const;
    int BoxLeft() const;
    int BoxRight() const;
    RECT GearRect() const;
    int RowsRight() const;
    bool HasScrollbar() const;
    RECT ThumbRect() const;
    int MaxScroll() const;

    // Search.
    void QueryChanged();
    void ApplyView();
    void UpdateStatus();
    void SetStatus(const std::wstring& text);
    void EverythingMissing();

    // Painting.
    void Paint(HDC hdc, const RECT& dirty);
    void DrawSearchRow();
    void DrawList();
    void DrawFooter();
    void RenderRow(Row& row, int width);
    void RenderFooter();
    const IconEntry* IconFor(const Row& row);
    void InvalidateList();
    void InvalidateSearchRow();

    // The search box.
    IDWriteTextLayout* BoxLayout();
    void BoxChanged(bool textChanged);
    void EnsureCaretVisible();
    float CaretX();
    int HitTestBox(int x);
    void Edit(const std::wstring& insert, bool typing);
    void DeleteRange(int start, int end);
    void Snapshot(bool typing);
    void Undo(bool redo);
    void MoveCaret(int position, bool extend);
    int WordLeft(int position) const;
    int WordRight(int position) const;
    void SelectWord(int position);
    void Paste();
    void CopySelection(bool cut);
    void UpdateImePosition();
    void RestartCaret();

    // The list.
    int RowAt(int x, int y) const;
    void Select(int index, bool scroll);
    void Move(int delta);
    void EnsureVisible(int index);
    void ScrollTo(int value);
    Row* Selected();
    void Open(const Result item);
    void Reveal(const Result item);
    void OpenInEverything();

    // Keys and mouse.
    bool OnKeyDown(WPARAM key);
    void OnChar(wchar_t c);
    void OnMouseDown(int x, int y, bool doubleClick);
    void OnMouseMove(int x, int y);
    void OnMouseUp(int x, int y);
    void OnWheel(int delta);
    void SetHover(int index);

    // Desktop-only Ctrl+F.
    void ForegroundChanged(HWND foreground);
    void OnHotkey();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;
    bool hotkeyRegistered_ = false;
    UINT taskbarCreated_ = 0;
    bool visible_ = false;
    bool exiting_ = false;
    HWND previous_ = nullptr; // foreground window before the search box opened
    Settings settings_;
    Stats stats_;
    SettingsWindow settingsWindow_;
    bool globalRegistered_ = false, globalFailed_ = false, hotkeysSuspended_ = false;
    bool typed_ = false; // something was typed since the box opened: a search to count
    uint32_t countedGeneration_ = 0;
    bool gearHover_ = false;
    TextBlock gearGlyph_;

    UINT dpi_ = 96;
    float scale_ = 1;
    TextEngine text_;
    Theme theme_;
    Canvas canvas_;
    Tray tray_;
    IconLoader icons_;
    Search search_;
    std::unordered_map<std::wstring, IconEntry> iconCache_;
    uint64_t iconUse_ = 0, paintIconUse_ = 0;

    // Search box.
    Editor editor_;
    IDWriteTextLayout* boxLayout_ = nullptr;
    TextBlock boxText_, placeholder_, glyph_;
    float boxScroll_ = 0;
    bool caretOn_ = true;
    float lineBox_ = 0, line14_ = 0, line12_ = 0;

    // Results.
    std::vector<Row> rows_;
    std::vector<std::wstring> terms_;
    int selected_ = -1, hover_ = -1, pressed_ = -1;
    int scroll_ = 0, contentHeight_ = 0;
    bool userMoved_ = false;
    std::wstring shownQuery_;
    uint32_t shownGeneration_ = 0;

    // Footer.
    std::wstring status_;
    TextBlock statusText_;
    bool statusRendered_ = false;
    struct Hint
    {
        TextBlock key, label;
    };
    std::vector<Hint> hints_;
    int hintsWidth_ = 0;

    // Mouse.
    Drag drag_ = Drag::None;
    int dragOffset_ = 0;
    bool tracking_ = false;

    // Opening animation.
    double animStart_ = 0;
    bool animating_ = false;

    // Everything start-up retry.
    uint64_t lastLaunch_ = 0;
    int retries_ = 0;
};
