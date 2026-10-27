// Everything (voidtools) over its WM_COPYDATA IPC, EVERYTHING_IPC_QUERY2 (1.4.1+); layouts from the SDK's everything_ipc.h.
#pragma once
#include "common.h"

#include <atomic>

enum : uint32_t
{
    EvFolder = 0x1,
    EvRoot = 0x2,
};

enum : uint32_t
{
    EvSortName = 1,
    EvSortDateModifiedDesc = 14,
};

struct EvItem
{
    const wchar_t* name; // null terminated, points into EvReply::data
    const wchar_t* dir;
    uint32_t nameLen;
    uint32_t dirLen;
    uint32_t flags;
};

struct EvReply
{
    uint32_t tag = 0;
    uint32_t generation = 0;
    bool ok = false;
    bool notReady = false; // Everything runs but is still loading its database
    uint32_t total = 0;
    std::vector<EvItem> items;
    std::unique_ptr<uint8_t[]> data;
};

// Fills reply.items from the `size` bytes of an EVERYTHING_IPC_LIST2 reply in reply.data; false if it is malformed.
bool ParseReply(EvReply& reply, size_t size);

// What the settings window shows about Everything.
enum class EvStatus : uint8_t
{
    Running,
    Stopped,
    Missing,
    Installing,
};

enum class EvSetup : uint8_t
{
    Started,   // winget runs; its process comes back
    NoWinget,
    Cancelled, // the UAC prompt was declined
    Failed,
};

// Queries run one at a time on a worker thread, so a slow or hung Everything never blocks the UI.
// Replies are posted to the notify window as WM_APP_EVERYTHING; queries from an older generation are dropped unsent.
class Everything
{
public:
    bool Start(HWND notify);
    void Stop();
    void SetGeneration(uint32_t generation) { latest_.store(generation); }
    void Query(uint32_t generation, uint32_t tag, std::wstring text, uint32_t max, uint32_t sort);

    static HWND Window();
    static bool IsRunning() { return Window() != nullptr; }
    static bool IsReady(); // running and its database loaded (asks Everything, at most ~150 ms)
    static std::wstring ExePath();
    static bool IsInstalled() { return IsRunning() || !ExePath().empty(); }
    static std::wstring Version(); // "1.4.1.1032", empty when it doesn't answer
    static bool Launch(const std::wstring& args);
    static bool CanInstall(); // winget is there
    static EvSetup Install(HWND owner, HANDLE& process);
    static constexpr wchar_t DownloadUrl[] = L"https://www.voidtools.com/downloads/";
    static void Boost(bool on); // full CPU speed for Everything while the search box is open

private:
    static DWORD WINAPI ThreadMain(void* self);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void SendNext();
    void Finish(EvReply* reply);

    struct Request
    {
        uint32_t generation;
        uint32_t tag;
        uint32_t max;
        uint32_t sort;
        std::wstring text;
    };

    HWND notify_ = nullptr;
    HWND hwnd_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE ready_ = nullptr;
    std::atomic<uint32_t> latest_{0};
    std::vector<Request*> queue_; // worker thread only
    Request* active_ = nullptr;
    uint32_t activeId_ = 0;
    uint32_t nextId_ = 0x4D410000;
};
