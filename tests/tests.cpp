// DeskFindTests.exe: the matching helpers, the Everything reply parser, and the search pipeline over a folder tree made
// for the run (read from disk, so Everything is not needed). Built and run by "build.cmd test"; failures are printed
// and the exit code is their number.
#include "../src/lang.h"
#include "../src/search.h"

#include <objbase.h>
#include <shellapi.h>

#include <cstdio>

namespace
{
int g_checks = 0, g_failures = 0;

std::string Utf8(std::wstring_view text)
{
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

void Check(bool ok, const char* what, int line)
{
    g_checks++;
    if (ok) return;
    g_failures++;
    printf("FAIL tests.cpp:%d: %s\n", line, what);
}
#define CHECK(x) Check((x), #x, __LINE__)

using Names = std::vector<std::wstring>;

std::string Join(const Names& names)
{
    std::string out = "[";
    for (size_t i = 0; i < names.size(); i++) out += (i ? ", " : "") + Utf8(names[i]);
    return out + "]";
}

void CheckNames(const Names& actual, const Names& expected, const char* what, int line)
{
    g_checks++;
    if (actual == expected) return;
    g_failures++;
    printf("FAIL tests.cpp:%d: %s\n  expected %s\n  actual   %s\n", line, what, Join(expected).c_str(), Join(actual).c_str());
}
#define CHECK_NAMES(actual, ...) CheckNames((actual), Names{__VA_ARGS__}, #actual, __LINE__)

// ---------------------------------------------------------------- matching

void TestFolding()
{
    CHECK(Fold(L'A') == L'a');
    CHECK(Folded(L"Café") == L"cafe");
    CHECK(Folded(L"İstanbul ışık") == L"istanbul isik");
    CHECK(Folded(L"Çağrı Öğün") == L"cagri ogun");
    CHECK(Folded(L"Straße") == L"strase"); // one unit out per unit in, so highlight indices stay aligned
    CHECK(Folded(L"Ærø Łódź") == L"aro lodz");
    CHECK(Fold(0x2019) == L'\'');
    CHECK(Fold(0x2013) == L'-');
    CHECK(Folded(L"日本語") == L"日本語");
    CHECK(LowerChar(L'Ü') == L'ü');

    CHECK(EqualsNoCase(L"ÜBER.txt", L"über.TXT"));
    CHECK(!EqualsNoCase(L"uber", L"über")); // case only: paths must not collide on ü/u
    CHECK(StartsWithNoCase(L"C:\\Users\\Ana", L"c:\\users"));
    CHECK(!StartsWithNoCase(L"C:\\", L"C:\\Users"));
    CHECK(ContainsNoCase(L"Annual REPORT.pdf", L"report"));
    CHECK(ContainsNoCase(L"x", L""));
    CHECK(!ContainsNoCase(L"ab", L"abc"));
}

void TestHashes()
{
    uint64_t joined = PathHash(L"C:\\Users\\Ana\\Desktop\\Notes.txt");
    CHECK(PathHash(L"C:\\Users\\Ana\\Desktop", L"Notes.txt") == joined);
    CHECK(HashLower(DirHash(L"C:\\Users\\Ana\\Desktop"), L"Notes.txt") == joined);
    CHECK(PathHash(L"c:\\users\\ana\\desktop\\NOTES.TXT") == joined);
    CHECK(PathHash(L"C:\\Users\\Ana\\Desktop\\Notës.txt") != joined);
    CHECK(PathHash(L"C:\\", L"x.txt") == PathHash(L"C:\\x.txt")); // a root keeps its one backslash
}

void TestCounts()
{
    SetEnvironmentVariableW(L"DESKFIND_LANG", nullptr);
    Lang::Set(1);
    CHECK(FormatCount(0) == L"0");
    CHECK(FormatCount(999) == L"999");
    CHECK(FormatCount(1000) == L"1,000");
    CHECK(FormatCount(1234567) == L"1,234,567");
    Lang::Set(2);
    CHECK(FormatCount(1234567) == L"1.234.567");
    Lang::Set(1);
}

void TestQueryWords()
{
    CHECK_NAMES(Search::Terms(L"  Café   Menü "), L"cafe", L"menu");
    // Everything's operators are left to Everything; only plain words are ranked and highlighted.
    CHECK_NAMES(Search::Terms(L"ext:pdf !draft invoice | receipt"), L"invoice", L"receipt");
    CHECK_NAMES(Search::Terms(L"\"annual report\""), L"annual", L"report");
    CHECK_NAMES(Search::Terms(L"*.psd"), L".psd");
    CHECK(Search::Terms(L"   ").empty());

    CHECK(Search::DisplayName(L"Notes.lnk", false) == L"Notes");
    CHECK(Search::DisplayName(L"Site.URL", false) == L"Site");
    CHECK(Search::DisplayName(L"Tools.lnk", true) == L"Tools.lnk"); // a folder keeps its name
    CHECK(Search::DisplayName(L".lnk", false) == L".lnk");
}

// ---------------------------------------------------------------- the Everything reply

// An EVERYTHING_IPC_LIST2 reply as Everything sends it, with the name and the path of each item.
struct ReplyBytes
{
    struct Item
    {
        uint32_t flags;
        std::wstring name, dir;
    };
    std::vector<Item> items;
    uint32_t total = 0, requestFlags = 0x3;

    std::vector<uint8_t> Build() const
    {
        std::vector<uint8_t> out(20 + items.size() * 8);
        auto put = [&](size_t at, uint32_t value) { memcpy(out.data() + at, &value, 4); };
        put(0, total);
        put(4, uint32_t(items.size()));
        put(12, requestFlags);
        put(16, EvSortName);
        for (size_t i = 0; i < items.size(); i++)
        {
            put(20 + i * 8, items[i].flags);
            put(20 + i * 8 + 4, uint32_t(out.size()));
            for (const std::wstring* text : {&items[i].name, &items[i].dir})
            {
                size_t at = out.size();
                out.resize(at + 4 + (text->size() + 1) * 2);
                put(at, uint32_t(text->size()));
                memcpy(out.data() + at + 4, text->c_str(), (text->size() + 1) * 2);
            }
        }
        return out;
    }
};

bool Parse(const std::vector<uint8_t>& bytes, EvReply& reply)
{
    // As the IPC thread copies it: two spare bytes past the end.
    reply.data.reset(new uint8_t[bytes.size() + 2]());
    memcpy(reply.data.get(), bytes.data(), bytes.size());
    return ParseReply(reply, bytes.size());
}

void TestReplyParser()
{
    ReplyBytes sample;
    sample.total = 7;
    sample.items = {{0, L"report.pdf", L"C:\\Users\\Ana\\Desktop"}, {EvFolder, L"Fotoğraflar", L"D:\\"}};
    std::vector<uint8_t> bytes = sample.Build();

    EvReply reply;
    CHECK(Parse(bytes, reply));
    CHECK(reply.total == 7);
    CHECK(reply.items.size() == 2);
    if (reply.items.size() == 2)
    {
        CHECK(std::wstring_view(reply.items[0].name, reply.items[0].nameLen) == L"report.pdf");
        CHECK(std::wstring_view(reply.items[0].dir, reply.items[0].dirLen) == L"C:\\Users\\Ana\\Desktop");
        CHECK(reply.items[0].flags == 0);
        CHECK(std::wstring_view(reply.items[1].name, reply.items[1].nameLen) == L"Fotoğraflar");
        CHECK(reply.items[1].flags == EvFolder);
        CHECK(reply.items[1].name[reply.items[1].nameLen] == 0);
    }

    ReplyBytes empty;
    EvReply none;
    CHECK(Parse(empty.Build(), none) && none.items.empty());

    // Anything malformed is refused, whatever the claimed sizes: nothing may be read past the buffer.
    for (size_t cut = 1; cut < bytes.size(); cut++)
    {
        EvReply truncated;
        std::vector<uint8_t> shorter(bytes.begin(), bytes.end() - cut);
        if (Parse(shorter, truncated))
        {
            Check(false, "a truncated reply is refused", __LINE__);
            break;
        }
    }
    auto corrupt = [&](size_t at, uint32_t value)
    {
        std::vector<uint8_t> copy = bytes;
        memcpy(copy.data() + at, &value, 4);
        EvReply out;
        return Parse(copy, out);
    };
    CHECK(!corrupt(4, 1000));                  // more items than the buffer holds
    CHECK(!corrupt(4, 0xFFFFFFFF));
    CHECK(!corrupt(12, 0x1));                  // no paths requested
    CHECK(!corrupt(24, uint32_t(bytes.size()))); // an item's data past the end
    CHECK(!corrupt(24, 0xFFFFFFFF));
    CHECK(!corrupt(36, 0x7FFFFFFF));           // a name longer than the buffer (the first item's length field)
    CHECK(!corrupt(36, 0xFFFFFFFF));
}

// ---------------------------------------------------------------- the pipeline

void MakeDir(const std::wstring& path) { CreateDirectoryW(path.c_str(), nullptr); }

void MakeFile(const std::wstring& path, DWORD attributes = FILE_ATTRIBUTE_NORMAL)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, attributes, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}

void RemoveTree(const std::wstring& path)
{
    std::wstring from = path;
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

// The search as the box runs it, with the replies (the folder walk, Everything if it runs) pumped in between.
struct Pipeline
{
    Search search;
    HWND hwnd = nullptr;
    std::wstring root;

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return DefWindowProcW(hwnd, msg, wParam, lParam); }

    template <class Done>
    bool PumpUntil(Done done, double ms)
    {
        double deadline = PreciseMs() + ms;
        while (!done())
        {
            double left = deadline - PreciseMs();
            if (left <= 0) return false;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, DWORD(left) + 1, QS_ALLINPUT);
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_APP_EVERYTHING) search.OnReply(reinterpret_cast<EvReply*>(msg.lParam));
                else DispatchMessageW(&msg);
            }
        }
        return true;
    }

    bool Configure(const SearchOptions& options)
    {
        search.Configure(options);
        search.PrepareIndex(true);
        return PumpUntil([&] { return search.IndexReady(); }, 15000);
    }

    const SearchView& Run(const std::wstring& query)
    {
        search.Run(query, false);
        if (search.View().deferred) search.SendDeferred();
        PumpUntil([&] { return !search.View().pending; }, 5000);
        return search.View();
    }

    // A group's results that lie in the fixture, relative to it, in the order shown. The real desktop is searched too.
    Names Shown(const std::wstring& query, Tier tier)
    {
        Names out;
        for (const TierBlock& block : Run(query).tiers)
            if (block.tier == tier)
                for (const Result& result : block.items)
                    if (StartsWithNoCase(result.path, root + L"\\")) out.push_back(result.path.substr(root.size() + 1));
        return out;
    }
};

std::wstring FixtureRoot()
{
    // Next to the repository's bin folder: a path under AppData or Temp, or with "bin" in it, would rank everything
    // in the fixture as app data or build output.
    wchar_t exe[MAX_PATH * 2];
    DWORD length = GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    std::wstring dir(exe, length);
    for (int up = 0; up < 2; up++) dir.resize(dir.rfind(L'\\'));
    return dir + L"\\testdata-" + std::to_wstring(GetCurrentProcessId());
}

void TestPipeline()
{
    Pipeline p;
    p.root = FixtureRoot();
    const std::wstring& r = p.root;
    RemoveTree(r);
    MakeDir(r);
    for (const wchar_t* dir : {L"\\Projects", L"\\Projects\\bin", L"\\Projects\\Quokkas", L"\\node_modules", L"\\Archive"}) MakeDir(r + dir);
    for (const wchar_t* file : {L"\\quokka.txt", L"\\Quokka notes.pdf", L"\\My quokka.lnk", L"\\Kıyı.txt", L"\\Projects\\quokka.txt",
                                L"\\Projects\\bin\\quokka.txt", L"\\Projects\\old-quokka-draft.docx", L"\\Projects\\squokkas.txt",
                                L"\\node_modules\\quokka.js", L"\\Archive\\quokka.txt"})
        MakeFile(r + file);
    MakeFile(r + L"\\Projects\\quokka-hidden.txt", FILE_ATTRIBUTE_HIDDEN);

    WNDCLASSW wc{};
    wc.lpfnWndProc = Pipeline::Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DeskFind.Tests";
    RegisterClassW(&wc);
    p.hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    p.search.Init(p.hwnd);

    SearchOptions options;
    options.extraFolders = {r};
    options.excludedFolders = {r + L"\\Archive"};
    options.hiddenFiles = 2;
    options.boostOpened = false; // this PC's open history must not reorder the fixture
    CHECK(p.Configure(options));

    // Exact name, then the start of the name, then the start of a word; shortcuts are matched without ".lnk".
    CHECK_NAMES(p.Shown(L"quokka", Tier::Desktop), L"quokka.txt", L"Quokka notes.pdf", L"My quokka.lnk");
    // The same order below the folder, and build output after the user's own file with an equal match. Hidden files,
    // excluded folders and what is inside node_modules never show.
    CHECK_NAMES(p.Shown(L"quokka", Tier::DesktopFolders), L"Projects\\quokka.txt", L"Projects\\bin\\quokka.txt", L"Projects\\Quokkas",
                L"Projects\\old-quokka-draft.docx", L"Projects\\squokkas.txt");
    // Case, accents and the Turkish dotless i don't matter, and words match in any order.
    CHECK_NAMES(p.Shown(L"QUÖKKA", Tier::Desktop), L"quokka.txt", L"Quokka notes.pdf", L"My quokka.lnk");
    CHECK_NAMES(p.Shown(L"kiyi", Tier::Desktop), L"Kıyı.txt");
    CHECK_NAMES(p.Shown(L"KIYI", Tier::Desktop), L"Kıyı.txt");
    CHECK_NAMES(p.Shown(L"draft old", Tier::DesktopFolders), L"Projects\\old-quokka-draft.docx");
    CHECK_NAMES(p.Shown(L"zzqx-nothing", Tier::DesktopFolders));

    // With developer folders shown, node_modules is searched too; hidden files can be turned on.
    options.hideDevFolders = false;
    options.hiddenFiles = 1;
    CHECK(p.Configure(options));
    Names all = p.Shown(L"quokka", Tier::DesktopFolders);
    CHECK(std::find(all.begin(), all.end(), L"node_modules\\quokka.js") != all.end());
    CHECK(std::find(all.begin(), all.end(), L"Projects\\quokka-hidden.txt") != all.end());
    CHECK(std::find(all.begin(), all.end(), L"Archive\\quokka.txt") == all.end());

    p.search.Shutdown();
    DestroyWindow(p.hwnd);
    RemoveTree(r);
}
} // namespace

int wmain()
{
    (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    double started = PreciseMs();
    TestFolding();
    TestHashes();
    TestCounts();
    TestQueryWords();
    TestReplyParser();
    TestPipeline();
    printf("%d checks, %d failed, %.0f ms\n", g_checks, g_failures, PreciseMs() - started);
    CoUninitialize();
    return g_failures;
}
