#include "everything.h"

#include <shellapi.h>
#include <shlobj.h>

namespace
{
constexpr wchar_t IpcClass[] = L"EVERYTHING_TASKBAR_NOTIFICATION";
constexpr wchar_t IpcClass15a[] = L"EVERYTHING_TASKBAR_NOTIFICATION_(1.5a)"; // 1.5 alpha runs as instance "1.5a"
constexpr ULONG_PTR CopyDataQuery2W = 18;
constexpr uint32_t RequestName = 0x1, RequestPath = 0x2;
constexpr UINT WM_QUEUE = WM_APP + 1, WM_NEXT = WM_APP + 2;
constexpr UINT_PTR TimeoutTimer = 1;
constexpr UINT TimeoutMs = 3000;

uint32_t U32(const uint8_t* p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

// EVERYTHING_IPC_LIST2 { totitems, numitems, offset, request_flags, sort_type } + ITEM2[numitems] { flags, data_offset }.
// At data_offset each requested field follows in flag order: DWORD length in chars, the text, a null terminator.
bool Parse(EvReply& reply, size_t size)
{
    const uint8_t* data = reply.data.get();
    if (size < 20) return false;
    reply.total = U32(data);
    uint32_t count = U32(data + 4), flags = U32(data + 12);
    if ((flags & (RequestName | RequestPath)) != (RequestName | RequestPath)) return false;
    if (count > (size - 20) / 8) return false;
    reply.items.reserve(count);
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t* entry = data + 20 + size_t(i) * 8;
        EvItem item{};
        item.flags = U32(entry);
        size_t at = U32(entry + 4);
        for (int field = 0; field < 2; field++)
        {
            if (at + 4 > size) return false;
            uint32_t length = U32(data + at);
            at += 4;
            if (length > (size - at) / 2 || at + (size_t(length) + 1) * 2 > size) return false;
            auto text = reinterpret_cast<const wchar_t*>(data + at);
            if (field == 0)
            {
                item.name = text;
                item.nameLen = length;
            }
            else
            {
                item.dir = text;
                item.dirLen = length;
            }
            at += (size_t(length) + 1) * 2;
        }
        reply.items.push_back(item);
    }
    return true;
}

// The App Installer alias first: a winget.exe found through the search path could be anyone's.
std::wstring WingetPath()
{
    std::wstring alias = KnownFolder(FOLDERID_LocalAppData);
    if (!alias.empty())
    {
        alias += L"\\Microsoft\\WindowsApps\\winget.exe";
        if (GetFileAttributesW(alias.c_str()) != INVALID_FILE_ATTRIBUTES) return alias;
    }
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"winget.exe", nullptr, MAX_PATH, found, nullptr)) return found;
    return {};
}
} // namespace

HWND Everything::Window()
{
    HWND window = FindWindowW(IpcClass, nullptr);
    return window ? window : FindWindowW(IpcClass15a, nullptr);
}

bool Everything::IsReady()
{
    HWND window = Window();
    if (!window) return false;
    DWORD_PTR loaded = 0;
    constexpr WPARAM IsDbLoaded = 401; // EVERYTHING_IPC_IS_DB_LOADED
    // Not SMTO_BLOCK: a late WM_COPYDATA from Everything must be able to get through while this waits.
    if (!SendMessageTimeoutW(window, WM_USER, IsDbLoaded, 0, SMTO_ABORTIFHUNG, 150, &loaded))
        return GetLastError() == ERROR_ACCESS_DENIED; // an elevated Everything filters this message out; queries still work
    return loaded != 0;
}

std::wstring Everything::Version()
{
    HWND window = Window();
    if (!window) return {};
    // EVERYTHING_IPC_GET_MAJOR_VERSION, _MINOR_VERSION, _REVISION and _BUILD_NUMBER are 0 to 3.
    std::wstring version;
    for (WPARAM part = 0; part < 4; part++)
    {
        DWORD_PTR value = 0;
        if (!SendMessageTimeoutW(window, WM_USER, part, 0, SMTO_ABORTIFHUNG, 200, &value)) return {};
        if (part) version += L'.';
        version += std::to_wstring(value);
    }
    return version;
}

std::wstring Everything::ExePath()
{
    // Prefer the copy that is running; portable installs live anywhere.
    if (HWND window = Window())
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
        {
            wchar_t path[MAX_PATH * 2];
            DWORD size = ARRAYSIZE(path);
            bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
            CloseHandle(process);
            if (ok) return path;
        }
    }
    for (const wchar_t* variable : {L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)"})
    {
        wchar_t root[MAX_PATH];
        if (!GetEnvironmentVariableW(variable, root, MAX_PATH)) continue;
        for (const wchar_t* folder : {L"\\Everything\\Everything.exe", L"\\Everything 1.5a\\Everything64.exe"})
        {
            std::wstring path = std::wstring(root) + folder;
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
        }
    }
    // Installed somewhere else: its start-on-sign-in entry names the exe.
    for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
    {
        wchar_t command[MAX_PATH * 2];
        DWORD size = sizeof(command);
        if (RegGetValueW(hive, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Everything", RRF_RT_REG_SZ, nullptr, command, &size) != ERROR_SUCCESS)
            continue;
        std::wstring path = command[0] == L'"' ? command + 1 : command;
        std::wstring lower = path;
        for (wchar_t& c : lower) c = LowerChar(c);
        size_t exe = lower.find(L".exe");
        if (exe == std::wstring::npos) continue;
        path.resize(exe + 4);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
    }
    return {};
}

bool Everything::CanInstall() { return !WingetPath().empty(); }

// Everything installs per machine, so winget runs elevated from the start: one UAC prompt, in front of `owner`, and
// none later from winget itself that could end up blinking on the taskbar. Its console stays hidden.
EvSetup Everything::Install(HWND owner, HANDLE& process)
{
    process = nullptr;
    std::wstring winget = WingetPath();
    if (winget.empty()) return EvSetup::NoWinget;
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpVerb = L"runas";
    info.lpFile = winget.c_str();
    info.lpParameters = L"install --id voidtools.Everything --exact --source winget --silent --accept-package-agreements --accept-source-agreements";
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) return GetLastError() == ERROR_CANCELLED ? EvSetup::Cancelled : EvSetup::Failed;
    process = info.hProcess;
    return process ? EvSetup::Started : EvSetup::Failed;
}

// Windows runs a windowless background process like Everything on efficiency cores at low clocks (EcoQoS),
// which measured ~30% slower per query; while the search box is open it gets full speed, then the default again.
void Everything::Boost(bool on)
{
    HWND window = Window();
    if (!window) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!process) return;
    PROCESS_POWER_THROTTLING_STATE state{PROCESS_POWER_THROTTLING_CURRENT_VERSION};
    state.ControlMask = on ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    state.StateMask = 0;
    SetProcessInformation(process, ProcessPowerThrottling, &state, sizeof(state));
    CloseHandle(process);
}

bool Everything::Launch(const std::wstring& args)
{
    std::wstring exe = ExePath();
    if (exe.empty()) return false;
    return (INT_PTR)ShellExecuteW(nullptr, nullptr, exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL) > 32;
}

bool Everything::Start(HWND notify)
{
    notify_ = notify;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 64 * 1024, ThreadMain, this, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread_) return false;
    WaitForSingleObject(ready_, 5000);
    return hwnd_ != nullptr;
}

void Everything::Stop()
{
    if (!thread_) return;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    WaitForSingleObject(thread_, TimeoutMs + 1000); // longer than any query can block it
    CloseHandle(thread_);
    CloseHandle(ready_);
    thread_ = nullptr;
}

void Everything::Query(uint32_t generation, uint32_t tag, std::wstring text, uint32_t max, uint32_t sort)
{
    auto* request = new Request{generation, tag, max, sort, std::move(text)};
    if (!PostMessageW(hwnd_, WM_QUEUE, 0, reinterpret_cast<LPARAM>(request))) delete request;
}

DWORD WINAPI Everything::ThreadMain(void* param)
{
    auto* self = static_cast<Everything*>(param);
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DeskFind.Everything";
    RegisterClassW(&wc);
    self->hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, self);
    if (self->hwnd_)
    {
        // Everything may run elevated; let its reply through UIPI.
        ChangeWindowMessageFilterEx(self->hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    }
    SetEvent(self->ready_);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    return 0;
}

LRESULT CALLBACK Everything::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Everything*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->Handle(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Everything::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_QUEUE:
        queue_.push_back(reinterpret_cast<Request*>(lParam));
        if (!active_) SendNext();
        return 0;

    case WM_NEXT:
        if (!active_) SendNext();
        return 0;

    case WM_COPYDATA:
    {
        auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (!active_ || cds->dwData != activeId_) return FALSE;
        auto* reply = new EvReply;
        // The buffer is only valid during this message.
        reply->data.reset(new (std::nothrow) uint8_t[cds->cbData + 2]);
        if (reply->data)
        {
            memcpy(reply->data.get(), cds->lpData, cds->cbData);
        }
        // Copied: let Everything go on while this thread parses.
        ReplyMessage(TRUE);
        if (reply->data) reply->ok = Parse(*reply, cds->cbData);
        if (!reply->ok) reply->items.clear();
        Finish(reply);
        return TRUE;
    }

    case WM_TIMER:
        if (wParam == TimeoutTimer && active_) Finish(new EvReply);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        for (Request* request : queue_) delete request;
        queue_.clear();
        delete active_;
        active_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Everything::Finish(EvReply* reply)
{
    KillTimer(hwnd_, TimeoutTimer);
    reply->tag = active_->tag;
    reply->generation = active_->generation;
    delete active_;
    active_ = nullptr;
    if (!PostMessageW(notify_, WM_APP_EVERYTHING, 0, reinterpret_cast<LPARAM>(reply))) delete reply;
    // Never send the next query from inside Everything's reply: it answers a nested query only much later.
    PostMessageW(hwnd_, WM_NEXT, 0, 0);
}

void Everything::SendNext()
{
    while (!queue_.empty())
    {
        // What the user typed goes before background work (generation 0, the desktop index).
        auto next = std::find_if(queue_.begin(), queue_.end(), [](const Request* r) { return r->generation != 0; });
        if (next == queue_.end()) next = queue_.begin();
        Request* request = *next;
        queue_.erase(next);
        if (request->generation != 0 && request->generation != latest_.load())
        {
            delete request; // superseded by newer typing
            continue;
        }

        active_ = request;
        // A new id before anything can wait: a late answer to an older query that arrives meanwhile is ignored.
        activeId_ = ++nextId_;
        HWND target = Window();
        if (!target || !IsReady())
        {
            // A freshly started Everything takes queries only after its database is loaded.
            auto* reply = new EvReply;
            reply->notReady = target != nullptr;
            Finish(reply);
            return;
        }

        // EVERYTHING_IPC_QUERY2: reply_hwnd, reply_copydata_message, search_flags, offset, max_results, request_flags, sort_type, text.
        size_t textBytes = (request->text.size() + 1) * sizeof(wchar_t);
        std::vector<uint8_t> buffer(28 + textBytes);
        uint32_t header[7] = {
            uint32_t(reinterpret_cast<UINT_PTR>(hwnd_)), activeId_, 0, 0, request->max, RequestName | RequestPath, request->sort,
        };
        memcpy(buffer.data(), header, sizeof(header));
        memcpy(buffer.data() + 28, request->text.c_str(), textBytes);

        COPYDATASTRUCT cds{CopyDataQuery2W, DWORD(buffer.size()), buffer.data()};
        DWORD_PTR result = 0;
        uint32_t id = activeId_;
        SetTimer(hwnd_, TimeoutTimer, TimeoutMs, nullptr);
        // Everything answers later with its own WM_COPYDATA; the reply can also arrive while this call waits.
        if (!SendMessageTimeoutW(target, WM_COPYDATA, reinterpret_cast<WPARAM>(hwnd_), reinterpret_cast<LPARAM>(&cds),
                                 SMTO_ABORTIFHUNG, TimeoutMs, &result) ||
            !result)
        {
            if (active_ && activeId_ == id) Finish(new EvReply);
        }
        return;
    }
}
