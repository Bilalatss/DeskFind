#include "app.h"

#include <dwmapi.h>
#include <imm.h>
#include <objbase.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <cmath>
#include <ctime>

#include "lang.h"
#include "shell.h"
#include "version.h"

namespace
{
constexpr int HotkeyId = 0x4446, GlobalHotkeyId = 0x4447, TestHotkeyId = 0x4448;

enum : UINT_PTR
{
    TimerCaret = 1,
    TimerAnim,
    TimerTrim,
    TimerInterim,
    TimerEverything,
    TimerDefer,
};

// Sizes in device-independent pixels.
constexpr float SearchHeight = 60, ListMax = 456, ListPad = 6, FooterHeight = 36;
constexpr float RowPitch = 50, RowRadius = 6, HeaderTop = 10, HeaderBottom = 4;
constexpr float IconSize = 24, IconLeft = 14, TextLeft = 50, RowRight = 12, TextGap = 10;
constexpr float Track = 10, ThumbWidth = 4, ThumbMargin = 6, ThumbMin = 24;
constexpr float GlyphLeft = 20, GlyphGap = 14;
constexpr float BoxFont = 18, NameFont = 14, SmallFont = 12, KeyFont = 11.5f;
constexpr double FadeMs = 120, SlideMs = 180, SlideDistance = 6;
constexpr UINT InterimMs = 150, TrimDelayMs = 4000, DeferMs = 250; // a second letter usually comes within 250 ms
constexpr size_t MaxIcons = 128, MaxQueryLength = 400;

App* g_app = nullptr;

double EaseOut(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    double u = 1 - t;
    return 1 - u * u * u;
}

bool Pressed(int key) { return GetKeyState(key) < 0; }

bool Intersects(const RECT& a, const RECT& b) { return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom; }

RECT Intersect(const RECT& a, const RECT& b)
{
    return RECT{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

bool IsWordChar(wchar_t c) { return IsCharAlphaNumericW(c) != FALSE || c == L'_'; }

const wchar_t* TierTitle(Tier tier)
{
    const Lang& lang = Lang::Get();
    switch (tier)
    {
    case Tier::Desktop: return lang.desktop;
    case Tier::DesktopFolders: return lang.desktopFolders;
    case Tier::Elsewhere: return lang.elsewhere;
    default: return lang.recent;
    }
}

// First occurrence of every query word in the shown name.
std::vector<Span> Highlights(const std::wstring& display, const std::vector<std::wstring>& terms)
{
    std::wstring folded = Folded(display);
    std::vector<bool> marked(display.size(), false);
    for (const std::wstring& term : terms)
    {
        size_t at = folded.find(term);
        if (at == std::wstring::npos) continue;
        for (size_t i = at; i < at + term.size(); i++) marked[i] = true;
    }
    std::vector<Span> spans;
    for (size_t i = 0; i < marked.size();)
    {
        if (!marked[i])
        {
            i++;
            continue;
        }
        size_t start = i;
        while (i < marked.size() && marked[i]) i++;
        spans.push_back(Span{uint32_t(start), uint32_t(i - start)});
    }
    return spans;
}
} // namespace

int RunApp(int start)
{
    // Never destroyed: a worker still inside a slow shell or Everything call at exit must not touch freed memory.
    // The process ends right after, and Windows takes the memory back.
    static App* app = new App;
    return app->Run(start);
}

// ---------------------------------------------------------------- lifetime

int App::Run(int start)
{
    g_app = this;
    instance_ = GetModuleHandleW(nullptr);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\DeskFind.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // Launching the exe again just opens the search box (or the settings) of the running copy.
        if (HWND other = start ? FindWindowW(MainWindowClass, nullptr) : nullptr)
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, start == 2 ? WM_APP_SETTINGS : WM_APP_SHOW, 0, 0);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    settings_ = Settings::Load();
    stats_ = Stats::Load();
    if (!stats_.since) stats_.since = int64_t(std::time(nullptr));
    Lang::Set(settings_.language);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (!text_.Init() || !CreateMainWindow()) return 1;
    LoadTheme();
    search_.Init(hwnd_);
    search_.Configure(settings_.Search());
    icons_.Start(hwnd_);
    UpdateTray();
    Tray::SetMenuTheme(settings_.theme); // after the tray icon, whose creation allows dark menus again
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(hwnd_, taskbarCreated_, MSGFLT_ALLOW, nullptr);
    hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    UpdateHotkeys();
    if (Autostart::Initialize()) tray_.Notify(Lang::Get().welcomeTitle, Lang::Get().welcomeText);
    if (!Everything::IsRunning())
    {
        lastLaunch_ = NowMs();
        Everything::Launch(L"-startup");
    }
    SetTimer(hwnd_, TimerTrim, TrimDelayMs, nullptr);
    if (start == 1) ShowSearch();
    if (start == 2) OpenSettings();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (boxLayout_) boxLayout_->Release();
    text_.Shutdown();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}

bool App::CreateMainWindow()
{
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    wc.lpszClassName = MainWindowClass;
    if (!RegisterClassExW(&wc)) return false;
    // A captioned window, so DWM gives it a shadow, rounded corners and the backdrop; WM_NCCALCSIZE removes the caption.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, MainWindowClass, L"DeskFind", WS_POPUP | WS_CAPTION, 0, 0, 680, 100, nullptr, nullptr,
                            instance_, nullptr);
    if (!hwnd_) return false;
    dpi_ = 0;
    SetDpi(GetDpiForWindow(hwnd_));
    return true;
}

void App::ApplyDwm()
{
    BOOL dark = theme_.dark;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    DWM_SYSTEMBACKDROP_TYPE backdrop = settings_.transparency ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
}

void App::Exit()
{
    exiting_ = true;
    settingsWindow_.Close();
    HideSearch();
    stats_.Save();
    tray_.Destroy();
    if (hook_) UnhookWinEvent(hook_);
    if (hotkeyRegistered_) UnregisterHotKey(hwnd_, HotkeyId);
    UnregisterHotKey(hwnd_, GlobalHotkeyId);
    search_.Shutdown();
    icons_.Stop();
    DestroyWindow(hwnd_);
}

void App::ShowSearch()
{
    if (visible_)
    {
        SetForegroundWindow(hwnd_);
        MoveCaret(int(editor_.text.size()), false);
        editor_.anchor = 0;
        BoxChanged(false);
        return;
    }
#ifdef DF_TRACE
    double started = PreciseMs();
#endif
    SetIdle(false);
    KillTimer(hwnd_, TimerTrim);
    LoadTheme();
    HWND foreground = GetForegroundWindow();
    previous_ = foreground != hwnd_ ? foreground : nullptr;
    stats_.opens++;
    typed_ = false;

    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(TargetMonitor(), MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
    SetDpi(dpiX);

    // Whatever was typed last stays, selected, so typing replaces it; its results show at once from the cache.
    editor_.anchor = 0;
    editor_.caret = int(editor_.text.size());
    editor_.typing = false;
    boxScroll_ = 0;
    Everything::Boost(true);
    search_.Run(editor_.text, true);
    search_.PrepareIndex();
    shownGeneration_ = 0;
    userMoved_ = false;
    scroll_ = 0;
    ApplyView();

    PlaceOnCursorMonitor();
    UINT actual = GetDpiForWindow(hwnd_);
    if (actual != dpi_)
    {
        SetDpi(actual);
        Relayout();
        PlaceOnCursorMonitor();
    }
    ApplyDwm();
    EnsureCaretVisible();

    visible_ = true;
    animStart_ = PreciseMs();
    animating_ = settings_.animations;
    if (animating_) SetTimer(hwnd_, TimerAnim, USER_TIMER_MINIMUM, nullptr);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    UpdateWindow(hwnd_);
    RestartCaret();
    UpdateImePosition();
    TRACE(L"shown in %.2f ms (dpi %u, %zu rows)", PreciseMs() - started, dpi_, rows_.size());
}

// Esc: back to where the user was (usually the desktop, so Ctrl+F works again right away).
void App::Dismiss()
{
    HWND previous = previous_;
    previous_ = nullptr;
    if (previous && IsWindow(previous) && IsWindowVisible(previous) && SetForegroundWindow(previous)) return; // deactivation hides us
    HideSearch();
}

void App::HideSearch()
{
    if (!visible_) return;
    visible_ = false;
    ShowWindow(hwnd_, SW_HIDE);
    if (typed_ && !search_.View().query.empty()) stats_.CountSearch();
    typed_ = false;
    stats_.Save();
    KillTimer(hwnd_, TimerCaret);
    KillTimer(hwnd_, TimerAnim);
    KillTimer(hwnd_, TimerInterim);
    KillTimer(hwnd_, TimerDefer);
    animating_ = false;
    Everything::Boost(false);
    if (GetCapture() == hwnd_) ReleaseCapture();
    drag_ = Drag::None;
    hover_ = pressed_ = -1;
    tracking_ = false;
    icons_.Cancel();
    for (auto it = iconCache_.begin(); it != iconCache_.end();)
        it = it->second.loaded ? std::next(it) : iconCache_.erase(it);
    gearHover_ = false;
    SetTimer(hwnd_, TimerTrim, TrimDelayMs, nullptr);
}

void App::OpenSettings()
{
    HideSearch();
    KillTimer(hwnd_, TimerTrim);
    SetIdle(false);
    settingsWindow_.Open(this);
}

void App::SettingsClosed()
{
    if (!visible_) SetTimer(hwnd_, TimerTrim, TrimDelayMs, nullptr);
}

// Everything takes effect at once: what is shown is rebuilt in the new language, theme and size.
void App::ApplySettings(const Settings& next)
{
    Settings old = settings_;
    settings_ = next;
    settings_.Save();
    bool language = old.language != next.language;
    if (language) Lang::Set(next.language);
    search_.Configure(settings_.Search());
    LoadTheme();
    UpdateTray();
    Tray::SetMenuTheme(settings_.theme);
    UpdateHotkeys();
    ResetText();
    shownQuery_.clear();
    if (visible_)
    {
        search_.Run(editor_.text, true);
        search_.PrepareIndex();
        ApplyView();
        ApplyDwm();
        Relayout();
        PlaceOnCursorMonitor();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (language || old.theme != next.theme || old.transparency != next.transparency) settingsWindow_.Refresh();
}

// The tray icon, if wanted, with a tip that names the shortcut actually set.
void App::UpdateTray()
{
    if (!settings_.trayIcon)
    {
        tray_.Destroy();
        return;
    }
    std::wstring tip = L"DeskFind";
    if (settings_.desktopKeyOn && settings_.desktopKey.vk)
    {
        wchar_t text[128];
        swprintf_s(text, Lang::Get().trayTip, settings_.desktopKey.Text().c_str());
        tip = text;
    }
    else if (settings_.globalKeyOn && settings_.globalKey.vk)
    {
        tip += L" · " + settings_.globalKey.Text();
    }
    tray_.SetTip(tip);
    tray_.Create(hwnd_);
}

void App::ResetStats()
{
    stats_.Reset();
    stats_.Save();
}

void App::ClearHistory()
{
    search_.Recent().Clear();
    if (visible_) QueryChanged();
}

bool App::HotkeyFree(const Hotkey& key)
{
    if (!RegisterHotKey(hwnd_, TestHotkeyId, key.mods | MOD_NOREPEAT, key.vk)) return false;
    UnregisterHotKey(hwnd_, TestHotkeyId);
    return true;
}

void App::SuspendHotkeys(bool suspend)
{
    hotkeysSuspended_ = suspend;
    UpdateHotkeys();
}

// The "everywhere" shortcut is registered all the time; the desktop one only while the desktop is in front.
void App::UpdateHotkeys()
{
    UnregisterHotKey(hwnd_, GlobalHotkeyId);
    globalRegistered_ = globalFailed_ = false;
    if (settings_.globalKeyOn && settings_.globalKey.vk && !hotkeysSuspended_)
    {
        globalRegistered_ = RegisterHotKey(hwnd_, GlobalHotkeyId, settings_.globalKey.mods | MOD_NOREPEAT, settings_.globalKey.vk) != FALSE;
        globalFailed_ = !globalRegistered_;
    }
    if (hotkeyRegistered_)
    {
        UnregisterHotKey(hwnd_, HotkeyId);
        hotkeyRegistered_ = false;
    }
    ForegroundChanged(GetForegroundWindow());
}

void App::CountAnswer()
{
    const SearchView& view = search_.View();
    if (view.pending || view.query.empty() || view.generation == countedGeneration_) return;
    countedGeneration_ = view.generation;
    stats_.answers++;
    stats_.answerMs += view.elapsedMs;
}

// While hidden, nothing but the tray icon and the hotkey needs memory.
void App::ReleaseMemory()
{
    canvas_.Release();
    for (Row& row : rows_)
    {
        row.rendered = false;
        row.first = row.second = row.third = TextBlock{};
    }
    if (boxLayout_)
    {
        boxLayout_->Release();
        boxLayout_ = nullptr;
    }
    boxText_ = placeholder_ = glyph_ = statusText_ = TextBlock{};
    statusRendered_ = false;
    hints_.clear();
    hints_.shrink_to_fit();
    text_.ReleaseFormats();
    search_.ReleaseMemory();
    // Keep the shared type icons, drop the per-file ones.
    for (auto it = iconCache_.begin(); it != iconCache_.end();)
        it = it->first.size() > 0 && it->first[0] != L'*' && it->first[0] != L'<' ? iconCache_.erase(it) : std::next(it);
    HeapCompact(GetProcessHeap(), 0);
    SetIdle(true);
    SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
}

// Hidden, the app only waits for a hotkey: efficiency cores and low memory priority (its pages go first
// when Windows needs memory). Shown, it asks for full speed explicitly.
void App::SetIdle(bool idle)
{
    PROCESS_POWER_THROTTLING_STATE power{PROCESS_POWER_THROTTLING_CURRENT_VERSION};
    power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    power.StateMask = idle ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &power, sizeof(power));
    MEMORY_PRIORITY_INFORMATION memory{ULONG(idle ? MEMORY_PRIORITY_LOW : MEMORY_PRIORITY_NORMAL)};
    SetProcessInformation(GetCurrentProcess(), ProcessMemoryPriority, &memory, sizeof(memory));
}

// ---------------------------------------------------------------- layout

int App::D(float dip) const { return int(std::lround(dip * scale_)); }

float App::WidthDip() const
{
    static constexpr float widths[] = {560, 680, 820};
    return widths[std::clamp(settings_.width, 0, 2)];
}

HMONITOR App::TargetMonitor() const
{
    if (settings_.monitor == 1) return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    POINT cursor;
    GetCursorPos(&cursor);
    return MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
}

void App::SetDpi(UINT dpi)
{
    if (dpi == 0) dpi = 96;
    if (dpi == dpi_) return;
    dpi_ = dpi;
    scale_ = float(dpi) / 96.0f;
    ResetText();
}

void App::ResetText()
{
    text_.ReleaseFormats();
    for (Row& row : rows_) row.rendered = false;
    if (boxLayout_)
    {
        boxLayout_->Release();
        boxLayout_ = nullptr;
    }
    boxText_ = placeholder_ = glyph_ = statusText_ = gearGlyph_ = TextBlock{};
    statusRendered_ = false;
    hints_.clear();
    lineBox_ = line14_ = line12_ = 0;
}

void App::LoadTheme() { theme_.Load(text_.Gamma(), text_.GrayscaleContrast(), settings_.theme, settings_.transparency); }

void App::Relayout()
{
    if (lineBox_ == 0)
    {
        lineBox_ = text_.LineHeight({Font::Ui, BoxFont * scale_});
        line14_ = text_.LineHeight({Font::Ui, NameFont * scale_});
        line12_ = text_.LineHeight({Font::Ui, SmallFont * scale_});
    }
    if (glyph_.masks.empty()) glyph_ = text_.Render(L"", {Font::Icons, BoxFont * scale_, false, Ink::Secondary});
    int y = 0;
    for (Row& row : rows_)
    {
        row.top = y;
        row.height = row.header ? D(HeaderTop) + int(std::lround(line12_)) + D(HeaderBottom) : D(RowPitch);
        y += row.height;
    }
    contentHeight_ = y;
    scroll_ = std::clamp(scroll_, 0, MaxScroll());
}

int App::ListHeight() const
{
    if (rows_.empty()) return 0;
    return std::min(contentHeight_ + 2 * D(ListPad), D(ListMax));
}

int App::WindowHeight() const { return D(SearchHeight) + (rows_.empty() ? 0 : Line() + ListHeight()) + Line() + D(FooterHeight); }

void App::ApplyWindowSize()
{
    RECT r;
    GetWindowRect(hwnd_, &r);
    int width = D(WidthDip()), height = WindowHeight();
    if (r.right - r.left == width && r.bottom - r.top == height) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Centered, a fifth of the way down the monitor the mouse is on (the desktop the user just clicked).
void App::PlaceOnCursorMonitor()
{
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(TargetMonitor(), &info);
    const RECT& work = info.rcWork;
    int width = D(WidthDip()), height = WindowHeight();
    int x = work.left + (work.right - work.left - width) / 2;
    int y = work.top + (work.bottom - work.top) / 5;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_NOCOPYBITS);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

RECT App::SearchRect() const { return RECT{0, 0, D(WidthDip()), D(SearchHeight)}; }

RECT App::ListRect() const
{
    int top = D(SearchHeight) + Line();
    return RECT{0, top, D(WidthDip()), top + ListHeight()};
}

RECT App::ViewportRect() const
{
    RECT list = ListRect();
    return RECT{D(ListPad), list.top + D(ListPad), D(WidthDip()) - D(ListPad), list.bottom - D(ListPad)};
}

RECT App::FooterRect() const
{
    int height = WindowHeight();
    return RECT{0, height - D(FooterHeight) - Line(), D(WidthDip()), height};
}

int App::BoxLeft() const { return D(GlyphLeft) + int(std::ceil(glyph_.width)) + D(GlyphGap); }
int App::BoxRight() const { return GearRect().left - D(6); }

// The settings button at the right end of the search row.
RECT App::GearRect() const
{
    int size = D(32), right = D(WidthDip()) - D(14), top = (D(SearchHeight) - size) / 2;
    return RECT{right - size, top, right, top + size};
}

bool App::HasScrollbar() const
{
    RECT vp = ViewportRect();
    return !rows_.empty() && contentHeight_ > vp.bottom - vp.top;
}

int App::RowsRight() const { return D(WidthDip()) - D(ListPad) - (HasScrollbar() ? D(Track) : 0); }

int App::MaxScroll() const
{
    RECT vp = ViewportRect();
    return std::max(0, contentHeight_ - int(vp.bottom - vp.top));
}

RECT App::ThumbRect() const
{
    RECT vp = ViewportRect();
    int trackTop = vp.top + D(ThumbMargin), trackBottom = vp.bottom - D(ThumbMargin);
    int track = std::max(1, trackBottom - trackTop);
    int view = vp.bottom - vp.top;
    int thumb = std::max(D(ThumbMin), int(int64_t(track) * view / std::max(1, contentHeight_)));
    thumb = std::min(thumb, track);
    int max = MaxScroll();
    int top = trackTop + (max > 0 ? int(int64_t(track - thumb) * scroll_ / max) : 0);
    int right = D(WidthDip()) - D(ListPad) - (D(Track) - D(ThumbWidth)) / 2;
    return RECT{right - D(ThumbWidth), top, right, top + thumb};
}

// ---------------------------------------------------------------- search

void App::QueryChanged()
{
#ifdef DF_TRACE
    double started = PreciseMs();
#endif
    search_.Run(editor_.text, false);
    typed_ = true;
    if (search_.View().deferred) SetTimer(hwnd_, TimerDefer, DeferMs, nullptr);
    else KillTimer(hwnd_, TimerDefer);
    // What is known locally shows at once; Everything's part follows, the previous one bridging the wait.
    ApplyView();
    CountAnswer();
    TRACE(L"typed '%ls': local answer in %.2f ms, %zu rows%ls", editor_.text.c_str(), PreciseMs() - started, rows_.size(),
          search_.View().pending ? L", Everything pending" : L"");
}

void App::ApplyView()
{
    KillTimer(hwnd_, TimerInterim);
    const SearchView& view = search_.View();
    if (view.everythingMissing) EverythingMissing();

    bool sameQuery = view.query == shownQuery_;
    std::wstring keep;
    if (sameQuery && userMoved_)
        if (Row* row = Selected()) keep = row->result.path;

    std::vector<Row> rows;
    terms_ = Search::Terms(view.query);
    for (const TierBlock& block : view.tiers)
    {
        Row header;
        header.header = true;
        header.tier = block.tier;
        header.title = TierTitle(block.tier);
        if (block.tier != Tier::Recent) header.count = FormatCount(block.total);
        rows.push_back(std::move(header));
        for (const Result& result : block.items)
        {
            Row row;
            row.tier = block.tier;
            row.result = result;
            std::wstring_view name = result.Name();
            row.display = Search::DisplayName(name, result.folder);
            row.kind = result.folder ? Lang::Get().folder : row.display.size() != name.size() ? Lang::Get().shortcut : L"";
            row.location = search_.DescribeLocation(result.Dir());
            row.iconKey = IconLoader::KeyFor(result.path, result.folder);
            row.spans = Highlights(row.display, terms_);
            rows.push_back(std::move(row));
        }
    }

    // Revalidation usually brings the same answer: keep the rendered rows and only refresh the status.
    bool same = sameQuery && rows.size() == rows_.size();
    for (size_t i = 0; same && i < rows.size(); i++)
        same = rows[i].header == rows_[i].header && rows[i].count == rows_[i].count && rows[i].title == rows_[i].title &&
               rows[i].result.path == rows_[i].result.path;
    if (!same)
    {
        rows_ = std::move(rows);
        shownQuery_ = view.query;
        selected_ = -1;
        for (size_t i = 0; i < rows_.size(); i++)
        {
            if (rows_[i].header) continue;
            if (selected_ < 0) selected_ = int(i);
            if (!keep.empty() && rows_[i].result.path == keep)
            {
                selected_ = int(i);
                break;
            }
        }
        if (!sameQuery)
        {
            scroll_ = 0;
            userMoved_ = false;
        }
        hover_ = -1;
        pressed_ = -1;
        Relayout();
        if (visible_)
        {
            ApplyWindowSize();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    shownGeneration_ = view.generation;
    UpdateStatus();
}

void App::UpdateStatus()
{
    const SearchView& view = search_.View();
    const Lang& lang = Lang::Get();
    if (view.everythingMissing) return; // EverythingMissing() said what is going on
    if (view.query.empty())
    {
        SetStatus(lang.idleHint);
        return;
    }
    if (view.pending)
    {
        // A normal wait is a few frames: keep the last status rather than flash "searching".
        SetTimer(hwnd_, TimerInterim, InterimMs, nullptr);
        return;
    }
    KillTimer(hwnd_, TimerInterim);
    uint64_t total = 0;
    for (const TierBlock& block : view.tiers) total += block.total;
    if (total == 0)
    {
        SetStatus(lang.noResults);
        return;
    }
    wchar_t time[32];
    if (view.elapsedMs < 1) wcscpy_s(time, L"<1 ms");
    else swprintf_s(time, L"%d ms", int(std::lround(view.elapsedMs)));
    SetStatus(FormatCount(total) + L" " + (total == 1 ? lang.result : lang.results) + L" · " + time);
}

void App::SetStatus(const std::wstring& text)
{
    if (text == status_) return;
    status_ = text;
    statusRendered_ = false;
    if (visible_)
    {
        RECT footer = FooterRect();
        InvalidateRect(hwnd_, &footer, FALSE);
    }
}

// The desktop part is still shown; the rest follows by itself once Everything can answer.
void App::EverythingMissing()
{
    const Lang& lang = Lang::Get();
    if (Everything::IsRunning())
    {
        SetStatus(lang.everythingLoading);
    }
    else
    {
        if (NowMs() - lastLaunch_ > 10000)
        {
            lastLaunch_ = NowMs();
            if (!Everything::Launch(L"-startup"))
            {
                SetStatus(lang.everythingMissing);
                return;
            }
        }
        SetStatus(lang.everythingStarting);
    }
    retries_ = 0;
    SetTimer(hwnd_, TimerEverything, 250, nullptr);
}

// ---------------------------------------------------------------- painting

void App::InvalidateList()
{
    if (!visible_) return;
    RECT list = ListRect();
    InvalidateRect(hwnd_, &list, FALSE);
}

void App::InvalidateSearchRow()
{
    if (!visible_) return;
    RECT search = SearchRect();
    InvalidateRect(hwnd_, &search, FALSE);
}

void App::Paint(HDC hdc, const RECT& dirty)
{
#ifdef DF_TRACE
    double started = PreciseMs();
#endif
    RECT client;
    GetClientRect(hwnd_, &client);
    if (!canvas_.Resize(client.right, client.bottom)) return;
    RECT area = Intersect(dirty, client);
    canvas_.SetClip(area);
    canvas_.opacity = 1;
    canvas_.offsetY = 0;
    canvas_.FillBackground(area, theme_.tint);
    if (animating_)
    {
        double t = PreciseMs() - animStart_;
        canvas_.opacity = float(EaseOut(t / FadeMs));
        canvas_.offsetY = int(std::lround(SlideDistance * scale_ * (1 - EaseOut(t / SlideMs))));
    }
    if (glyph_.masks.empty() || lineBox_ == 0) Relayout();
    DrawSearchRow();
    DrawList();
    DrawFooter();
    canvas_.ResetClip();
    BitBlt(hdc, area.left, area.top, area.right - area.left, area.bottom - area.top, canvas_.Dc(), area.left, area.top, SRCCOPY);
    TRACE(L"paint %dx%d in %.2f ms%ls", area.right - area.left, area.bottom - area.top, PreciseMs() - started, animating_ ? L" (anim)" : L"");
}

void App::DrawSearchRow()
{
    RECT row = SearchRect();
    RECT outer = canvas_.Clip();
    if (!Intersects(row, outer)) return;
    const Palette& palette = theme_.palette;
    canvas_.DrawText(glyph_, D(GlyphLeft), (D(SearchHeight) - int(std::lround(glyph_.height))) / 2 + D(1), palette);

    int left = BoxLeft(), right = BoxRight();
    int top = int(std::lround((D(SearchHeight) - lineBox_) / 2)), lineHeight = int(std::lround(lineBox_));
    canvas_.SetClip(Intersect(outer, RECT{left - D(2), 0, right, D(SearchHeight)}));
    if (editor_.text.empty())
    {
        if (placeholder_.masks.empty()) placeholder_ = text_.Render(Lang::Get().placeholder, {Font::Ui, BoxFont * scale_, false, Ink::Tertiary});
        canvas_.DrawText(placeholder_, left + D(2), top, palette);
    }
    else
    {
        IDWriteTextLayout* layout = BoxLayout();
        int origin = left - int(std::lround(boxScroll_));
        if (layout && editor_.HasSelection())
        {
            UINT32 count = 0;
            layout->HitTestTextRange(editor_.SelStart(), editor_.SelEnd() - editor_.SelStart(), 0, 0, nullptr, 0, &count);
            std::vector<DWRITE_HIT_TEST_METRICS> ranges(count);
            if (count && SUCCEEDED(layout->HitTestTextRange(editor_.SelStart(), editor_.SelEnd() - editor_.SelStart(), 0, 0, ranges.data(), count, &count)))
                for (const auto& m : ranges)
                    canvas_.FillRect(origin + int(std::lround(m.left)), top, origin + int(std::lround(m.left + m.width)), top + lineHeight, theme_.selection);
        }
        canvas_.DrawText(boxText_, origin, top, palette);
        // Text running past an edge fades out there instead of being cut mid-letter.
        int fade = D(20);
        if (boxScroll_ > 0.5f) canvas_.FadeToBackground(left - D(2), top, left - D(2) + fade, top + lineHeight, theme_.tint, true);
        if (boxText_.width - boxScroll_ > float(right - left) + 0.5f) canvas_.FadeToBackground(right - fade, top, right, top + lineHeight, theme_.tint, false);
    }
    if (caretOn_ && !editor_.HasSelection())
    {
        int x = left + int(std::lround(CaretX() - boxScroll_));
        canvas_.FillRect(x, top, x + Line(), top + lineHeight, palette.ink[int(Ink::Primary)]);
    }
    canvas_.SetClip(outer);

    RECT gear = GearRect();
    if (gearHover_) canvas_.FillRoundRect(float(gear.left), float(gear.top), float(gear.right), float(gear.bottom), 5 * scale_, theme_.selected);
    if (gearGlyph_.masks.empty()) gearGlyph_ = text_.Render(L"\xE713", {Font::Icons, 16 * scale_, false, Ink::Tertiary});
    canvas_.DrawText(gearGlyph_, (gear.left + gear.right - int(gearGlyph_.width)) / 2, (gear.top + gear.bottom - int(gearGlyph_.height)) / 2, palette);
}

void App::RenderRow(Row& row, int width)
{
    if (row.rendered && row.renderedWidth == width) return;
    row.rendered = true;
    row.renderedWidth = width;
    TextStyle minor{Font::Ui, SmallFont * scale_, false, Ink::Tertiary};
    if (row.header)
    {
        row.first = text_.Render(row.title, {Font::Ui, SmallFont * scale_, true, Ink::Secondary});
        row.second = text_.Render(row.count, minor);
        return;
    }
    float kind = 0;
    if (!row.kind.empty())
    {
        row.third = text_.Render(row.kind, minor);
        kind = row.third.width + float(D(TextGap));
    }
    else
    {
        kind = float(D(TextGap));
    }
    float available = float(width - D(TextLeft) - D(RowRight)) - kind;
    row.first = text_.Render(row.display, {Font::Ui, NameFont * scale_, false, Ink::Primary}, available, &row.spans);
    row.second = text_.Render(row.location, {Font::Ui, SmallFont * scale_, false, Ink::Secondary}, available);
}

const App::IconEntry* App::IconFor(const Row& row)
{
    int size = D(IconSize);
    auto it = iconCache_.find(row.iconKey);
    if (it != iconCache_.end() && it->second.size == size)
    {
        it->second.used = ++iconUse_;
        return it->second.loaded ? &it->second : nullptr;
    }
    IconEntry& entry = iconCache_[row.iconKey];
    entry.size = size;
    entry.loaded = false;
    entry.pixels.clear();
    entry.used = ++iconUse_;
    icons_.Request(row.iconKey, row.result.path, row.result.folder, size);
    return nullptr;
}

void App::DrawList()
{
    if (rows_.empty()) return;
    RECT outer = canvas_.Clip();
    RECT list = ListRect();
    if (!Intersects(RECT{0, list.top - Line(), D(WidthDip()), list.bottom}, outer)) return;
    paintIconUse_ = iconUse_ + 1; // icons drawn from here on are on screen and must not be evicted
    canvas_.FillRect(0, list.top - Line(), D(WidthDip()), list.top, theme_.divider);

    RECT vp = ViewportRect();
    canvas_.SetClip(Intersect(outer, RECT{0, vp.top, D(WidthDip()), vp.bottom}));
    const Palette& palette = theme_.palette;
    int x0 = D(ListPad), x1 = RowsRight(), width = x1 - x0;
    int view = vp.bottom - vp.top;
    for (size_t i = 0; i < rows_.size(); i++)
    {
        Row& row = rows_[i];
        int top = vp.top + row.top - scroll_;
        if (top + row.height <= vp.top || top >= vp.bottom)
        {
            // Rows far off screen give their rendered text back.
            if (row.rendered && (row.top + row.height < scroll_ - view || row.top > scroll_ + 2 * view))
            {
                row.rendered = false;
                row.first = row.second = row.third = TextBlock{};
            }
            continue;
        }
        RenderRow(row, width);
        if (row.header)
        {
            int y = top + D(HeaderTop);
            canvas_.DrawText(row.first, x0 + D(12), y, palette);
            canvas_.DrawText(row.second, x1 - D(12) - int(std::ceil(row.second.width)), y, palette);
            continue;
        }
        int rowTop = top + D(1), rowBottom = top + row.height - D(1);
        bool selected = int(i) == selected_, hovered = int(i) == hover_;
        if (selected || hovered)
            canvas_.FillRoundRect(float(x0), float(rowTop), float(x1), float(rowBottom), RowRadius * scale_, selected ? theme_.selected : theme_.hover);
        if (selected)
        {
            float middle = float(rowTop + rowBottom) / 2;
            canvas_.FillRoundRect(float(x0), middle - 9 * scale_, float(x0) + 3 * scale_, middle + 9 * scale_, 1.5f * scale_, theme_.accentFill);
        }
        int iconSize = D(IconSize);
        if (const IconEntry* icon = IconFor(row); icon && !icon->pixels.empty())
            canvas_.DrawImage(icon->pixels.data(), icon->size, icon->size, x0 + D(IconLeft), rowTop + (rowBottom - rowTop - iconSize) / 2);

        int textX = x0 + D(TextLeft);
        int stack = int(std::lround(line14_ + float(D(1)) + line12_));
        int textY = rowTop + (rowBottom - rowTop - stack) / 2;
        canvas_.DrawText(row.first, textX, textY, palette);
        canvas_.DrawText(row.second, textX, textY + int(std::lround(line14_)) + D(1), palette);
        if (!row.kind.empty())
            canvas_.DrawText(row.third, x1 - D(RowRight) - int(std::ceil(row.third.width)), rowTop + (rowBottom - rowTop - int(std::lround(line12_))) / 2, palette);
    }
    if (HasScrollbar())
    {
        RECT thumb = ThumbRect();
        canvas_.FillRoundRect(float(thumb.left), float(thumb.top), float(thumb.right), float(thumb.bottom), 2 * scale_, theme_.scrollThumb);
    }
    canvas_.SetClip(outer);
}

void App::RenderFooter()
{
    if (!settings_.keyHints) hintsWidth_ = 0;
    if (hints_.empty() && settings_.keyHints)
    {
        const Lang& lang = Lang::Get();
        const wchar_t* pairs[][2] = {{L"↵", lang.hintOpen}, {L"Ctrl ↵", lang.hintReveal}, {L"Ctrl E", L"Everything"}, {L"Esc", lang.hintClose}};
        TextStyle style{Font::Ui, KeyFont * scale_, false, Ink::Tertiary};
        hintsWidth_ = 0;
        for (size_t i = 0; i < ARRAYSIZE(pairs); i++)
        {
            Hint hint{text_.Render(pairs[i][0], style), text_.Render(pairs[i][1], style)};
            hintsWidth_ += D(5) * 2 + 2 * Line() + int(std::ceil(hint.key.width)) + D(6) + int(std::ceil(hint.label.width));
            if (i + 1 < ARRAYSIZE(pairs)) hintsWidth_ += D(14);
            hints_.push_back(std::move(hint));
        }
    }
    if (!statusRendered_)
    {
        int available = D(WidthDip()) - D(16) - hintsWidth_ - D(12) - D(20);
        statusText_ = text_.Render(status_, {Font::Ui, SmallFont * scale_, false, Ink::Tertiary}, float(std::max(10, available)));
        statusRendered_ = true;
    }
}

void App::DrawFooter()
{
    RECT footer = FooterRect();
    if (!Intersects(footer, canvas_.Clip())) return;
    RenderFooter();
    const Palette& palette = theme_.palette;
    canvas_.FillRect(0, footer.top, D(WidthDip()), footer.top + Line(), theme_.divider);
    int top = footer.top + Line(), height = footer.bottom - top;
    canvas_.DrawText(statusText_, D(20), top + (height - int(std::lround(statusText_.height))) / 2, palette);

    int x = D(WidthDip()) - D(16) - hintsWidth_;
    for (size_t i = 0; i < hints_.size(); i++)
    {
        const Hint& hint = hints_[i];
        int keyWidth = int(std::ceil(hint.key.width)), keyHeight = int(std::lround(hint.key.height));
        int capWidth = D(5) * 2 + 2 * Line() + keyWidth, capHeight = keyHeight + D(1) + 2 * Line();
        int capTop = top + (height - capHeight) / 2;
        float radius = 4 * scale_;
        canvas_.FillRoundRect(float(x), float(capTop), float(x + capWidth), float(capTop + capHeight), radius, theme_.keyCapBg);
        canvas_.StrokeRoundRect(float(x), float(capTop), float(x + capWidth), float(capTop + capHeight), radius, float(Line()), theme_.keyCapBorder);
        canvas_.DrawText(hint.key, x + Line() + D(5), capTop + Line(), palette);
        x += capWidth + D(6);
        canvas_.DrawText(hint.label, x, top + (height - int(std::lround(hint.label.height))) / 2, palette);
        x += int(std::ceil(hint.label.width)) + D(14);
    }
}

// ---------------------------------------------------------------- search box

IDWriteTextLayout* App::BoxLayout()
{
    if (!boxLayout_ && !editor_.text.empty())
    {
        boxLayout_ = text_.CreateLayout(editor_.text, {Font::Ui, BoxFont * scale_, false, Ink::Primary}, 0, false);
        boxText_ = text_.Render(boxLayout_, Ink::Primary);
    }
    return boxLayout_;
}

void App::BoxChanged(bool textChanged)
{
    if (textChanged && boxLayout_)
    {
        boxLayout_->Release();
        boxLayout_ = nullptr;
        boxText_ = TextBlock{};
    }
    EnsureCaretVisible();
    RestartCaret();
    UpdateImePosition();
    InvalidateSearchRow();
    if (textChanged) QueryChanged();
}

float App::CaretX()
{
    IDWriteTextLayout* layout = BoxLayout();
    if (!layout) return 0;
    FLOAT x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout->HitTestTextPosition(UINT32(editor_.caret), FALSE, &x, &y, &metrics);
    return x;
}

void App::EnsureCaretVisible()
{
    float width = float(BoxRight() - BoxLeft());
    BoxLayout();
    float total = editor_.text.empty() ? 0 : boxText_.width;
    float caret = CaretX();
    if (caret - boxScroll_ > width - 2) boxScroll_ = caret - width + 2;
    if (caret - boxScroll_ < 0) boxScroll_ = caret;
    boxScroll_ = std::clamp(boxScroll_, 0.0f, std::max(0.0f, total - width + 2));
}

int App::HitTestBox(int x)
{
    IDWriteTextLayout* layout = BoxLayout();
    if (!layout) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout->HitTestPoint(float(x - BoxLeft()) + boxScroll_, lineBox_ / 2, &trailing, &inside, &metrics);
    return int(metrics.textPosition + (trailing ? metrics.length : 0));
}

void App::Snapshot(bool typing)
{
    if (typing && editor_.typing && !editor_.undo.empty()) return; // one undo step per run of typing
    editor_.undo.push_back({editor_.text, editor_.caret, editor_.anchor});
    if (editor_.undo.size() > 100) editor_.undo.erase(editor_.undo.begin());
    editor_.redo.clear();
    editor_.typing = typing;
}

void App::Edit(const std::wstring& insert, bool typing)
{
    std::wstring clean;
    for (wchar_t c : insert)
        if (c >= 0x20 && c != 0x7F) clean.push_back(c);
        else if (c == L'\t' || c == L'\n') clean.push_back(L' ');
    int start = editor_.SelStart(), end = editor_.SelEnd();
    size_t room = MaxQueryLength - std::min(MaxQueryLength, editor_.text.size() - size_t(end - start));
    if (clean.size() > room)
    {
        clean.resize(room);
        if (!clean.empty() && IS_HIGH_SURROGATE(clean.back())) clean.pop_back();
    }
    if (clean.empty() && start == end) return;
    Snapshot(typing);
    editor_.text.replace(size_t(start), size_t(end - start), clean);
    editor_.caret = editor_.anchor = start + int(clean.size());
    BoxChanged(true);
}

void App::DeleteRange(int start, int end)
{
    start = std::clamp(start, 0, int(editor_.text.size()));
    end = std::clamp(end, start, int(editor_.text.size()));
    if (start == end) return;
    Snapshot(false);
    editor_.text.erase(size_t(start), size_t(end - start));
    editor_.caret = editor_.anchor = start;
    BoxChanged(true);
}

void App::Undo(bool redo)
{
    auto& from = redo ? editor_.redo : editor_.undo;
    auto& to = redo ? editor_.undo : editor_.redo;
    if (from.empty()) return;
    to.push_back({editor_.text, editor_.caret, editor_.anchor});
    Editor::Snapshot state = std::move(from.back());
    from.pop_back();
    editor_.text = std::move(state.text);
    editor_.caret = state.caret;
    editor_.anchor = state.anchor;
    editor_.typing = false;
    BoxChanged(true);
}

void App::MoveCaret(int position, bool extend)
{
    int n = int(editor_.text.size());
    position = std::clamp(position, 0, n);
    // Never between the halves of a surrogate pair.
    if (position > 0 && position < n && IS_LOW_SURROGATE(editor_.text[position]) && IS_HIGH_SURROGATE(editor_.text[position - 1]))
        position += position > editor_.caret ? 1 : -1;
    editor_.caret = position;
    if (!extend) editor_.anchor = position;
    editor_.typing = false;
    BoxChanged(false);
}

int App::WordLeft(int position) const
{
    const std::wstring& t = editor_.text;
    while (position > 0 && !IsWordChar(t[position - 1])) position--;
    while (position > 0 && IsWordChar(t[position - 1])) position--;
    return position;
}

int App::WordRight(int position) const
{
    const std::wstring& t = editor_.text;
    int n = int(t.size());
    while (position < n && IsWordChar(t[position])) position++;
    while (position < n && !IsWordChar(t[position])) position++;
    return position;
}

void App::SelectWord(int position)
{
    const std::wstring& t = editor_.text;
    int n = int(t.size()), start = std::min(position, n), end = start;
    if (start < n && !IsWordChar(t[start]) && start > 0 && IsWordChar(t[start - 1])) start--, end--;
    while (start > 0 && IsWordChar(t[start - 1])) start--;
    while (end < n && IsWordChar(t[end])) end++;
    if (start == end && end < n)
    {
        end++;
        if (end < n && IS_HIGH_SURROGATE(t[end - 1]) && IS_LOW_SURROGATE(t[end])) end++; // a whole emoji, not half
    }
    editor_.anchor = start;
    editor_.caret = end;
    editor_.typing = false;
    BoxChanged(false);
}

void App::Paste()
{
    if (!OpenClipboard(hwnd_)) return;
    std::wstring text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
    {
        if (auto* chars = static_cast<const wchar_t*>(GlobalLock(data)))
        {
            text = chars;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
    if (!text.empty()) Edit(text, false);
}

void App::CopySelection(bool cut)
{
    if (!editor_.HasSelection()) return;
    ShellOps::CopyText(hwnd_, editor_.text.substr(size_t(editor_.SelStart()), size_t(editor_.SelEnd() - editor_.SelStart())));
    if (cut) DeleteRange(editor_.SelStart(), editor_.SelEnd());
}

void App::UpdateImePosition()
{
    if (!visible_) return;
    HIMC imc = ImmGetContext(hwnd_);
    if (!imc) return;
    COMPOSITIONFORM form{CFS_POINT};
    form.ptCurrentPos.x = BoxLeft() + int(std::lround(CaretX() - boxScroll_));
    form.ptCurrentPos.y = int(std::lround((D(SearchHeight) - lineBox_) / 2));
    ImmSetCompositionWindow(imc, &form);
    ImmReleaseContext(hwnd_, imc);
}

void App::RestartCaret()
{
    caretOn_ = true;
    if (!visible_) return;
    UINT blink = GetCaretBlinkTime();
    if (blink == INFINITE || blink == 0) KillTimer(hwnd_, TimerCaret);
    else SetTimer(hwnd_, TimerCaret, blink, nullptr);
}

// ---------------------------------------------------------------- list

App::Row* App::Selected()
{
    if (selected_ < 0 || selected_ >= int(rows_.size()) || rows_[selected_].header) return nullptr;
    return &rows_[selected_];
}

int App::RowAt(int x, int y) const
{
    RECT vp = ViewportRect();
    if (rows_.empty() || y < vp.top || y >= vp.bottom || x < D(ListPad) || x >= RowsRight()) return -1;
    int content = y - vp.top + scroll_;
    for (size_t i = 0; i < rows_.size(); i++)
        if (content >= rows_[i].top && content < rows_[i].top + rows_[i].height) return rows_[i].header ? -1 : int(i);
    return -1;
}

void App::ScrollTo(int value)
{
    value = std::clamp(value, 0, MaxScroll());
    if (value == scroll_) return;
    scroll_ = value;
    InvalidateList();
}

void App::EnsureVisible(int index)
{
    if (index < 0 || index >= int(rows_.size())) return;
    RECT vp = ViewportRect();
    int view = vp.bottom - vp.top;
    int top = rows_[index].top, bottom = top + rows_[index].height;
    // Keep the group title visible when stepping onto a group's first row.
    if (index > 0 && rows_[index - 1].header) top = rows_[index - 1].top;
    if (top < scroll_) ScrollTo(top);
    else if (bottom > scroll_ + view) ScrollTo(bottom - view);
}

void App::Select(int index, bool scroll)
{
    if (index == selected_) return;
    selected_ = index;
    userMoved_ = true;
    if (scroll) EnsureVisible(index);
    InvalidateList();
}

void App::Move(int delta)
{
    if (rows_.empty()) return;
    int step = delta > 0 ? 1 : -1, index = selected_;
    for (int remaining = std::abs(delta); remaining > 0; remaining--)
    {
        int next = index + step;
        while (next >= 0 && next < int(rows_.size()) && rows_[next].header) next += step;
        if (next < 0 || next >= int(rows_.size())) break;
        index = next;
    }
    if (index >= 0 && index != selected_) Select(index, true);
    else if (index >= 0) EnsureVisible(index);
}

void App::SetHover(int index)
{
    if (index == hover_) return;
    hover_ = index;
    InvalidateList();
}

// Takes a copy: the shell calls below wait in COM's modal loop, where an Everything reply can rebuild rows_.
void App::Open(const Result item)
{
    std::wstring error;
    AllowSetForegroundWindow(ASFW_ANY);
    if (ShellOps::Open(item.path, std::wstring(item.Dir()), error))
    {
        stats_.launches++;
        search_.Recent().Record(item.path, item.folder);
        HideSearch();
    }
    else
    {
        SetStatus(Lang::Get().openFailed + error);
    }
}

// Desktop items get their icon selected on the desktop itself; anything else opens Explorer on it.
void App::Reveal(const Result item)
{
    AllowSetForegroundWindow(ASFW_ANY);
    search_.Recent().Record(item.path, item.folder);
    stats_.launches++;
    if (search_.IsOnDesktop(item.Dir()) && ShellOps::SelectOnDesktop(item.path))
    {
        HideSearch();
        return;
    }
    std::wstring error;
    if (ShellOps::Reveal(item.path, error)) HideSearch();
    else SetStatus(Lang::Get().revealFailed + error);
}

void App::OpenInEverything()
{
    std::wstring query;
    for (wchar_t c : editor_.text)
    {
        if (c == L'"') query += L"\"\"";
        else query.push_back(c);
    }
    AllowSetForegroundWindow(ASFW_ANY);
    if (Everything::Launch(L"-s \"" + query + L"\"")) HideSearch();
    else SetStatus(Lang::Get().everythingNoOpen);
}

// ---------------------------------------------------------------- input

bool App::OnKeyDown(WPARAM key)
{
    bool ctrl = Pressed(VK_CONTROL), shift = Pressed(VK_SHIFT), alt = Pressed(VK_MENU);
    if (alt) return false; // Alt+F4 and AltGr characters go their usual way
    int n = int(editor_.text.size()), caret = editor_.caret;
    switch (key)
    {
    case VK_ESCAPE: Dismiss(); return true;
    case VK_DOWN: Move(1); return true;
    case VK_UP: Move(-1); return true;
    case VK_NEXT: Move(8); return true;
    case VK_PRIOR: Move(-8); return true;
    case VK_RETURN:
        if (Row* row = Selected())
        {
            if (ctrl) Reveal(row->result);
            else Open(row->result);
        }
        return true;
    case VK_LEFT:
        if (!shift && editor_.HasSelection()) MoveCaret(editor_.SelStart(), false);
        else MoveCaret(ctrl ? WordLeft(caret) : caret - 1, shift);
        return true;
    case VK_RIGHT:
        if (!shift && editor_.HasSelection()) MoveCaret(editor_.SelEnd(), false);
        else MoveCaret(ctrl ? WordRight(caret) : caret + 1, shift);
        return true;
    case VK_HOME: MoveCaret(0, shift); return true;
    case VK_END: MoveCaret(n, shift); return true;
    case VK_BACK:
        if (editor_.HasSelection()) DeleteRange(editor_.SelStart(), editor_.SelEnd());
        else if (caret > 0)
        {
            int from = ctrl ? WordLeft(caret) : caret - 1;
            if (!ctrl && from > 0 && IS_LOW_SURROGATE(editor_.text[from]) && IS_HIGH_SURROGATE(editor_.text[from - 1])) from--;
            DeleteRange(from, caret);
        }
        return true;
    case VK_DELETE:
        if (shift && !ctrl) CopySelection(true);
        else if (editor_.HasSelection()) DeleteRange(editor_.SelStart(), editor_.SelEnd());
        else if (caret < n)
        {
            int to = ctrl ? WordRight(caret) : caret + 1;
            if (!ctrl && to < n && IS_LOW_SURROGATE(editor_.text[to])) to++;
            DeleteRange(caret, to);
        }
        return true;
    case VK_INSERT:
        if (shift) Paste();
        else if (ctrl) CopySelection(false);
        return true;
    case VK_TAB: return true;
    case VK_F5:
        search_.Run(editor_.text, true);
        ApplyView();
        return true;
    }
    if (!ctrl) return false;
    switch (key)
    {
    case 'A':
        editor_.anchor = 0;
        editor_.caret = n;
        BoxChanged(false);
        return true;
    case 'C':
        if (shift)
        {
            if (Row* row = Selected())
                if (ShellOps::CopyText(hwnd_, row->result.path)) SetStatus(Lang::Get().pathCopied);
        }
        else
        {
            CopySelection(false);
        }
        return true;
    case 'X': CopySelection(true); return true;
    case 'V': Paste(); return true;
    case 'Z': Undo(shift); return true;
    case 'Y': Undo(true); return true;
    case 'E': OpenInEverything(); return true;
    case VK_OEM_COMMA: OpenSettings(); return true;
    case 'F': // Ctrl+F again: start a new search over the old text
        editor_.anchor = 0;
        editor_.caret = n;
        BoxChanged(false);
        return true;
    }
    return false;
}

void App::OnChar(wchar_t c)
{
    if (c < 0x20 || c == 0x7F) return; // control keys are handled in OnKeyDown
    if (Pressed(VK_CONTROL) && !Pressed(VK_MENU)) return;
    Edit(std::wstring(1, c), true);
}

void App::OnMouseDown(int x, int y, bool doubleClick)
{
    RECT gear = GearRect();
    if (x >= gear.left && x < gear.right && y >= gear.top && y < gear.bottom)
    {
        OpenSettings();
        return;
    }
    SetCapture(hwnd_);
    if (y < D(SearchHeight))
    {
        int position = HitTestBox(x);
        if (doubleClick)
        {
            SelectWord(position);
            drag_ = Drag::None;
            return;
        }
        MoveCaret(position, Pressed(VK_SHIFT));
        drag_ = Drag::Box;
        return;
    }
    RECT vp = ViewportRect();
    if (HasScrollbar() && x >= RowsRight() && y >= vp.top && y < vp.bottom)
    {
        RECT thumb = ThumbRect();
        if (y >= thumb.top && y < thumb.bottom)
        {
            drag_ = Drag::Thumb;
            dragOffset_ = y - thumb.top;
        }
        else
        {
            int view = vp.bottom - vp.top;
            ScrollTo(scroll_ + (y < thumb.top ? -view : view));
        }
        return;
    }
    int row = RowAt(x, y);
    if (row >= 0)
    {
        pressed_ = row;
        drag_ = Drag::Row;
        Select(row, false);
    }
}

void App::OnMouseMove(int x, int y)
{
    if (!tracking_)
    {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        tracking_ = TrackMouseEvent(&track) != FALSE;
    }
    switch (drag_)
    {
    case Drag::Box: MoveCaret(HitTestBox(x), true); return;
    case Drag::Thumb:
    {
        RECT vp = ViewportRect(), thumb = ThumbRect();
        int trackTop = vp.top + D(ThumbMargin), track = (vp.bottom - D(ThumbMargin)) - trackTop - (thumb.bottom - thumb.top);
        if (track > 0) ScrollTo(int(int64_t(y - dragOffset_ - trackTop) * MaxScroll() / track));
        return;
    }
    default:
    {
        RECT gear = GearRect();
        bool overGear = x >= gear.left && x < gear.right && y >= gear.top && y < gear.bottom;
        if (overGear != gearHover_)
        {
            gearHover_ = overGear;
            InvalidateSearchRow();
        }
        SetHover(RowAt(x, y));
    }
    }
}

void App::OnMouseUp(int x, int y)
{
    Drag drag = drag_;
    drag_ = Drag::None;
    if (GetCapture() == hwnd_) ReleaseCapture();
    if (drag != Drag::Row) return;
    int row = RowAt(x, y);
    if (row < 0 || row != pressed_) return;
    pressed_ = -1;
    if (Pressed(VK_CONTROL)) Reveal(rows_[row].result);
    else Open(rows_[row].result);
}

void App::OnWheel(int delta)
{
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == WHEEL_PAGESCROLL)
    {
        RECT vp = ViewportRect();
        ScrollTo(scroll_ - (delta > 0 ? 1 : -1) * int(vp.bottom - vp.top));
    }
    else
    {
        ScrollTo(scroll_ - delta * int(lines) * D(16) / WHEEL_DELTA);
    }
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(hwnd_, &p);
    SetHover(RowAt(p.x, p.y));
}

// ---------------------------------------------------------------- hotkey

void CALLBACK App::OnWinEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD)
{
    if (g_app) g_app->ForegroundChanged(hwnd);
}

// Ctrl+F is registered only while the desktop is the foreground window, so every other app keeps its own Ctrl+F.
void App::ForegroundChanged(HWND foreground)
{
    const Hotkey& key = settings_.desktopKey;
    // Not twice: when the everywhere shortcut is the same keys, it already covers the desktop.
    bool wanted = settings_.desktopKeyOn && key.vk && !hotkeysSuspended_ && !(globalRegistered_ && settings_.globalKey == key) &&
                  ShellOps::IsDesktopWindow(foreground);
    if (wanted == hotkeyRegistered_) return;
    if (wanted)
    {
        hotkeyRegistered_ = RegisterHotKey(hwnd_, HotkeyId, key.mods | MOD_NOREPEAT, key.vk) != FALSE;
    }
    else
    {
        UnregisterHotKey(hwnd_, HotkeyId);
        hotkeyRegistered_ = false;
    }
}

void App::OnHotkey()
{
    HWND foreground = GetForegroundWindow();
    if (ShellOps::IsDesktopWindow(foreground))
    {
        ShowSearch();
        return;
    }
    // Focus left the desktop before our foreground event arrived: give the keystroke back.
    ForegroundChanged(foreground);
    INPUT input[2] = {};
    input[0].type = input[1].type = INPUT_KEYBOARD;
    input[0].ki.wVk = input[1].ki.wVk = WORD(settings_.desktopKey.vk);
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(INPUT));
}

// ---------------------------------------------------------------- messages

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_app && (g_app->hwnd_ == hwnd || !g_app->hwnd_))
    {
        if (!g_app->hwnd_) g_app->hwnd_ = hwnd;
        return g_app->Handle(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::Handle(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCCALCSIZE:
        if (wParam) return 0; // the whole window is client area
        break;
    case WM_NCHITTEST: return HTCLIENT;
    case WM_NCACTIVATE: return DefWindowProcW(hwnd_, msg, wParam, -1); // nothing to repaint in a frame we don't have
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) HideSearch();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);
        if (visible_) Paint(hdc, ps.rcPaint);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (OnKeyDown(wParam)) return 0;
        break;
    case WM_CHAR: OnChar(wchar_t(wParam)); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd_, &p);
            bool text = p.y < D(SearchHeight) && p.x >= BoxLeft() - D(4) && p.x < BoxRight();
            SetCursor(LoadCursorW(nullptr, text ? IDC_IBEAM : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), msg == WM_LBUTTONDBLCLK); return 0;
    case WM_MOUSEMOVE: OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_LBUTTONUP: OnMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_MOUSELEAVE:
        tracking_ = false;
        SetHover(-1);
        if (gearHover_)
        {
            gearHover_ = false;
            InvalidateSearchRow();
        }
        return 0;
    case WM_CAPTURECHANGED: drag_ = Drag::None; return 0;
    case WM_MOUSEWHEEL: OnWheel(GET_WHEEL_DELTA_WPARAM(wParam)); return 0;
    case WM_TIMER:
        switch (wParam)
        {
        case TimerCaret:
            caretOn_ = !caretOn_;
            InvalidateSearchRow();
            break;
        case TimerAnim:
            if (PreciseMs() - animStart_ >= SlideMs)
            {
                animating_ = false;
                KillTimer(hwnd_, TimerAnim);
            }
            if (visible_) InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case TimerTrim:
            KillTimer(hwnd_, TimerTrim);
            if (!visible_ && !settingsWindow_.IsOpen()) ReleaseMemory();
            break;
        case TimerInterim:
            KillTimer(hwnd_, TimerInterim);
            if (search_.View().pending) SetStatus(Lang::Get().searching);
            break;
        case TimerDefer:
            KillTimer(hwnd_, TimerDefer);
            search_.SendDeferred();
            break;
        case TimerEverything:
            if (Everything::IsReady())
            {
                KillTimer(hwnd_, TimerEverything);
                if (visible_)
                {
                    Everything::Boost(true);
                    search_.Retry();
                    search_.PrepareIndex();
                    ApplyView();
                }
            }
            else if (++retries_ > 120)
            {
                KillTimer(hwnd_, TimerEverything);
                SetStatus(Lang::Get().everythingNoStart);
            }
            break;
        }
        return 0;
    case WM_DPICHANGED:
        SetDpi(HIWORD(wParam));
        if (visible_)
        {
            Relayout();
            ApplyWindowSize();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0)
        {
            LoadTheme();
            Tray::ThemeChanged();
            if (settings_.trayIcon) tray_.Refresh();
            if (visible_)
            {
                ApplyDwm();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
        break;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        LoadTheme();
        if (settings_.trayIcon) tray_.Refresh();
        if (visible_) InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    case WM_HOTKEY:
        if (wParam == HotkeyId) OnHotkey();
        if (wParam == GlobalHotkeyId)
        {
            if (visible_) Dismiss();
            else ShowSearch();
        }
        return 0;
    case WM_APP_EVERYTHING:
        if (search_.OnReply(reinterpret_cast<EvReply*>(lParam)))
        {
            ApplyView();
            CountAnswer();
            TRACE(L"Everything answered '%ls': %.2f ms since typing (ours %.2f), %zu rows", search_.View().query.c_str(),
                  search_.View().elapsedMs, search_.View().processMs, rows_.size());
        }
        return 0;
    case WM_APP_ICON:
    {
        std::unique_ptr<IconResult> result(reinterpret_cast<IconResult*>(lParam));
        auto it = iconCache_.find(result->key);
        if (it == iconCache_.end() || it->second.size != result->size) return 0;
        if (result->dropped)
        {
            if (!it->second.loaded) iconCache_.erase(it);
            return 0;
        }
        it->second.pixels = std::move(result->pixels);
        it->second.loaded = true;
        if (iconCache_.size() > MaxIcons)
        {
            // Least recently drawn first, pending ones included; never what the last paint drew.
            std::vector<std::pair<uint64_t, std::wstring>> order;
            for (auto& [key, entry] : iconCache_)
                if (entry.used < paintIconUse_) order.emplace_back(entry.used, key);
            std::sort(order.begin(), order.end());
            for (size_t i = 0; i < order.size() && iconCache_.size() > MaxIcons * 3 / 4; i++) iconCache_.erase(order[i].second);
        }
        InvalidateList();
        return 0;
    }
    case WM_APP_TRAY:
        switch (LOWORD(lParam))
        {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case NIN_BALLOONUSERCLICK: ShowSearch(); break;
        case WM_CONTEXTMENU:
        {
            POINT at{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)};
            std::wstring keys = settings_.desktopKeyOn ? settings_.desktopKey.Text() : settings_.globalKeyOn ? settings_.globalKey.Text() : L"";
            switch (tray_.ShowMenu(at, keys))
            {
            case TrayCmdSearch: ShowSearch(); break;
            case TrayCmdSettings: OpenSettings(); break;
            case TrayCmdAutostart:
                Autostart::Set(!Autostart::IsEnabled());
                settingsWindow_.Refresh();
                break;
            case TrayCmdAbout: ShellExecuteW(nullptr, L"open", L"" DESKFIND_URL, nullptr, nullptr, SW_SHOWNORMAL); break;
            case TrayCmdExit: Exit(); break;
            }
            break;
        }
        }
        return 0;
    case WM_APP_SHOW: ShowSearch(); return 0;
    case WM_APP_QUIT: Exit(); return 0;
    case WM_APP_SETTINGS: OpenSettings(); return 0;
    case WM_IME_STARTCOMPOSITION: UpdateImePosition(); break;
    case WM_CLOSE: Dismiss(); return 0; // Alt+F4 only hides; the app lives in the tray
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default:
        if (msg == taskbarCreated_ && msg != 0)
        {
            if (settings_.trayIcon) tray_.Refresh();
            return 0;
        }
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}
