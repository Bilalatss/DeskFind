// The search window as screen readers see it: the search box, the results grouped as on screen, the settings button
// and the status line, which is read out when it changes.
#include "app.h"

#include "appparts.h"
#include "lang.h"

namespace
{
enum : int
{
    IdBox = 1,
    IdList,
    IdGear,
    IdStatus,
    IdRow = 100, // + the row's index; a group title stands for its group
};
} // namespace

bool App::IsRowId(int id) const { return id >= IdRow && size_t(id - IdRow) < rows_.size(); }

int App::AccessParent(int id)
{
    if (!IsRowId(id)) return 0;
    size_t index = size_t(id - IdRow);
    if (rows_[index].header) return IdList;
    for (size_t i = index; i-- > 0;)
        if (rows_[i].header) return IdRow + int(i);
    return IdList;
}

void App::AccessChildren(int id, std::vector<int>& out)
{
    if (id == 0)
    {
        out = {IdBox, IdGear};
        if (!rows_.empty()) out.push_back(IdList);
        out.push_back(IdStatus);
    }
    else if (id == IdList)
    {
        bool grouped = false;
        for (size_t i = 0; i < rows_.size(); i++)
        {
            grouped = grouped || rows_[i].header;
            if (rows_[i].header || !grouped) out.push_back(IdRow + int(i));
        }
    }
    else if (IsRowId(id) && rows_[size_t(id - IdRow)].header)
    {
        for (size_t i = size_t(id - IdRow) + 1; i < rows_.size() && !rows_[i].header; i++) out.push_back(IdRow + int(i));
    }
}

bool App::AccessDescribe(int id, AccessNode& node)
{
    const Lang& lang = Lang::Get();
    switch (id)
    {
    case 0:
        node.role = UIA_WindowControlTypeId;
        node.name = L"DeskFind";
        GetClientRect(hwnd_, &node.rect);
        return true;
    case IdBox:
        node.role = UIA_EditControlTypeId;
        node.name = lang.placeholder;
        node.value = editor_.text;
        node.hasValue = true;
        node.readOnly = false;
        node.focusable = true;
        node.rect = RECT{BoxLeft(), 0, BoxRight(), D(SearchHeight)};
        return true;
    case IdList:
        if (rows_.empty()) return false;
        node.role = UIA_ListControlTypeId;
        node.name = lang.accResults;
        node.rect = ViewportRect();
        return true;
    case IdGear:
        node.role = UIA_ButtonControlTypeId;
        node.name = lang.menuSettings;
        node.invoke = true;
        node.rect = GearRect();
        return true;
    case IdStatus:
        node.role = UIA_TextControlTypeId;
        node.name = status_;
        node.live = true;
        node.rect = FooterRect();
        return true;
    }
    if (!IsRowId(id)) return false;
    size_t index = size_t(id - IdRow);
    const Row& row = rows_[index];
    RECT vp = ViewportRect();
    int top = vp.top + row.top - scroll_;
    if (top + row.height > vp.top && top < vp.bottom)
        node.rect = RECT{D(ListPad), std::max(top, int(vp.top)), RowsRight(), std::min(top + row.height, int(vp.bottom))};
    if (row.header)
    {
        node.role = UIA_GroupControlTypeId;
        node.name = row.count.empty() ? row.title : row.title + L", " + row.count;
        return true;
    }
    // Read as a sighted user sees it: the name, where it is, and what it is when that isn't plain from the name.
    node.role = UIA_ListItemControlTypeId;
    node.name = row.display;
    if (!row.location.empty()) node.name += L", " + row.location;
    if (row.notice == Notice::None && !row.kind.empty()) node.name += L", " + row.kind;
    node.focusable = true;
    node.invoke = true;
    node.selected = int(index) == selected_;
    return true;
}

// The keyboard stays in the search box; the arrow keys move a selection through the list, and while they do, the
// selected result is what has the focus.
int App::AccessFocus()
{
    if (!visible_) return 0;
    if (listFocus_ && IsRowId(IdRow + selected_)) return IdRow + selected_;
    return IdBox;
}

int App::AccessHit(int x, int y)
{
    POINT p{x, y};
    RECT gear = GearRect(), footer = FooterRect(), vp = ViewportRect();
    if (PtInRect(&gear, p)) return IdGear;
    if (y < D(SearchHeight)) return IdBox;
    if (PtInRect(&footer, p)) return IdStatus;
    if (rows_.empty() || !PtInRect(&vp, p)) return 0;
    int content = y - vp.top + scroll_;
    for (size_t i = 0; i < rows_.size(); i++)
        if (content >= rows_[i].top && content < rows_[i].top + rows_[i].height) return IdRow + int(i);
    return IdList;
}

bool App::AccessAct(int id, AccessAction action)
{
    if (id == IdGear && action == AccessAction::Invoke)
    {
        OpenSettings();
        return true;
    }
    if (id == IdBox && action == AccessAction::Focus)
    {
        listFocus_ = false;
        access_.Sync();
        return true;
    }
    if (!IsRowId(id) || rows_[size_t(id - IdRow)].header) return false;
    int index = id - IdRow;
    switch (action)
    {
    case AccessAction::Invoke:
        if (rows_[size_t(index)].notice != Notice::None) RunNotice(rows_[size_t(index)].notice);
        else Open(rows_[size_t(index)].result);
        return true;
    case AccessAction::Select:
    case AccessAction::Focus:
        listFocus_ = true;
        Select(index, true);
        access_.Sync();
        return true;
    default: return false;
    }
}

bool App::AccessSetValue(int id, const std::wstring& value)
{
    if (id != IdBox) return false;
    editor_.anchor = 0;
    editor_.caret = int(editor_.text.size());
    Edit(value, false);
    return true;
}

void App::AccessStatusChanged()
{
    if (visible_) access_.Announce(IdStatus);
}
