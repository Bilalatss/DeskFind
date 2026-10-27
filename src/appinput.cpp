// The search window's text box, result list, keyboard and mouse.
#include "app.h"

#include <imm.h>

#include <cmath>

#include "appparts.h"
#include "lang.h"
#include "shell.h"

namespace
{
bool Pressed(int key) { return GetKeyState(key) < 0; }

bool IsWordChar(wchar_t c) { return IsCharAlphaNumericW(c) != FALSE || c == L'_'; }
} // namespace

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
    listFocus_ = false;
    access_.Sync();
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
    access_.Sync();
}

void App::Move(int delta)
{
    if (rows_.empty()) return;
    listFocus_ = true;
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
    access_.Sync();
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
    if (!evInstalled_)
    {
        SetStatus(Lang::Get().everythingMissing);
        return;
    }
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
            if (row->notice != Notice::None) RunNotice(row->notice);
            else if (ctrl) Reveal(row->result);
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
            if (Row* row = Selected(); row && row->notice == Notice::None)
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
    if (rows_[row].notice != Notice::None) RunNotice(rows_[row].notice);
    else if (Pressed(VK_CONTROL)) Reveal(rows_[row].result);
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
