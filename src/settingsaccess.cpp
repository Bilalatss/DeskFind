// The settings window as screen readers see it: the page list, then the page's sections and cards with their
// controls, and a drop-down's options while it is open. The keyboard focus is the window's own (Tab, arrows).
#include "settingsui.h"

#include "lang.h"

namespace
{
enum : int
{
    IdPages = 1,
    IdContent,
    IdPage = 10,        // + the page
    IdItem = 1000,      // + ItemStride × the item, + 1 + the control for its controls
    ItemStride = 100,
    IdOption = 1000000, // + the option of the open drop-down
};

const wchar_t* PageName(int page)
{
    const Lang& lang = Lang::Get();
    const wchar_t* names[] = {lang.navGeneral, lang.navShortcuts, lang.navSearch, lang.navAppearance, lang.navStats, lang.navAbout};
    return names[page];
}

std::wstring JoinKeys(const std::vector<std::wstring>& keys)
{
    std::wstring out;
    for (const std::wstring& key : keys) out += (out.empty() ? L"" : L" + ") + key;
    return out;
}
} // namespace

int SettingsWindow::TargetId(const Target& target) const
{
    if (target.nav >= 0) return IdPage + target.nav;
    if (target.item < 0) return 0;
    return IdItem + target.item * ItemStride + (target.control >= 0 ? 1 + target.control : 0);
}

SettingsWindow::Target SettingsWindow::IdTarget(int id) const
{
    Target t;
    if (id >= IdPage && id < IdPage + int(Page::Count))
    {
        t.nav = id - IdPage;
    }
    else if (id >= IdItem && id < IdOption)
    {
        t.item = (id - IdItem) / ItemStride;
        t.control = (id - IdItem) % ItemStride - 1;
        if (t.item >= int(items_.size()) || t.control >= int(items_[size_t(t.item)].controls.size())) return Target{};
    }
    return t;
}

int SettingsWindow::AccessParent(int id)
{
    if (id >= IdOption) return TargetId(popupFor_);
    Target t = IdTarget(id);
    if (t.nav >= 0) return IdPages;
    if (t.control >= 0) return TargetId(Target{-1, t.item, -1});
    return t.item >= 0 ? IdContent : 0;
}

void SettingsWindow::AccessChildren(int id, std::vector<int>& out)
{
    if (id == 0)
    {
        out = {IdPages, IdContent};
        return;
    }
    if (id == IdPages)
    {
        for (int i = 0; i < int(Page::Count); i++) out.push_back(IdPage + i);
        return;
    }
    if (id == IdContent)
    {
        for (size_t i = 0; i < items_.size(); i++) out.push_back(TargetId(Target{-1, int(i), -1}));
        return;
    }
    Target t = IdTarget(id);
    if (t.item >= 0 && t.control < 0)
    {
        for (size_t c = 0; c < items_[size_t(t.item)].controls.size(); c++) out.push_back(TargetId(Target{-1, t.item, int(c)}));
    }
    else if (t.control >= 0 && popup_ && t == popupFor_)
    {
        for (size_t k = 0; k < items_[size_t(t.item)].controls[size_t(t.control)].options.size(); k++) out.push_back(IdOption + int(k));
    }
}

bool SettingsWindow::AccessDescribe(int id, AccessNode& node)
{
    const Lang& lang = Lang::Get();
    if (id == 0 || id == IdPages)
    {
        node.role = id == 0 ? UIA_WindowControlTypeId : UIA_ListControlTypeId;
        node.name = id == 0 ? lang.settingsTitle : lang.accPages;
        GetClientRect(hwnd_, &node.rect);
        if (id == IdPages) node.rect.right = NavWidth();
        return true;
    }
    if (id == IdContent)
    {
        node.role = UIA_PaneControlTypeId;
        node.name = PageName(int(page_));
        node.rect = Viewport();
        return true;
    }
    if (id >= IdOption)
    {
        const Control* combo = popup_ ? ControlAt(popupFor_) : nullptr;
        int option = id - IdOption;
        if (!combo || option >= int(combo->options.size())) return false;
        node.role = UIA_ListItemControlTypeId;
        node.name = combo->options[size_t(option)];
        node.selected = option == popupHover_;
        node.focusable = node.invoke = true;
        node.rect = PopupItemRect(option);
        node.window = popup_;
        return true;
    }
    Target t = IdTarget(id);
    if (t.nav >= 0)
    {
        node.role = UIA_ListItemControlTypeId;
        node.name = PageName(t.nav);
        node.selected = t.nav == int(page_);
        node.focusable = node.invoke = true;
        node.rect = NavRect(t.nav);
        return true;
    }
    if (t.item < 0) return false;
    const Item& item = items_[size_t(t.item)];
    if (t.control >= 0)
    {
        DescribeControl(item, item.controls[size_t(t.control)], t == popupFor_ && popup_ != nullptr, t == recording_, node);
        return true;
    }
    node.rect = ContentToClient(item.rect);
    node.name = item.title;
    switch (item.kind)
    {
    case Item::Kind::Section:
        node.role = UIA_TextControlTypeId;
        node.heading = 2;
        break;
    case Item::Kind::Note: node.role = UIA_TextControlTypeId; break;
    case Item::Kind::Tile:
        node.role = UIA_TextControlTypeId;
        node.name += L": " + item.value;
        break;
    case Item::Kind::Hero:
        node.role = UIA_TextControlTypeId;
        node.name += L", " + item.value;
        node.help = item.desc;
        break;
    default:
        node.help = item.desc;
        if (item.act != Act::None)
        {
            node.role = item.act == Act::OpenUrl ? UIA_HyperlinkControlTypeId : UIA_ButtonControlTypeId;
            node.focusable = node.invoke = true;
        }
        else
        {
            node.role = item.controls.empty() ? UIA_TextControlTypeId : UIA_GroupControlTypeId;
        }
        break;
    }
    return true;
}

// A control is named after its card ("Start with Windows"), unless it says something of its own (a button's text).
void SettingsWindow::DescribeControl(const Item& item, const Control& c, bool open, bool recording, AccessNode& node) const
{
    const Lang& lang = Lang::Get();
    node.rect = ContentToClient(c.rect);
    node.enabled = c.enabled;
    node.name = item.title;
    switch (c.type)
    {
    case Control::Type::Toggle:
        node.role = UIA_CheckBoxControlTypeId;
        node.toggle = c.on ? 1 : 0;
        break;
    case Control::Type::Combo:
        node.role = UIA_ComboBoxControlTypeId;
        node.value = c.text;
        node.hasValue = true;
        node.expanded = open ? 1 : 0;
        break;
    case Control::Type::Button:
        node.role = UIA_ButtonControlTypeId;
        node.name = c.text;
        node.invoke = true;
        break;
    case Control::Type::IconButton:
        node.role = UIA_ButtonControlTypeId;
        node.name = c.act == Act::GroupUp ? lang.moveUp : c.act == Act::GroupDown ? lang.moveDown : lang.removeFolder;
        node.help = item.title;
        node.invoke = true;
        break;
    case Control::Type::Hotkey:
        node.role = UIA_ButtonControlTypeId;
        node.value = recording ? lang.keyRecording : c.keys.empty() ? lang.keyNone : JoinKeys(c.keys);
        node.hasValue = node.invoke = true;
        node.enabled = true; // a shortcut that is off can still be changed
        break;
    case Control::Type::Keys:
        node.role = UIA_TextControlTypeId;
        node.name = JoinKeys(c.keys);
        return;
    case Control::Type::Label:
        node.role = UIA_TextControlTypeId;
        node.name = c.text;
        return;
    }
    node.focusable = node.enabled;
}

int SettingsWindow::AccessFocus()
{
    if (popup_)
    {
        const Control* combo = ControlAt(popupFor_);
        if (combo && popupHover_ >= 0 && popupHover_ < int(combo->options.size())) return IdOption + popupHover_;
    }
    std::vector<Target> list = Focusables();
    return focus_ >= 0 && focus_ < int(list.size()) ? TargetId(list[size_t(focus_)]) : 0;
}

int SettingsWindow::AccessHit(int x, int y)
{
    Target t = HitTest(x, y);
    if (t.Valid()) return TargetId(t);
    if (x < NavWidth()) return IdPages;
    POINT p{x, y};
    RECT vp = Viewport();
    if (!PtInRect(&vp, p)) return 0;
    p.y += scroll_ - vp.top;
    for (size_t i = 0; i < items_.size(); i++)
        if (PtInRect(&items_[i].rect, p)) return TargetId(Target{-1, int(i), -1});
    return IdContent;
}

// What a screen reader does goes the way the keyboard goes: the element gets the focus, then it is activated.
bool SettingsWindow::AccessAct(int id, AccessAction action)
{
    if (id >= IdOption)
    {
        const Control* combo = popup_ ? ControlAt(popupFor_) : nullptr;
        int option = id - IdOption;
        if (!combo || option >= int(combo->options.size())) return false;
        if (action == AccessAction::Focus)
        {
            popupHover_ = option;
            InvalidateRect(popup_, nullptr, FALSE);
        }
        else if (action == AccessAction::Select || action == AccessAction::Invoke)
        {
            popupHover_ = option;
            ClosePopup(true);
        }
        else
        {
            return false;
        }
        access_.Sync();
        return true;
    }

    Target t = IdTarget(id);
    std::vector<Target> list = Focusables();
    auto found = std::find(list.begin(), list.end(), t);
    if (!t.Valid() || found == list.end()) return false;
    const Control* c = ControlAt(t);
    bool fits = action == AccessAction::Focus;
    if (!c) fits = fits || action == AccessAction::Invoke || action == AccessAction::Select;
    else if (c->type == Control::Type::Toggle) fits = fits || action == AccessAction::Toggle;
    else if (c->type == Control::Type::Combo) fits = fits || action == AccessAction::Expand || action == AccessAction::Collapse || action == AccessAction::Invoke;
    else fits = fits || action == AccessAction::Invoke;
    if (!fits) return false;

    focus_ = int(found - list.begin());
    focusVisible_ = true;
    EnsureFocusVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (action == AccessAction::Collapse) ClosePopup(false);
    else if (action != AccessAction::Focus && !(action == AccessAction::Expand && popup_ && popupFor_ == t)) Activate(t);
    access_.Sync();
    return true;
}

bool SettingsWindow::AccessSetValue(int, const std::wstring&) { return false; }

void SettingsWindow::AccessRebuilt()
{
    access_.ChildrenChanged(IdContent);
    access_.Sync(IsWindowVisible(hwnd_) != FALSE);
}

void SettingsWindow::AccessPopupChanged(const Target& combo)
{
    access_.ChildrenChanged(TargetId(combo));
    access_.Sync();
}
