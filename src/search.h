// The three-tier search: the desktop itself, folders under the desktop, then the rest of the PC.
#pragma once
#include "common.h"
#include "everything.h"
#include "history.h"

enum class Tier : uint8_t
{
    Desktop,
    DesktopFolders,
    Elsewhere,
    Recent,
};

struct Result
{
    std::wstring path;   // full path
    uint32_t nameAt = 0; // where the file name starts in path
    uint32_t order = 0;  // Everything's order: most recently modified first
    bool folder = false;
    Tier tier = Tier::Desktop;
    uint8_t score = 4;
    uint8_t place = 0; // 1: app data, build output or logs, ranked after real files of the same score
    float boost = 0;   // from the open history

    std::wstring_view Name() const { return std::wstring_view(path).substr(nameAt); }
    std::wstring_view Dir() const
    {
        size_t end = nameAt;
        if (end > 0 && path[end - 1] == L'\\' && !(end == 3 && path[1] == L':')) end--; // keep "C:\"
        return std::wstring_view(path).substr(0, end);
    }
};

struct TierBlock
{
    Tier tier;
    uint32_t total = 0;
    std::vector<Result> items;
};

struct SearchView
{
    std::wstring query;
    std::vector<TierBlock> tiers;
    uint32_t generation = 0;
    bool pending = false;           // Everything has not answered yet
    bool everythingMissing = false; // Everything is not running
    bool fromCache = false;
    bool deferred = false; // a one-letter query: Everything is asked only if no second letter follows soon
    double elapsedMs = 0;
    double processMs = 0; // our share of elapsedMs: splitting, filtering and ranking Everything's answers
};

class Search
{
public:
    void Init(HWND notify);
    void Shutdown();

    // Recomputes the view; what Everything has to answer arrives later through OnReply.
    // `revalidate` re-asks Everything even when the cache is fresh (the box was just reopened).
    void Run(const std::wstring& query, bool revalidate);
    bool OnReply(EvReply* reply); // true when the view changed
    void Retry() { Run(lastQuery_, true); }
    void SendDeferred(); // the wait for a second letter is over
    // Fetches the whole desktop tree once, so the desktop folders tier is answered locally while the box is open.
    void PrepareIndex();
    bool IndexReady() const { return index_.ready; }
    size_t IndexBytes() const;

    const SearchView& View() const { return view_; }
    History& Recent() { return history_; }
    std::wstring DescribeLocation(std::wstring_view dir) const;
    bool IsOnDesktop(std::wstring_view dir) const;
    void ReleaseMemory();

    // The query's plain words, folded: what the result list highlights.
    static std::vector<std::wstring> Terms(const std::wstring& query) { return Parse(query).terms; }
    static std::wstring_view DisplayName(std::wstring_view name, bool folder); // .lnk and .url shown without extension

private:
    struct Candidate
    {
        std::wstring path;
        uint32_t nameAt;
        uint32_t order;
        bool folder;
    };

    struct CacheEntry
    {
        std::wstring query;
        uint64_t time = 0;
        uint64_t used = 0;
        bool complete = false; // every match Everything has is in here, so longer queries can be filtered locally
        bool plain = false;
        uint32_t desktopTotal = 0, treeTotal = 0, restTotal = 0;
        std::vector<Candidate> desktop, tree, rest; // desktop only for queries the local list can't answer
        size_t Bytes() const;
    };

    struct Query
    {
        std::wstring text;
        bool plain = false;
        std::vector<std::wstring> terms; // folded
        std::vector<std::wstring> lower; // the same words, lower case only: compared the way Everything matches
        std::wstring whole;              // folded terms joined by spaces
    };

    struct Pending
    {
        uint32_t generation = 0;
        Query query;
        std::unique_ptr<EvReply> global, tree, direct;
        bool waitingTree = false, waitingDirect = false;
        bool deferred = false;
    };

    // Everything under the desktops (noise folders left out), compact: one name pool, folders as a tree.
    struct TreeIndex
    {
        static constexpr uint32_t NoParent = 0xFFFFFFFF;
        struct Dir
        {
            uint32_t parent; // NoParent for a desktop itself, whose name is its full path
            uint32_t name;   // offset into names
            uint16_t length;
            uint8_t place;   // Place() of anything inside, before the file's own extension counts
            uint64_t hash;   // DirHash of the full path, for open-history lookups
        };
        struct Entry
        {
            uint32_t name; // offset into names
            uint16_t length;
            uint8_t folder;
            uint8_t minorExtension;
            uint32_t dir;
            uint32_t order; // most recently modified first
        };
        std::vector<Entry> entries;
        std::vector<Dir> dirs;
        std::vector<wchar_t> names;
        bool ready = false;

        std::wstring_view Name(uint32_t offset, uint16_t length) const { return std::wstring_view(names.data() + offset, length); }
        std::wstring DirPath(uint32_t dir) const;
    };
    struct TreeAnswer
    {
        std::wstring query;
        uint32_t history = 0, total = 0;
        std::vector<Result> items;
        bool valid = false;
    };

    static Query Parse(const std::wstring& text);
    void RefreshDesktop(bool force);
    std::vector<Result> LocalDesktop(const Query& query, uint32_t& total) const;
    std::vector<Result> LocalTree(const Query& query, uint32_t& total) const;
    void BuildIndex(const EvReply& reply);
    std::wstring IndexQuery() const;
    bool UseIndex(const Query& query) const { return query.plain && index_.ready; }
    uint8_t ScoreName(std::wstring_view display, bool folder, const Query& query) const;
    static uint8_t ScoreFolded(std::wstring_view folded, size_t stem, std::wstring_view display, const Query& query);
    uint8_t PlaceOf(std::wstring_view dir, std::wstring_view name, bool folder) const;
    void Compose(const CacheEntry& entry, const Query& query);
    std::vector<Result> Rank(const std::vector<Candidate>& items, Tier tier, const Query& query, size_t show) const;
    std::vector<Result> RankResults(std::vector<Result> items, const Query& query, size_t show) const;
    void Score(Result& result, const Query& query) const;
    uint8_t Place(const Result& result) const;
    void ComposeRecent();
    void ComposeLocal(const Query& query);

    CacheEntry* FindCached(const std::wstring& text);
    CacheEntry* FindRefinable(const Query& query);
    CacheEntry Refine(const CacheEntry& source, const Query& query) const;
    void Store(CacheEntry entry);
    void BuildFromReplies();

    bool IsNoise(std::wstring_view dir) const;
    bool IsSystem(std::wstring_view dir) const;
    int DesktopIndex(std::wstring_view dir) const; // -1: not under a desktop, 0..: which desktop
    bool IsDesktopRoot(std::wstring_view dir) const;
    std::wstring TreeQuery(const std::wstring& text) const;
    std::wstring DirectQuery(const std::wstring& text) const;

    Everything everything_;
    History history_;
    SearchView view_;
    Pending pending_;
    std::wstring lastQuery_;
    uint32_t generation_ = 0;
    double startedAt_ = 0;

    std::vector<std::wstring> desktops_; // user desktop first, then the public one
    std::vector<Candidate> desktopItems_;
    uint64_t desktopTime_ = 0;
    std::wstring windowsDir_, tempDir_, recentDir_, appDataDir_, programDataDir_;
    std::wstring startMenuDirs_[2], programDirs_[2];

    std::vector<CacheEntry> cache_;
    uint64_t useCounter_ = 0;
    TreeIndex index_;
    bool indexInFlight_ = false, indexDiscard_ = false;
    mutable TreeAnswer treeAnswer_; // the last query the index answered, reused while Everything's part arrives
};
