// UI Automation for the windows DeskFind draws itself, so screen readers (Narrator, NVDA) can read and use them: the
// window describes its elements on request, and Sync() tells listeners what changed where the keyboard is.
#pragma once
#include "common.h"

#include <ole2.h>
#include <uiautomation.h>

// One element, as its window describes it.
struct AccessNode
{
    long role = UIA_PaneControlTypeId; // a UIA_*ControlTypeId
    std::wstring name;
    std::wstring help;          // read after the name: a card's description
    std::wstring value;         // with hasValue: an edit's text, a drop-down's choice, a shortcut's keys
    RECT rect{};                // client pixels; empty when it isn't on screen
    HWND window = nullptr;      // the window `rect` is in, when not the tree's own (a drop-down's list)
    bool hasValue = false, readOnly = true;
    bool focusable = false, enabled = true;
    bool invoke = false;        // can be clicked
    int toggle = -1;            // a switch: 0 off, 1 on
    int selected = -1;          // an item that can be selected: 0 no, 1 yes
    int expanded = -1;          // opens a list: 0 closed, 1 open
    int heading = 0;            // 1..9: a heading of that level
    bool live = false;          // a status line: Announce() has it read out without moving the focus
};

enum class AccessAction : uint8_t
{
    Invoke,
    Toggle,
    Select,
    Expand,
    Collapse,
    Focus,
};

// What a window implements. Ids are its own and 0 is the window itself; every call comes on the window's thread.
class AccessSource
{
public:
    virtual int AccessParent(int id) = 0; // never asked about 0
    virtual void AccessChildren(int id, std::vector<int>& out) = 0;
    virtual bool AccessDescribe(int id, AccessNode& node) = 0; // false: it isn't there any more
    virtual int AccessFocus() = 0;                             // where the keyboard is; 0 for the window itself
    virtual int AccessHit(int x, int y) = 0;                   // the deepest element at a client point, or 0
    virtual bool AccessAct(int id, AccessAction action) = 0;   // false: it can't now
    virtual bool AccessSetValue(int id, const std::wstring& value) = 0;

protected:
    ~AccessSource() = default;
};

struct AccessLink; // the window and its source, cleared when the window goes

// One window's provider: answers WM_GETOBJECT and raises events, the latter only while a client listens.
class Accessibility
{
public:
    ~Accessibility() { Detach(); }
    void Attach(HWND hwnd, AccessSource* source);
    void Detach(); // before the window goes: elements a client still holds then answer "not available"
    bool OnGetObject(WPARAM wParam, LPARAM lParam, LRESULT& result);

    // Compares the focused element with the last call: focus moved, or its value, switch or open state changed.
    // `announce` false only notes the state (the window just appeared, and Windows reports its focus itself).
    void Sync(bool announce = true);
    void ChildrenChanged(int id); // what is under `id` was rebuilt
    void Announce(int id);        // a live text changed

private:
    struct Snapshot
    {
        int id = -1;
        int toggle = -1, expanded = -1;
        std::wstring value, name;
    };
    void Raise(int id, EVENTID event) const;
    void RaiseProperty(int id, PROPERTYID property, const VARIANT& now) const;

    std::shared_ptr<AccessLink> link_;
    IRawElementProviderSimple* root_ = nullptr;
    Snapshot last_;
};
