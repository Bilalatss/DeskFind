// DeskFind.exe --selftest out.txt [@settings] [@boost] [@index|@walk] query...  (@settings: search as the saved settings say;
// @walk: the desktop folder index read from disk, as without Everything) : runs the search pipeline without UI and writes
// what it found and how long it took.
// A query written as "a>b>c" is typed one prefix at a time, the way the search box sends it.
#include "search.h"
#include "settings.h"

namespace
{
struct Harness
{
    Search search;
    HWND hwnd = nullptr;
    std::wstring log;

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // Pumps messages until the current search is final (a one-letter query is sent at once, as if typing paused).
    void Wait()
    {
        if (search.View().deferred) search.SendDeferred();
        double deadline = PreciseMs() + 5000;
        while (search.View().pending && PreciseMs() < deadline) Pump();
    }

    void Pump()
    {
        MSG msg;
        if (!GetMessageW(&msg, nullptr, 0, 0)) return;
        if (msg.message == WM_APP_EVERYTHING) search.OnReply(reinterpret_cast<EvReply*>(msg.lParam));
        else DispatchMessageW(&msg);
    }

    void BuildIndex(bool fromDisk)
    {
        double started = PreciseMs();
        search.PrepareIndex(fromDisk);
        while (!search.IndexReady() && PreciseMs() - started < 5000) Pump();
        wchar_t line[128];
        swprintf_s(line, L"index%ls ready=%d in %.1f ms, %zu entries, %.0f KB\r\n", fromDisk ? L" (disk)" : L"", search.IndexReady(), PreciseMs() - started,
                   search.IndexEntries(), search.IndexBytes() / 1024.0);
        log += line;
    }

    void Dump(bool details)
    {
        const SearchView& view = search.View();
        wchar_t line[256];
        swprintf_s(line, L"%-24ls %7.2f ms (ours %.2f)%ls%ls\r\n", view.query.c_str(), view.elapsedMs, view.processMs, view.fromCache ? L" (cache)" : L"",
                   view.everythingMissing ? L" (no Everything)" : L"");
        log += line;
        if (!details) return;
        for (const TierBlock& block : view.tiers)
        {
            const wchar_t* names[] = {L"Desktop", L"DesktopFolders", L"Elsewhere", L"Recent"};
            swprintf_s(line, L"  [%ls] total=%u shown=%zu\r\n", names[int(block.tier)], block.total, block.items.size());
            log += line;
            for (size_t i = 0; i < block.items.size() && i < 6; i++)
            {
                const Result& r = block.items[i];
                log += L"    " + std::to_wstring(r.score) + L" " + std::wstring(r.Name()) + L" | " + search.DescribeLocation(r.Dir()) + L"\r\n";
            }
        }
    }
};
} // namespace

int RunSelfTest(int argc, wchar_t** argv)
{
    if (argc < 3) return 2;
    Harness h;
    WNDCLASSW wc{};
    wc.lpfnWndProc = Harness::Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DeskFind.SelfTest";
    RegisterClassW(&wc);
    h.hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    h.search.Init(h.hwnd);
    h.log += L"everything=" + std::to_wstring(Everything::IsRunning()) + L"\r\n";

    for (int i = 2; i < argc; i++)
    {
        std::wstring spec = argv[i];
        if (spec == L"@index" || spec == L"@walk")
        {
            h.BuildIndex(spec == L"@walk");
            continue;
        }
        if (spec == L"@settings")
        {
            h.search.Configure(Settings::Load().Search());
            h.log += L"settings loaded\r\n";
            continue;
        }
        if (spec == L"@boost")
        {
            Everything::Boost(true);
            h.log += L"boost on\r\n";
            continue;
        }
        if (spec.find(L'+') != std::wstring::npos)
        {
            // Typed faster than Everything answers: every prefix at once, only the last one waited for.
            double started = PreciseMs();
            size_t start = 0;
            while (true)
            {
                size_t next = spec.find(L'+', start);
                h.search.Run(spec.substr(start, next == std::wstring::npos ? std::wstring::npos : next - start), false);
                if (next == std::wstring::npos) break;
                start = next + 1;
            }
            h.Wait();
            h.Dump(false);
            wchar_t line[64];
            swprintf_s(line, L"  burst total %.1f ms\r\n", PreciseMs() - started);
            h.log += line;
        }
        else if (spec.find(L'>') != std::wstring::npos)
        {
            // Typed: every prefix in turn, without waiting between keys longer than Everything takes.
            size_t start = 0;
            while (true)
            {
                size_t next = spec.find(L'>', start);
                std::wstring typed = spec.substr(start, next == std::wstring::npos ? std::wstring::npos : next - start);
                h.search.Run(typed, false);
                h.Wait();
                h.Dump(next == std::wstring::npos);
                if (next == std::wstring::npos) break;
                start = next + 1;
            }
        }
        else
        {
            h.search.Run(spec, false);
            h.Wait();
            h.Dump(true);
            h.search.Run(spec, false); // again: from the cache
            h.Wait();
            h.Dump(false);
        }
        h.log += L"\r\n";
    }
    h.search.Shutdown();

    int bytes = WideCharToMultiByte(CP_UTF8, 0, h.log.c_str(), (int)h.log.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, h.log.c_str(), (int)h.log.size(), utf8.data(), bytes, nullptr, nullptr);
    HANDLE file = CreateFileW(argv[1], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 3;
    DWORD written;
    WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(file);
    return 0;
}
