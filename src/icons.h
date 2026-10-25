// Shell icons, loaded on a background STA thread and handed to the UI as premultiplied pixels.
#pragma once
#include "common.h"

struct IconResult
{
    std::wstring key;
    int size = 0;
    std::vector<uint32_t> pixels; // premultiplied BGRA, size x size; empty when there is no icon
    bool dropped = false;         // the queue was full: ask again when the row is drawn next
};

class IconLoader
{
public:
    void Start(HWND notify);
    void Stop();
    // Newest requests are served first: they are the rows on screen now.
    void Request(const std::wstring& key, const std::wstring& path, bool folder, int size);
    void Cancel(); // drop everything queued (the window was hidden)

    // Generic per-extension icons share one key; types with their own icon get one per file.
    static std::wstring KeyFor(std::wstring_view path, bool folder);

private:
    struct Job
    {
        std::wstring key, path;
        bool folder;
        int size;
    };
    static DWORD WINAPI ThreadMain(void* self);
    void Work();
    bool Next(Job& job);

    HWND notify_ = nullptr;
    HANDLE thread_ = nullptr;
    SRWLOCK lock_ = SRWLOCK_INIT;
    CONDITION_VARIABLE wake_ = CONDITION_VARIABLE_INIT;
    std::vector<Job> jobs_;
    bool stop_ = false;
};
