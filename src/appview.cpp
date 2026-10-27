// The search window's layout, what it shows for a search, and its painting.
#include "app.h"

#include <cmath>

#include "appparts.h"
#include "lang.h"

namespace
{
double EaseOut(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    double u = 1 - t;
    return 1 - u * u * u;
}

bool Intersects(const RECT& a, const RECT& b) { return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom; }

RECT Intersect(const RECT& a, const RECT& b)
{
    return RECT{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

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
    // A notice goes where the rest-of-PC group is shown, under its title.
    Notice notice = CurrentNotice();
    auto position = [&](Tier tier)
    {
        for (int i = 0; i < 3; i++)
            if (settings_.order[i] == uint8_t(tier)) return i;
        return 3;
    };
    bool noticed = notice == Notice::None;
    for (const TierBlock& block : view.tiers)
    {
        if (!noticed && position(block.tier) > position(Tier::Elsewhere))
        {
            AddNotice(rows, notice, true);
            noticed = true;
        }
        Row header;
        header.header = true;
        header.tier = block.tier;
        header.title = TierTitle(block.tier);
        if (block.tier != Tier::Recent) header.count = FormatCount(block.total);
        rows.push_back(std::move(header));
        if (!noticed && block.tier == Tier::Elsewhere)
        {
            AddNotice(rows, notice, false);
            noticed = true;
        }
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
    if (!noticed) AddNotice(rows, notice, true);

    // Revalidation usually brings the same answer: keep the rendered rows and only refresh the status.
    bool same = sameQuery && rows.size() == rows_.size();
    for (size_t i = 0; same && i < rows.size(); i++)
        same = rows[i].header == rows_[i].header && rows[i].count == rows_[i].count && rows[i].title == rows_[i].title &&
               rows[i].result.path == rows_[i].result.path && rows[i].notice == rows_[i].notice && rows[i].display == rows_[i].display;
    if (!same)
    {
        rows_ = std::move(rows);
        shownQuery_ = view.query;
        selected_ = -1;
        for (size_t i = 0; i < rows_.size(); i++)
        {
            // A notice is never selected for the user, so Enter can't start an install by surprise.
            if (rows_[i].header || rows_[i].notice != Notice::None) continue;
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
            access_.ChildrenChanged(0);
            access_.Sync();
        }
    }
    shownGeneration_ = view.generation;
    UpdateStatus();
}

void App::UpdateStatus()
{
    const SearchView& view = search_.View();
    const Lang& lang = Lang::Get();
    if (view.everythingMissing && evStatus_) return; // EverythingMissing() said what is going on
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
    wchar_t time[32] = {};
    if (view.elapsedMs < 1) wcscpy_s(time, L"<1 ms");
    else swprintf_s(time, L"%d ms", int(std::lround(view.elapsedMs)));
    SetStatus(FormatCount(total) + L" " + (total == 1 ? lang.result : lang.results) + L" · " + time);
}

void App::SetStatus(const std::wstring& text)
{
    if (text == status_) return;
    status_ = text;
    statusRendered_ = false;
    AccessStatusChanged();
    if (visible_)
    {
        RECT footer = FooterRect();
        InvalidateRect(hwnd_, &footer, FALSE);
    }
}

// The desktop part is still shown; the rest follows by itself once Everything can answer. When it can't (not installed,
// or it didn't start), a notice row in the rest-of-PC group says so instead of the footer.
void App::EverythingMissing()
{
    const Lang& lang = Lang::Get();
    // With the rest of the PC hidden, Everything is not the user's business: it is still started, silently.
    bool say = settings_.show[size_t(Tier::Elsewhere)];
    evStatus_ = false;
    if (Everything::IsRunning())
    {
        if (say) SetStatus(lang.everythingLoading);
    }
    else
    {
        if (!evInstalled_ || evInstalling_ || evStartFailed_) return;
        if (NowMs() - lastLaunch_ > 10000)
        {
            lastLaunch_ = NowMs();
            if (!Everything::Launch(L"-startup"))
            {
                evInstalled_ = Everything::IsInstalled();
                evStartFailed_ = evInstalled_;
                return;
            }
        }
        if (say) SetStatus(lang.everythingStarting);
    }
    evStatus_ = say;
    retries_ = 0;
    SetTimer(hwnd_, TimerEverything, 250, nullptr);
}

App::Notice App::CurrentNotice() const
{
    if (!settings_.show[size_t(Tier::Elsewhere)] || search_.View().query.empty()) return Notice::None;
    if (evInstalling_) return Notice::Installing;
    if (Everything::IsRunning()) return Notice::None;
    if (!evInstalled_) return evInstallFailed_ || !Everything::CanInstall() ? Notice::Download : Notice::Install;
    return evStartFailed_ ? Notice::Retry : Notice::None;
}

void App::AddNotice(std::vector<Row>& rows, Notice notice, bool header) const
{
    const Lang& lang = Lang::Get();
    if (header)
    {
        Row title;
        title.header = true;
        title.tier = Tier::Elsewhere;
        title.title = TierTitle(Tier::Elsewhere);
        rows.push_back(std::move(title));
    }
    Row row;
    row.tier = Tier::Elsewhere;
    row.notice = notice;
    switch (notice)
    {
    case Notice::Install:
        row.display = lang.noticeInstall;
        row.location = lang.noticeInstallDesc;
        row.iconKey = L"\xE896"; // Download
        break;
    case Notice::Download:
        row.display = evInstallFailed_ ? lang.noticeInstallFailed : lang.noticeInstall;
        row.location = evInstallFailed_ ? lang.noticeInstallFailedDesc : lang.noticeDownloadDesc;
        row.iconKey = L"\xE896";
        break;
    case Notice::Installing:
        row.display = lang.noticeInstalling;
        row.location = lang.noticeInstallingDesc;
        row.iconKey = L"\xE895"; // Sync
        break;
    default:
        row.display = lang.noticeNoStart;
        row.location = lang.noticeNoStartDesc;
        row.iconKey = L"\xE72C"; // Refresh
        break;
    }
    rows.push_back(std::move(row));
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
        std::vector<RECT> selection;
        if (layout && editor_.HasSelection())
        {
            UINT32 count = 0;
            layout->HitTestTextRange(editor_.SelStart(), editor_.SelEnd() - editor_.SelStart(), 0, 0, nullptr, 0, &count);
            std::vector<DWRITE_HIT_TEST_METRICS> ranges(count);
            if (count && SUCCEEDED(layout->HitTestTextRange(editor_.SelStart(), editor_.SelEnd() - editor_.SelStart(), 0, 0, ranges.data(), count, &count)))
                for (const auto& m : ranges)
                {
                    selection.push_back(RECT{origin + int(std::lround(m.left)), top, origin + int(std::lround(m.left + m.width)), top + lineHeight});
                    canvas_.FillRect(selection.back().left, top, selection.back().right, top + lineHeight, theme_.selection);
                }
        }
        canvas_.DrawText(boxText_, origin, top, palette);
        if (theme_.selectedText)
        {
            RECT box = canvas_.Clip();
            for (const RECT& r : selection)
            {
                canvas_.SetClip(Intersect(box, r));
                canvas_.DrawText(boxText_, origin, top, palette, theme_.selectedText);
            }
            canvas_.SetClip(box);
        }
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
    canvas_.DrawText(gearGlyph_, (gear.left + gear.right - int(gearGlyph_.width)) / 2, (gear.top + gear.bottom - int(gearGlyph_.height)) / 2, palette,
                     gearHover_ ? theme_.selectedText : 0);
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
    if (row.notice != Notice::None) row.third = text_.Render(row.iconKey, {Font::Icons, 20 * scale_, false, Ink::Accent});
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
        if (hovered && !selected && theme_.outline)
            canvas_.StrokeRoundRect(float(x0), float(rowTop), float(x1), float(rowBottom), RowRadius * scale_, float(Line()), theme_.outline);
        Argb ink = selected ? theme_.selectedText : 0;
        if (selected)
        {
            float middle = float(rowTop + rowBottom) / 2;
            canvas_.FillRoundRect(float(x0), middle - 9 * scale_, float(x0) + 3 * scale_, middle + 9 * scale_, 1.5f * scale_, theme_.accentFill);
        }
        int iconSize = D(IconSize);
        if (row.notice != Notice::None)
            canvas_.DrawText(row.third, x0 + D(IconLeft) + (iconSize - int(std::lround(row.third.width))) / 2,
                             rowTop + (rowBottom - rowTop - int(std::lround(row.third.height))) / 2, palette, ink);
        else if (const IconEntry* icon = IconFor(row); icon && !icon->pixels.empty())
            canvas_.DrawImage(icon->pixels.data(), icon->size, icon->size, x0 + D(IconLeft), rowTop + (rowBottom - rowTop - iconSize) / 2);

        int textX = x0 + D(TextLeft);
        int stack = int(std::lround(line14_ + float(D(1)) + line12_));
        int textY = rowTop + (rowBottom - rowTop - stack) / 2;
        canvas_.DrawText(row.first, textX, textY, palette, ink);
        canvas_.DrawText(row.second, textX, textY + int(std::lround(line14_)) + D(1), palette, ink);
        if (!row.kind.empty())
            canvas_.DrawText(row.third, x1 - D(RowRight) - int(std::ceil(row.third.width)), rowTop + (rowBottom - rowTop - int(std::lround(line12_))) / 2, palette,
                             ink);
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
        std::vector<std::pair<const wchar_t*, const wchar_t*>> pairs = {{L"↵", lang.hintOpen}, {L"Ctrl ↵", lang.hintReveal}};
        if (evInstalled_) pairs.emplace_back(L"Ctrl E", L"Everything");
        pairs.emplace_back(L"Esc", lang.hintClose);
        TextStyle style{Font::Ui, KeyFont * scale_, false, Ink::Tertiary};
        hintsWidth_ = 0;
        for (size_t i = 0; i < pairs.size(); i++)
        {
            Hint hint{text_.Render(pairs[i].first, style), text_.Render(pairs[i].second, style)};
            hintsWidth_ += D(5) * 2 + 2 * Line() + int(std::ceil(hint.key.width)) + D(6) + int(std::ceil(hint.label.width));
            if (i + 1 < pairs.size()) hintsWidth_ += D(14);
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
