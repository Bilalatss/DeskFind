#include "access.h"

struct AccessLink
{
    HWND hwnd = nullptr;
    AccessSource* source = nullptr;
};

namespace
{
BSTR Bstr(const std::wstring& text) { return SysAllocStringLen(text.c_str(), UINT(text.size())); }

// One element of a window's tree. It holds only its id: everything else is asked from the window when a client wants
// it, so nothing goes stale, and a new one is made for every answer (clients compare elements by their runtime id).
class Element final : public IRawElementProviderSimple,
                      public IRawElementProviderFragment,
                      public IRawElementProviderFragmentRoot,
                      public IInvokeProvider,
                      public IToggleProvider,
                      public IValueProvider,
                      public ISelectionItemProvider,
                      public IExpandCollapseProvider
{
public:
    Element(std::shared_ptr<AccessLink> link, int id) : link_(std::move(link)), id_(id) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot) && id_ == 0) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (riid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
        else if (riid == __uuidof(IToggleProvider)) *out = static_cast<IToggleProvider*>(this);
        else if (riid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
        else if (riid == __uuidof(ISelectionItemProvider)) *out = static_cast<ISelectionItemProvider*>(this);
        else if (riid == __uuidof(IExpandCollapseProvider)) *out = static_cast<IExpandCollapseProvider*>(this);
        else
        {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        long refs = InterlockedDecrement(&refs_);
        if (refs == 0) delete this;
        return ULONG(refs);
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override
    {
        *out = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** out) override
    {
        *out = nullptr;
        AccessNode node;
        if (!Describe(node)) return UIA_E_ELEMENTNOTAVAILABLE;
        IUnknown* provider = nullptr;
        if (pattern == UIA_InvokePatternId && node.invoke) provider = static_cast<IInvokeProvider*>(this);
        else if (pattern == UIA_TogglePatternId && node.toggle >= 0) provider = static_cast<IToggleProvider*>(this);
        else if (pattern == UIA_ValuePatternId && node.hasValue) provider = static_cast<IValueProvider*>(this);
        else if (pattern == UIA_SelectionItemPatternId && node.selected >= 0) provider = static_cast<ISelectionItemProvider*>(this);
        else if (pattern == UIA_ExpandCollapsePatternId && node.expanded >= 0) provider = static_cast<IExpandCollapseProvider*>(this);
        if (provider)
        {
            provider->AddRef();
            *out = provider;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override
    {
        VariantInit(out);
        AccessNode node;
        if (!Describe(node)) return UIA_E_ELEMENTNOTAVAILABLE;
        auto text = [&](const std::wstring& value)
        {
            if (value.empty()) return;
            out->vt = VT_BSTR;
            out->bstrVal = Bstr(value);
        };
        auto flag = [&](bool value)
        {
            out->vt = VT_BOOL;
            out->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
        };
        auto number = [&](long value)
        {
            out->vt = VT_I4;
            out->lVal = value;
        };
        switch (property)
        {
        case UIA_ControlTypePropertyId: number(node.role); break;
        case UIA_NamePropertyId: text(node.name); break;
        case UIA_HelpTextPropertyId: text(node.help); break;
        case UIA_IsEnabledPropertyId: flag(node.enabled); break;
        case UIA_IsKeyboardFocusablePropertyId: flag(node.focusable); break;
        case UIA_HasKeyboardFocusPropertyId: flag(::GetFocus() == link_->hwnd && link_->source->AccessFocus() == id_); break;
        case UIA_IsOffscreenPropertyId: flag(IsRectEmpty(&node.rect) != FALSE); break;
        case UIA_FrameworkIdPropertyId: text(L"DeskFind"); break;
        case UIA_LiveSettingPropertyId:
            if (node.live) number(Polite);
            break;
        case UIA_HeadingLevelPropertyId:
            if (node.heading > 0) number(HeadingLevel1 + node.heading - 1);
            break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override
    {
        *out = nullptr;
        if (id_ != 0) return S_OK;
        if (!link_->hwnd) return UIA_E_ELEMENTNOTAVAILABLE;
        return UiaHostProviderFromHwnd(link_->hwnd, out);
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override
    {
        *out = nullptr;
        AccessSource* source = link_->source;
        if (!source) return UIA_E_ELEMENTNOTAVAILABLE;
        int target = -1;
        std::vector<int> list;
        switch (direction)
        {
        case NavigateDirection_Parent:
            if (id_ != 0) target = source->AccessParent(id_);
            break;
        case NavigateDirection_FirstChild:
        case NavigateDirection_LastChild:
            source->AccessChildren(id_, list);
            if (!list.empty()) target = direction == NavigateDirection_FirstChild ? list.front() : list.back();
            break;
        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling:
        {
            if (id_ == 0) break;
            source->AccessChildren(source->AccessParent(id_), list);
            auto at = std::find(list.begin(), list.end(), id_);
            if (at == list.end()) break;
            if (direction == NavigateDirection_NextSibling && at + 1 != list.end()) target = *(at + 1);
            if (direction == NavigateDirection_PreviousSibling && at != list.begin()) target = *(at - 1);
            break;
        }
        }
        if (target >= 0) *out = new Element(link_, target);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override
    {
        *out = nullptr;
        if (id_ == 0) return S_OK; // the window's own provider gives the root's
        SAFEARRAY* ids = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!ids) return E_OUTOFMEMORY;
        int parts[2] = {UiaAppendRuntimeId, id_};
        for (LONG i = 0; i < 2; i++)
        {
            HRESULT hr = SafeArrayPutElement(ids, &i, &parts[i]);
            if (FAILED(hr))
            {
                SafeArrayDestroy(ids);
                return hr;
            }
        }
        *out = ids;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override
    {
        *out = UiaRect{};
        AccessNode node;
        if (!Describe(node)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (IsRectEmpty(&node.rect)) return S_OK;
        POINT corner{node.rect.left, node.rect.top};
        ClientToScreen(node.window ? node.window : link_->hwnd, &corner);
        *out = UiaRect{double(corner.x), double(corner.y), double(node.rect.right - node.rect.left), double(node.rect.bottom - node.rect.top)};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override
    {
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override { return Act(AccessAction::Focus); }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override
    {
        *out = new Element(link_, 0);
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override
    {
        *out = nullptr;
        if (!link_->source) return UIA_E_ELEMENTNOTAVAILABLE;
        POINT p{LONG(x), LONG(y)};
        ScreenToClient(link_->hwnd, &p);
        *out = new Element(link_, std::max(0, link_->source->AccessHit(p.x, p.y)));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override
    {
        *out = nullptr;
        if (!link_->source) return UIA_E_ELEMENTNOTAVAILABLE;
        int id = link_->source->AccessFocus();
        if (id > 0) *out = new Element(link_, id); // none: the window itself
        return S_OK;
    }

    // IInvokeProvider
    HRESULT STDMETHODCALLTYPE Invoke() override { return Act(AccessAction::Invoke); }

    // IToggleProvider
    HRESULT STDMETHODCALLTYPE Toggle() override { return Act(AccessAction::Toggle); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override
    {
        AccessNode node;
        if (!Describe(node)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = node.toggle == 1 ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

    // IValueProvider
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override
    {
        if (!link_->source) return UIA_E_ELEMENTNOTAVAILABLE;
        return link_->source->AccessSetValue(id_, value ? value : L"") ? S_OK : UIA_E_ELEMENTNOTENABLED;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override
    {
        *out = nullptr;
        AccessNode node;
        if (!Describe(node)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = Bstr(node.value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override
    {
        AccessNode node;
        if (!Describe(node)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = node.readOnly;
        return S_OK;
    }

    // ISelectionItemProvider: one selection at a time.
    HRESULT STDMETHODCALLTYPE Select() override { return Act(AccessAction::Select); }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Act(AccessAction::Select); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* out) override
    {
        AccessNode node;
        if (!Describe(node)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = node.selected == 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** out) override
    {
        *out = nullptr;
        AccessSource* source = link_->source;
        if (!source) return UIA_E_ELEMENTNOTAVAILABLE;
        // The nearest list (an item can sit in a group within it).
        for (int at = source->AccessParent(id_); at > 0; at = source->AccessParent(at))
        {
            AccessNode node;
            if (source->AccessDescribe(at, node) && (node.role == UIA_ListControlTypeId || node.role == UIA_ComboBoxControlTypeId))
            {
                *out = new Element(link_, at);
                break;
            }
        }
        return S_OK;
    }

    // IExpandCollapseProvider
    HRESULT STDMETHODCALLTYPE Expand() override { return Act(AccessAction::Expand); }
    HRESULT STDMETHODCALLTYPE Collapse() override { return Act(AccessAction::Collapse); }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* out) override
    {
        AccessNode node;
        if (!Describe(node)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = node.expanded == 1 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        return S_OK;
    }

private:
    ~Element() = default;

    bool Describe(AccessNode& node) const { return link_->source && link_->source->AccessDescribe(id_, node); }

    HRESULT Act(AccessAction action) const
    {
        if (!link_->source) return UIA_E_ELEMENTNOTAVAILABLE;
        return link_->source->AccessAct(id_, action) ? S_OK : UIA_E_ELEMENTNOTENABLED;
    }

    std::shared_ptr<AccessLink> link_;
    const int id_;
    long refs_ = 1;
};
} // namespace

void Accessibility::Attach(HWND hwnd, AccessSource* source)
{
    Detach();
    link_ = std::make_shared<AccessLink>(AccessLink{hwnd, source});
    root_ = new Element(link_, 0);
    last_ = Snapshot{};
}

void Accessibility::Detach()
{
    if (!link_) return;
    HWND hwnd = link_->hwnd;
    link_->hwnd = nullptr;
    link_->source = nullptr;
    if (hwnd) UiaReturnRawElementProvider(hwnd, 0, 0, nullptr); // UIA lets go of what it got from this window
    if (root_) root_->Release();
    root_ = nullptr;
    link_.reset();
}

bool Accessibility::OnGetObject(WPARAM wParam, LPARAM lParam, LRESULT& result)
{
    if (!root_ || static_cast<long>(lParam) != static_cast<long>(UiaRootObjectId)) return false;
    result = UiaReturnRawElementProvider(link_->hwnd, wParam, lParam, root_);
    return true;
}

void Accessibility::Sync(bool announce)
{
    if (!link_ || !link_->source) return;
    AccessSource* source = link_->source;
    Snapshot now;
    now.id = source->AccessFocus();
    AccessNode node;
    if (!source->AccessDescribe(now.id, node)) now.id = -1;
    now.toggle = node.toggle;
    now.expanded = node.expanded;
    now.value = node.value;
    now.name = node.name;
    Snapshot was = std::move(last_);
    last_ = now;
    // Focus moves only matter in the window the user is in.
    if (!announce || now.id < 0 || GetForegroundWindow() != link_->hwnd || !UiaClientsAreListening()) return;

    // A list item that now stands for something else (the results were replaced) is announced like a move.
    if (now.id != was.id || (node.selected >= 0 && now.name != was.name))
    {
        if (node.selected == 1) Raise(now.id, UIA_SelectionItem_ElementSelectedEventId);
        Raise(now.id, UIA_AutomationFocusChangedEventId);
        return;
    }
    VARIANT value;
    VariantInit(&value);
    if (now.toggle != was.toggle)
    {
        value.vt = VT_I4;
        value.lVal = now.toggle == 1 ? ToggleState_On : ToggleState_Off;
        RaiseProperty(now.id, UIA_ToggleToggleStatePropertyId, value);
    }
    if (now.expanded != was.expanded)
    {
        value.vt = VT_I4;
        value.lVal = now.expanded == 1 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        RaiseProperty(now.id, UIA_ExpandCollapseExpandCollapseStatePropertyId, value);
    }
    auto text = [&](PROPERTYID property, const std::wstring& changed)
    {
        value.vt = VT_BSTR;
        value.bstrVal = Bstr(changed);
        RaiseProperty(now.id, property, value);
        VariantClear(&value);
    };
    if (now.value != was.value) text(UIA_ValueValuePropertyId, now.value);
    if (now.name != was.name) text(UIA_NamePropertyId, now.name);
}

void Accessibility::ChildrenChanged(int id)
{
    if (!link_ || !link_->source || !UiaClientsAreListening()) return;
    auto* element = new Element(link_, id);
    UiaRaiseStructureChangedEvent(element, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    element->Release();
}

void Accessibility::Announce(int id)
{
    if (!link_ || !link_->source || !UiaClientsAreListening()) return;
    Raise(id, UIA_LiveRegionChangedEventId);
}

void Accessibility::Raise(int id, EVENTID event) const
{
    auto* element = new Element(link_, id);
    UiaRaiseAutomationEvent(element, event);
    element->Release();
}

void Accessibility::RaiseProperty(int id, PROPERTYID property, const VARIANT& now) const
{
    VARIANT old;
    VariantInit(&old);
    auto* element = new Element(link_, id);
    UiaRaiseAutomationPropertyChangedEvent(element, property, old, now);
    element->Release();
}
