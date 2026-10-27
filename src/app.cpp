#include "app.h"

#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <ctime>

#include "appparts.h"
#include "lang.h"
#include "shell.h"
#include "version.h"

namespace
{
constexpr int HotkeyId = 0x4446, GlobalHotkeyId = 0x4447, TestHotkeyId = 0x4448;
constexpr ULONG InstallTimeoutMs = 10 * 60 * 1000;

App* g_app = nullptr;
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
    (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
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
    evInstalled_ = Everything::IsInstalled();
    // The first run's welcome; without Everything it says what Everything adds, and a click opens its settings card.
    if (Autostart::Initialize())
        Notify(Lang::Get().welcomeTitle, evInstalled_ ? Lang::Get().welcomeText : Lang::Get().welcomeEverything,
               evInstalled_ ? Balloon::Search : Balloon::Everything);
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
    access_.Attach(hwnd_, this);
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
    DWM_SYSTEMBACKDROP_TYPE backdrop = settings_.transparency && !theme_.highContrast ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    COLORREF border = theme_.highContrast ? GetSysColor(COLOR_WINDOWTEXT) : DWMWA_COLOR_DEFAULT;
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
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
    access_.Detach();
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
    // Installed or removed meanwhile: the notice and the Ctrl+E hint follow. A start that failed is tried again.
    bool installed = Everything::IsInstalled();
    if (installed != evInstalled_)
    {
        hints_.clear();
        statusRendered_ = false;
    }
    evInstalled_ = installed;
    if (installed) evInstallFailed_ = false;
    evStartFailed_ = false;
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
    listFocus_ = false;
    access_.Sync(false);
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

void App::OpenSettings(bool everything)
{
    HideSearch();
    KillTimer(hwnd_, TimerTrim);
    SetIdle(false);
    settingsWindow_.Open(this, everything);
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

// ---------------------------------------------------------------- Everything

void App::RunNotice(Notice notice)
{
    switch (notice)
    {
    case Notice::Install: InstallEverything(hwnd_); break;
    case Notice::Download: OpenDownloadPage(); break;
    case Notice::Retry:
        evStartFailed_ = false;
        lastLaunch_ = 0;
        ApplyView(); // EverythingMissing() starts it again and says so
        break;
    default: break; // installing: the row only tells
    }
}

void App::OpenDownloadPage()
{
    AllowSetForegroundWindow(ASFW_ANY);
    ShellExecuteW(nullptr, L"open", Everything::DownloadUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

// winget behind one UAC prompt; without winget, or once it has failed, the download page instead.
void App::InstallEverything(HWND owner)
{
    if (evInstalling_) return;
    if (Everything::IsInstalled())
    {
        evInstalled_ = true;
        EverythingChanged();
        return;
    }
    if (evInstallFailed_ || !Everything::CanInstall())
    {
        OpenDownloadPage();
        return;
    }
    const Lang& lang = Lang::Get();
    HANDLE process = nullptr;
    switch (Everything::Install(owner, process))
    {
    case EvSetup::Started:
        if (RegisterWaitForSingleObject(&installWait_, process, OnInstallExit, hwnd_, InstallTimeoutMs, WT_EXECUTEONLYONCE))
        {
            installProcess_ = process;
            evInstalling_ = true;
            // The UAC prompt can take the search box away: then a notification says the news will come.
            if (!visible_ && !settingsWindow_.IsOpen()) Notify(lang.installingTitle, lang.installingText, Balloon::Search);
            break;
        }
        CloseHandle(process);
        evInstallFailed_ = true;
        break;
    case EvSetup::NoWinget: OpenDownloadPage(); return;
    case EvSetup::Cancelled: return; // asked, and the answer was no
    case EvSetup::Failed: evInstallFailed_ = true; break;
    }
    EverythingChanged();
}

void CALLBACK App::OnInstallExit(void* context, BOOLEAN) { PostMessageW(static_cast<HWND>(context), WM_APP_SETUP, 0, 0); }

// winget's exit code is not trusted either way ("already installed" is an error): what counts is whether Everything is there now.
void App::InstallDone()
{
    if (installWait_) (void)UnregisterWait(installWait_);
    if (installProcess_) CloseHandle(installProcess_);
    installWait_ = installProcess_ = nullptr;
    evInstalling_ = false;
    evInstalled_ = Everything::IsInstalled();
    evInstallFailed_ = !evInstalled_;
    const Lang& lang = Lang::Get();
    if (evInstalled_)
    {
        evStartFailed_ = false;
        if (!Everything::IsRunning())
        {
            lastLaunch_ = NowMs();
            Everything::Launch(L"-startup");
        }
        retries_ = 0;
        SetTimer(hwnd_, TimerEverything, 250, nullptr);
        if (!visible_) Notify(lang.installedTitle, lang.installedText, Balloon::Search);
    }
    else if (!visible_)
    {
        Notify(lang.installFailedTitle, lang.installFailedText, Balloon::Download);
    }
    EverythingChanged();
}

// Installed, installing or failed: the notice row, the Ctrl+E hint and the settings card follow.
void App::EverythingChanged()
{
    hints_.clear();
    statusRendered_ = false;
    if (visible_)
    {
        ApplyView();
        RECT footer = FooterRect();
        InvalidateRect(hwnd_, &footer, FALSE);
    }
    settingsWindow_.EverythingChanged();
}

EvStatus App::EverythingStatus() const
{
    if (evInstalling_) return EvStatus::Installing;
    if (Everything::IsRunning()) return EvStatus::Running;
    return Everything::IsInstalled() ? EvStatus::Stopped : EvStatus::Missing;
}

void App::StartEverything()
{
    evStartFailed_ = false;
    lastLaunch_ = NowMs();
    if (!Everything::Launch(L"-startup")) return;
    retries_ = 0;
    SetTimer(hwnd_, TimerEverything, 250, nullptr);
}

void App::Notify(const wchar_t* title, const wchar_t* text, Balloon click)
{
    if (!settings_.trayIcon) return; // no icon, no notifications
    balloon_ = click;
    tray_.Notify(title, text);
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
    case WM_GETOBJECT:
    {
        LRESULT result = 0;
        if (access_.OnGetObject(wParam, lParam, result)) return result;
        break;
    }
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
    case WM_TIMER: OnTimer(wParam); return 0;
    case WM_DPICHANGED:
        SetDpi(HIWORD(wParam));
        if (visible_)
        {
            Relayout();
            ApplyWindowSize();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
        if (msg == WM_SYSCOLORCHANGE || wParam == SPI_SETHIGHCONTRAST ||
            (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0))
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
    case WM_APP_ICON: OnIcon(reinterpret_cast<IconResult*>(lParam)); return 0;
    case WM_APP_TRAY: OnTray(wParam, lParam); return 0;
    case WM_APP_SHOW: ShowSearch(); return 0;
    case WM_APP_QUIT: Exit(); return 0;
    case WM_APP_SETTINGS: OpenSettings(); return 0;
    case WM_APP_SETUP: InstallDone(); return 0;
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

void App::OnTimer(WPARAM id)
{
    switch (id)
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
        settingsWindow_.EverythingChanged(); // its card shows "running" as soon as it is, before its database is loaded
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
        else if (Everything::IsRunning())
        {
            retries_ = 0; // loading its database, which takes a while the first time
        }
        else if (++retries_ > 120)
        {
            KillTimer(hwnd_, TimerEverything);
            evStartFailed_ = true;
            if (visible_) ApplyView();
        }
        break;
    }
}

void App::OnIcon(IconResult* raw)
{
    std::unique_ptr<IconResult> result(raw);
    auto it = iconCache_.find(result->key);
    if (it == iconCache_.end() || it->second.size != result->size) return;
    if (result->dropped)
    {
        if (!it->second.loaded) iconCache_.erase(it);
        return;
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
}

void App::OnTray(WPARAM wParam, LPARAM lParam)
{
    switch (LOWORD(lParam))
    {
    case NIN_SELECT:
    case NIN_KEYSELECT: ShowSearch(); break;
    case NIN_BALLOONUSERCLICK:
        if (balloon_ == Balloon::Everything) OpenSettings(true);
        else if (balloon_ == Balloon::Download) OpenDownloadPage();
        else ShowSearch();
        break;
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
}
