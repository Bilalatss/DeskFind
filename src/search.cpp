#include "search.h"

#include <shlobj.h>

#include "lang.h"

namespace
{
// Shown per tier; Everything is asked for more so the name ranking has something to choose from.
constexpr size_t ShowDesktop = 50, ShowTree = 40, ShowRest = 80, ShowRecent = 8;
constexpr uint32_t GlobalFetch = 1000, TreeFetch = 500, DirectFetch = 300, IndexFetch = 400000;
constexpr uint64_t FreshMs = 3000, DesktopListMs = 1500;
constexpr size_t CacheEntries = 48, CacheBytes = 1536 * 1024;

enum : uint32_t
{
    TagGlobal = 1,
    TagTree,
    TagDirect,
    TagIndex,
};

// Queries Everything must answer even after newer typing (generation 0 is never superseded).
constexpr uint32_t Always = 0;

// Folders full of files nobody looks for from the desktop; Ctrl+E in Everything still shows them.
constexpr std::wstring_view NoiseSegments[] = {
    L"node_modules", L".git", L".svn", L".hg", L"__pycache__", L".venv", L".vs",
    L".godot", L".import", L".gradle", L".next", L".nuget", L".cache", L"$Recycle.Bin", L"System Volume Information",
};

// Generated or app-owned places: still shown, but after the user's own files with the same name match.
constexpr std::wstring_view MinorSegments[] = {
    L"bin", L"obj", L"Debug", L"Release", L"build", L"dist", L"out", L"target", L"logs", L"log",
    L"packages", L"vendor", L"site-packages", L"Library", L"cache", L"Temp", L"tmp",
};
constexpr std::wstring_view MinorExtensions[] = {
    L".log", L".tmp", L".pdb", L".obj", L".ilk", L".tlog", L".pyc", L".cache", L".map", L".etl",
};

// Everything treats these as syntax; anything else is a plain substring of the name.
bool IsSyntax(wchar_t c) { return c == L'"' || c == L'*' || c == L'?' || c == L'|' || c == L'!' || c == L'<' || c == L'>' || c == L':' || c == L'\\' || c == L'/'; }

bool IsWordChar(wchar_t c) { return IsCharAlphaNumericW(c) != FALSE; }

bool HasSegment(std::wstring_view dir, std::wstring_view segment)
{
    for (size_t at = 0; at + segment.size() < dir.size() + 1; at++)
    {
        at = dir.find(L'\\', at);
        if (at == std::wstring_view::npos) return false;
        size_t start = at + 1;
        if (start + segment.size() > dir.size()) return false;
        size_t end = start + segment.size();
        if ((end == dir.size() || dir[end] == L'\\') && EqualsNoCase(dir.substr(start, segment.size()), segment)) return true;
    }
    return false;
}

bool HasPrefixDir(std::wstring_view dir, std::wstring_view root)
{
    return !root.empty() && StartsWithNoCase(dir, root) && (dir.size() == root.size() || dir[root.size()] == L'\\');
}

std::wstring_view StripShortcut(std::wstring_view name, bool folder)
{
    if (folder || name.size() < 5) return name;
    std::wstring_view ext = name.substr(name.size() - 4);
    if (EqualsNoCase(ext, L".lnk") || EqualsNoCase(ext, L".url")) return name.substr(0, name.size() - 4);
    return name;
}

std::wstring_view Stem(std::wstring_view name, bool folder)
{
    if (folder) return name;
    size_t dot = name.rfind(L'.');
    return dot == std::wstring_view::npos || dot == 0 ? name : name.substr(0, dot);
}

bool ContainsAll(std::wstring_view foldedName, const std::vector<std::wstring>& terms)
{
    for (const std::wstring& term : terms)
        if (foldedName.find(term) == std::wstring_view::npos) return false;
    return true;
}

std::wstring Quote(const std::wstring& text) { return L"\"" + text + L"\""; }

// Shortens from the middle, keeping the first segment: C:\…\Projects\2024
std::wstring ShortenPath(std::wstring_view path, size_t max)
{
    if (path.size() <= max) return std::wstring(path);
    std::vector<std::wstring_view> parts;
    size_t start = 0;
    while (true)
    {
        size_t slash = path.find(L'\\', start);
        parts.push_back(path.substr(start, slash == std::wstring_view::npos ? std::wstring_view::npos : slash - start));
        if (slash == std::wstring_view::npos) break;
        start = slash + 1;
    }
    if (parts.size() <= 2) return std::wstring(path);
    size_t length = parts[0].size() + 2, keepFrom = parts.size();
    for (size_t i = parts.size() - 1; i > 0; i--)
    {
        if (keepFrom < parts.size() && length + parts[i].size() + 1 > max) break;
        keepFrom = i;
        length += parts[i].size() + 1;
    }
    if (keepFrom == 1) return std::wstring(path);
    std::wstring out(parts[0]);
    out += L"\\…";
    for (size_t i = keepFrom; i < parts.size(); i++)
    {
        out += L'\\';
        out += parts[i];
    }
    return out;
}
} // namespace

size_t Search::CacheEntry::Bytes() const
{
    size_t bytes = sizeof(*this) + query.size() * 2;
    for (auto* list : {&desktop, &tree, &rest})
        for (const Candidate& c : *list) bytes += sizeof(Candidate) + c.path.capacity() * 2;
    return bytes;
}

void Search::Init(HWND notify)
{
    desktops_.push_back(KnownFolder(FOLDERID_Desktop));
    std::wstring common = KnownFolder(FOLDERID_PublicDesktop);
    if (!common.empty() && !EqualsNoCase(common, desktops_[0])) desktops_.push_back(common);

    wchar_t buffer[MAX_PATH + 1];
    if (GetWindowsDirectoryW(buffer, MAX_PATH)) windowsDir_ = buffer;
    DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length)
    {
        wchar_t full[MAX_PATH + 1];
        if (GetLongPathNameW(buffer, full, MAX_PATH)) tempDir_ = full;
        else tempDir_ = buffer;
        while (tempDir_.size() > 3 && tempDir_.back() == L'\\') tempDir_.pop_back();
    }

    appDataDir_ = KnownFolder(FOLDERID_RoamingAppData);
    size_t cut = appDataDir_.rfind(L'\\');
    if (cut != std::wstring::npos) appDataDir_.resize(cut); // ...\AppData, covering Local and Roaming
    programDataDir_ = KnownFolder(FOLDERID_ProgramData);
    recentDir_ = KnownFolder(FOLDERID_Recent);
    startMenuDirs_[0] = KnownFolder(FOLDERID_StartMenu);
    startMenuDirs_[1] = KnownFolder(FOLDERID_CommonStartMenu);
    programDirs_[0] = KnownFolder(FOLDERID_ProgramFiles);
    programDirs_[1] = KnownFolder(FOLDERID_ProgramFilesX86);

    history_.Load();
    everything_.Start(notify);
}

void Search::Shutdown() { everything_.Stop(); }

void Search::ReleaseMemory()
{
    index_ = TreeIndex{};
    indexDiscard_ = indexInFlight_;
    treeAnswer_ = TreeAnswer{};
    cache_.clear();
    cache_.shrink_to_fit();
    desktopItems_.clear();
    desktopItems_.shrink_to_fit();
    desktopTime_ = 0;
}

Search::Query Search::Parse(const std::wstring& text)
{
    Query query;
    query.text = text;
    query.plain = true;
    for (wchar_t c : text)
        if (IsSyntax(c)) query.plain = false;

    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find(L' ', start);
        if (end == std::wstring::npos) end = text.size();
        if (end > start)
        {
            std::wstring_view word(text.data() + start, end - start);
            // Ranking only looks at plain words; operators are left to Everything.
            bool isOperator = word[0] == L'!' || word.find(L':') != std::wstring_view::npos || word == L"|";
            if (!isOperator)
            {
                std::wstring term, lower;
                for (wchar_t c : word)
                    if (c != L'"' && c != L'*' && c != L'?' && c != L'<' && c != L'>' && c != L'|')
                    {
                        term.push_back(Fold(c));
                        lower.push_back(LowerChar(c));
                    }
                if (!term.empty())
                {
                    query.terms.push_back(std::move(term));
                    query.lower.push_back(std::move(lower));
                }
            }
        }
        start = end + 1;
    }
    for (const std::wstring& term : query.terms)
    {
        if (!query.whole.empty()) query.whole += L' ';
        query.whole += term;
    }
    if (query.terms.empty()) query.plain = false;
    return query;
}

int Search::DesktopIndex(std::wstring_view dir) const
{
    for (size_t i = 0; i < desktops_.size(); i++)
        if (HasPrefixDir(dir, desktops_[i])) return int(i);
    return -1;
}

bool Search::IsDesktopRoot(std::wstring_view dir) const
{
    for (const std::wstring& desktop : desktops_)
        if (EqualsNoCase(dir, desktop)) return true;
    return false;
}

bool Search::IsOnDesktop(std::wstring_view dir) const { return IsDesktopRoot(dir); }

bool Search::IsNoise(std::wstring_view dir) const
{
    for (std::wstring_view segment : NoiseSegments)
        if (HasSegment(dir, segment)) return true;
    return false;
}

// Windows itself, temp files, and Recent: shortcuts Windows makes to every opened file, copies of real ones.
bool Search::IsSystem(std::wstring_view dir) const
{
    return HasPrefixDir(dir, windowsDir_) || HasPrefixDir(dir, tempDir_) || HasPrefixDir(dir, recentDir_);
}

std::wstring Search::TreeQuery(const std::wstring& text) const
{
    // Path terms first: Everything narrows to the desktop folders before it compares names.
    std::wstring query = L"<";
    for (size_t i = 0; i < desktops_.size(); i++)
    {
        if (i) query += L" | ";
        query += Quote(desktops_[i] + L"\\");
    }
    query += L"> " + text;
    for (std::wstring_view segment : NoiseSegments)
        query += L" !\"\\" + std::wstring(segment) + L"\\\"";
    return query;
}

std::wstring Search::DirectQuery(const std::wstring& text) const
{
    std::wstring query = L"<";
    for (size_t i = 0; i < desktops_.size(); i++)
    {
        if (i) query += L" | ";
        query += L"parent:" + Quote(desktops_[i]);
    }
    return query + L"> " + text;
}

void Search::RefreshDesktop(bool force)
{
    if (!force && desktopTime_ && NowMs() - desktopTime_ < DesktopListMs) return;
    desktopTime_ = NowMs();
    desktopItems_.clear();

    // Show what Explorer shows on the desktop: hidden files only if Explorer is set to show them.
    DWORD hidden = 2, superHidden = 0, size = sizeof(DWORD);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"Hidden", RRF_RT_REG_DWORD, nullptr, &hidden, &size);
    size = sizeof(DWORD);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowSuperHidden", RRF_RT_REG_DWORD, nullptr, &superHidden, &size);

    struct Found
    {
        Candidate candidate;
        uint64_t written;
    };
    std::vector<Found> found;
    for (const std::wstring& desktop : desktops_)
    {
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileExW((desktop + L"\\*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) continue;
        do
        {
            std::wstring_view name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            DWORD attributes = data.dwFileAttributes;
            bool isHidden = (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
            bool isSystem = (attributes & FILE_ATTRIBUTE_SYSTEM) != 0;
            if (isHidden && isSystem && superHidden != 1) continue;
            if (isHidden && hidden != 1) continue;
            if (EqualsNoCase(name, L"desktop.ini") || EqualsNoCase(name, L"thumbs.db")) continue;
            Found f;
            f.candidate.path = desktop + L"\\" + std::wstring(name);
            f.candidate.nameAt = uint32_t(desktop.size() + 1);
            f.candidate.folder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            f.candidate.order = 0;
            f.written = (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
            found.push_back(std::move(f));
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.written > b.written; });
    desktopItems_.reserve(found.size());
    for (size_t i = 0; i < found.size(); i++)
    {
        found[i].candidate.order = uint32_t(i);
        desktopItems_.push_back(std::move(found[i].candidate));
    }
}

// 0 exact, 1 prefix, 2 word start, 3 contains, 4 matched only through the path or operators.
// `folded` is the folded display name and `stem` its length without the extension.
uint8_t Search::ScoreFolded(std::wstring_view folded, size_t stem, std::wstring_view display, const Query& query)
{
    if (query.terms.empty()) return 4;
    std::wstring_view whole = query.whole;
    if (folded == whole || folded.substr(0, stem) == whole) return 0;
    if (folded.substr(0, whole.size()) == whole) return 1;
    size_t at = folded.find(whole);
    if (at != std::wstring_view::npos && at > 0 && !IsWordChar(display[at - 1])) return 2;
    if (at != std::wstring_view::npos) return 3;
    return ContainsAll(folded, query.terms) ? 3 : 4;
}

uint8_t Search::ScoreName(std::wstring_view display, bool folder, const Query& query) const
{
    thread_local std::wstring folded;
    FoldInto(display, folded);
    return ScoreFolded(folded, Stem(display, folder).size(), display, query);
}

void Search::Score(Result& result, const Query& query) const
{
    result.score = ScoreName(StripShortcut(result.Name(), result.folder), result.folder, query);
}

uint8_t Search::PlaceOf(std::wstring_view dir, std::wstring_view name, bool folder) const
{
    // Start menu shortcuts are how apps are found, although they live under AppData and ProgramData.
    for (const std::wstring& menu : startMenuDirs_)
        if (HasPrefixDir(dir, menu)) return 0;
    if (HasPrefixDir(dir, appDataDir_) || HasPrefixDir(dir, programDataDir_)) return 1;
    // In Program Files the programs are what people look for, not their parts.
    for (const std::wstring& programs : programDirs_)
        if (HasPrefixDir(dir, programs))
        {
            if (folder || name.empty()) return 0;
            std::wstring_view ext = name.size() > 4 ? name.substr(name.size() - 4) : std::wstring_view{};
            return EqualsNoCase(ext, L".exe") || EqualsNoCase(ext, L".lnk") ? 0 : 1;
        }
    // Folders starting with a dot are app settings and caches (.vscode, .android, .antigravity).
    for (size_t at = dir.find(L"\\."); at != std::wstring_view::npos; at = dir.find(L"\\.", at + 1))
        if (at + 2 < dir.size() && dir[at + 2] != L'.') return 1;
    for (std::wstring_view segment : MinorSegments)
        if (HasSegment(dir, segment)) return 1;
    if (!folder)
    {
        for (std::wstring_view ext : MinorExtensions)
            if (name.size() > ext.size() && EqualsNoCase(name.substr(name.size() - ext.size()), ext)) return 1;
    }
    return 0;
}

uint8_t Search::Place(const Result& result) const
{
    if (result.tier == Tier::Desktop || result.tier == Tier::Recent) return 0;
    return PlaceOf(result.Dir(), result.Name(), result.folder);
}

std::vector<Result> Search::RankResults(std::vector<Result> items, const Query& query, size_t show) const
{
    for (Result& result : items)
    {
        Score(result, query);
        result.place = Place(result);
        result.boost = history_.Boost(result.path);
    }
    // Best name match, then what was opened here before, then real files before generated ones,
    // then Everything's order (most recently modified first).
    auto better = [](const Result& a, const Result& b)
    {
        if (a.score != b.score) return a.score < b.score;
        if (a.boost != b.boost) return a.boost > b.boost;
        if (a.place != b.place) return a.place < b.place;
        return a.order < b.order;
    };
    if (items.size() > show)
    {
        std::partial_sort(items.begin(), items.begin() + show, items.end(), better);
        items.resize(show);
    }
    else
    {
        std::sort(items.begin(), items.end(), better);
    }
    return items;
}

std::vector<Result> Search::Rank(const std::vector<Candidate>& items, Tier tier, const Query& query, size_t show) const
{
    std::vector<Result> results;
    results.reserve(items.size());
    for (const Candidate& c : items)
    {
        Result r;
        r.path = c.path;
        r.nameAt = c.nameAt;
        r.order = c.order;
        r.folder = c.folder;
        r.tier = tier;
        results.push_back(std::move(r));
    }
    return RankResults(std::move(results), query, show);
}

std::wstring Search::TreeIndex::DirPath(uint32_t dir) const
{
    thread_local std::vector<uint32_t> chain;
    chain.clear();
    // Parents are always added before their children, so the walk ends (and never loops).
    for (uint32_t at = dir; at != NoParent && chain.size() <= dirs.size(); at = dirs[at].parent) chain.push_back(at);
    std::wstring path;
    for (size_t i = chain.size(); i-- > 0;)
    {
        const Dir& d = dirs[chain[i]];
        if (!path.empty() && path.back() != L'\\') path.push_back(L'\\');
        path.append(names.data() + d.name, d.length);
    }
    return path;
}

std::vector<Result> Search::LocalTree(const Query& query, uint32_t& total) const
{
    if (treeAnswer_.valid && treeAnswer_.query == query.text && treeAnswer_.history == history_.Version())
    {
        total = treeAnswer_.total;
        return treeAnswer_.items;
    }
    // Rank on the compact entries; only the shown ones become full results.
    struct Hit
    {
        uint8_t score, place;
        float boost;
        uint32_t order, entry;
    };
    std::vector<Hit> hits;
    thread_local std::wstring folded;
    bool history = !history_.Empty();
    for (uint32_t i = 0; i < index_.entries.size(); i++)
    {
        const TreeIndex::Entry& e = index_.entries[i];
        std::wstring_view name = index_.Name(e.name, e.length);
        FoldInto(name, folded);
        if (!ContainsAll(folded, query.terms)) continue;
        const TreeIndex::Dir& dir = index_.dirs[e.dir];
        bool folder = e.folder != 0;
        std::wstring_view display = StripShortcut(name, folder);
        Hit hit;
        hit.score = ScoreFolded(std::wstring_view(folded).substr(0, display.size()), Stem(display, folder).size(), display, query);
        hit.place = dir.place | e.minorExtension;
        hit.boost = 0;
        if (history)
            if (const History::Entry* opened = history_.Find(HashLower(dir.hash, name)))
            {
                std::wstring path = index_.DirPath(e.dir);
                if (path.back() != L'\\') path.push_back(L'\\');
                path += name;
                if (EqualsNoCase(path, opened->path)) hit.boost = History::BoostOf(*opened);
            }
        hit.order = e.order;
        hit.entry = i;
        hits.push_back(hit);
    }
    total = uint32_t(hits.size());
    auto better = [](const Hit& a, const Hit& b)
    {
        if (a.score != b.score) return a.score < b.score;
        if (a.boost != b.boost) return a.boost > b.boost;
        if (a.place != b.place) return a.place < b.place;
        return a.order < b.order;
    };
    size_t show = std::min(hits.size(), ShowTree);
    std::partial_sort(hits.begin(), hits.begin() + show, hits.end(), better);
    std::vector<Result> results;
    results.reserve(show);
    for (size_t i = 0; i < show; i++)
    {
        const TreeIndex::Entry& e = index_.entries[hits[i].entry];
        Result r;
        r.path = index_.DirPath(e.dir);
        if (r.path.back() != L'\\') r.path.push_back(L'\\');
        r.nameAt = uint32_t(r.path.size());
        r.path += index_.Name(e.name, e.length);
        r.order = e.order;
        r.folder = e.folder != 0;
        r.tier = Tier::DesktopFolders;
        r.score = hits[i].score;
        r.place = hits[i].place;
        r.boost = hits[i].boost;
        results.push_back(std::move(r));
    }
    treeAnswer_ = TreeAnswer{query.text, history_.Version(), total, results, true};
    return results;
}

std::wstring Search::IndexQuery() const
{
    std::wstring query = L"<";
    for (size_t i = 0; i < desktops_.size(); i++)
    {
        if (i) query += L" | ";
        query += Quote(desktops_[i] + L"\\");
    }
    query += L">";
    for (std::wstring_view segment : NoiseSegments)
        query += L" !\"\\" + std::wstring(segment) + L"\\\"";
    return query;
}

size_t Search::IndexBytes() const
{
    return index_.entries.capacity() * sizeof(TreeIndex::Entry) + index_.dirs.capacity() * sizeof(TreeIndex::Dir) +
           index_.names.capacity() * sizeof(wchar_t);
}

void Search::PrepareIndex()
{
    if (index_.ready) return;
    if (indexInFlight_)
    {
        indexDiscard_ = false; // reopened before the answer came: it is wanted again
        return;
    }
    if (!Everything::IsRunning()) return;
    indexInFlight_ = true;
    everything_.Query(Always, TagIndex, IndexQuery(), IndexFetch, EvSortDateModifiedDesc);
}

void Search::BuildIndex(const EvReply& reply)
{
    TreeIndex index;
    index.entries.reserve(reply.items.size());
    size_t chars = 0;
    for (const EvItem& item : reply.items) chars += item.nameLen;
    index.names.reserve(chars + chars / 8);

    // Folders become tree nodes (their own name plus a parent), interned with every parent up to a desktop.
    std::unordered_map<std::wstring_view, uint32_t> known;
    auto intern = [&](auto& self, std::wstring_view dir) -> uint32_t
    {
        auto found = known.find(dir);
        if (found != known.end()) return found->second;
        TreeIndex::Dir node{};
        std::wstring_view segment = dir;
        size_t cut = dir.rfind(L'\\');
        node.parent = TreeIndex::NoParent;
        if (!IsDesktopRoot(dir) && cut != std::wstring_view::npos && cut > 2)
        {
            node.parent = self(self, dir.substr(0, cut));
            segment = dir.substr(cut + 1);
        }
        node.name = uint32_t(index.names.size());
        node.length = uint16_t(std::min<size_t>(segment.size(), 0xFFFF));
        index.names.insert(index.names.end(), segment.begin(), segment.begin() + node.length);
        node.place = PlaceOf(dir, {}, true);
        node.hash = DirHash(dir);
        uint32_t id = uint32_t(index.dirs.size());
        index.dirs.push_back(node);
        known.emplace(dir, id);
        return id;
    };

    uint32_t order = 0;
    for (const EvItem& item : reply.items)
    {
        order++;
        std::wstring_view dir(item.dir, item.dirLen), name(item.name, item.nameLen);
        // The desktop itself comes from the local listing; noise is excluded by the query already.
        if ((item.flags & EvRoot) || item.nameLen > 0xFFFF || IsDesktopRoot(dir) || IsNoise(dir)) continue;
        if (EqualsNoCase(name, L"desktop.ini") || EqualsNoCase(name, L"thumbs.db")) continue;
        TreeIndex::Entry entry;
        entry.dir = intern(intern, dir);
        entry.name = uint32_t(index.names.size());
        entry.length = uint16_t(item.nameLen);
        entry.folder = (item.flags & EvFolder) ? 1 : 0;
        entry.minorExtension = !entry.folder && PlaceOf({}, name, false) ? 1 : 0;
        entry.order = order;
        index.names.insert(index.names.end(), item.name, item.name + item.nameLen);
        index.entries.push_back(entry);
    }
    index.entries.shrink_to_fit();
    index.dirs.shrink_to_fit();
    index.names.shrink_to_fit();
    treeAnswer_.valid = false;
    index.ready = true;
    index_ = std::move(index);
}

void Search::SendDeferred()
{
    if (!pending_.deferred || pending_.generation != generation_) return;
    pending_.deferred = false;
    view_.deferred = false;
    startedAt_ = PreciseMs(); // the wait for a second letter is not search time
    everything_.Query(pending_.generation, TagGlobal, pending_.query.text, GlobalFetch, EvSortDateModifiedDesc);
}

std::vector<Result> Search::LocalDesktop(const Query& query, uint32_t& total) const
{
    std::vector<Result> matches;
    std::wstring folded;
    for (const Candidate& c : desktopItems_)
    {
        FoldInto(std::wstring_view(c.path).substr(c.nameAt), folded);
        if (!ContainsAll(folded, query.terms)) continue;
        Result r;
        r.path = c.path;
        r.nameAt = c.nameAt;
        r.order = c.order;
        r.folder = c.folder;
        r.tier = Tier::Desktop;
        matches.push_back(std::move(r));
    }
    total = uint32_t(matches.size());
    return RankResults(std::move(matches), query, ShowDesktop);
}

void Search::Compose(const CacheEntry& entry, const Query& query)
{
    view_.tiers.clear();
    TierBlock desktop{Tier::Desktop};
    if (query.plain) desktop.items = LocalDesktop(query, desktop.total);
    else
    {
        desktop.items = Rank(entry.desktop, Tier::Desktop, query, ShowDesktop);
        desktop.total = entry.desktopTotal;
    }
    TierBlock tree{Tier::DesktopFolders};
    if (UseIndex(query)) tree.items = LocalTree(query, tree.total);
    else
    {
        tree.items = Rank(entry.tree, Tier::DesktopFolders, query, ShowTree);
        tree.total = entry.treeTotal;
    }
    TierBlock rest{Tier::Elsewhere, entry.restTotal, Rank(entry.rest, Tier::Elsewhere, query, ShowRest)};
    for (TierBlock* block : {&desktop, &tree, &rest})
        if (!block->items.empty()) view_.tiers.push_back(std::move(*block));
}

void Search::ComposeRecent()
{
    view_.tiers.clear();
    TierBlock recent{Tier::Recent};
    // Only fixed disks are checked for files that are gone: a sleeping USB stick or a lost network drive
    // could stall the UI thread for seconds. Twice the list length is the most it looks at.
    UINT driveType[26] = {};
    size_t looked = 0;
    for (const History::Entry& entry : history_.Entries())
    {
        if (recent.items.size() >= ShowRecent || looked++ >= ShowRecent * 2) break;
        const std::wstring& path = entry.path;
        bool drive = path.size() > 2 && path[1] == L':' && path[2] == L'\\' && LowerChar(path[0]) >= L'a' && LowerChar(path[0]) <= L'z';
        if (drive)
        {
            UINT& type = driveType[LowerChar(path[0]) - L'a'];
            if (!type)
            {
                wchar_t root[] = {path[0], L':', L'\\', 0};
                type = GetDriveTypeW(root);
            }
            if (type == DRIVE_FIXED && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            if (type == DRIVE_NO_ROOT_DIR) continue;
        }
        size_t slash = entry.path.rfind(L'\\');
        if (slash == std::wstring::npos || slash + 1 >= entry.path.size()) continue;
        Result r;
        r.path = entry.path;
        r.nameAt = uint32_t(slash + 1);
        r.folder = entry.folder;
        // Recent items on the desktop keep the Desktop tier (Ctrl+Enter selects them there) under the one header.
        r.tier = IsDesktopRoot(r.Dir()) ? Tier::Desktop : Tier::Recent;
        r.order = uint32_t(recent.items.size());
        recent.items.push_back(std::move(r));
    }
    recent.total = uint32_t(recent.items.size());
    if (!recent.items.empty()) view_.tiers.push_back(std::move(recent));
}

Search::CacheEntry* Search::FindCached(const std::wstring& text)
{
    for (CacheEntry& entry : cache_)
        if (entry.query == text)
        {
            entry.used = ++useCounter_;
            return &entry;
        }
    return nullptr;
}

Search::CacheEntry* Search::FindRefinable(const Query& query)
{
    // A complete answer for a looser query holds every match of this one: "dro" covers "drone" and "drone pro".
    // Coverage is checked on lower case only, never through our wider folding (ı/i, ß/s): Everything
    // tells those apart, so "sı" does not cover "si".
    CacheEntry* best = nullptr;
    size_t bestSize = SIZE_MAX;
    for (CacheEntry& entry : cache_)
    {
        if (!entry.complete || !entry.plain) continue;
        Query source = Parse(entry.query);
        bool covered = true;
        for (const std::wstring& term : source.lower)
        {
            bool inSome = false;
            for (const std::wstring& mine : query.lower)
                if (mine.find(term) != std::wstring::npos) inSome = true;
            if (!inSome) covered = false;
        }
        size_t size = entry.tree.size() + entry.rest.size();
        if (covered && size < bestSize)
        {
            best = &entry;
            bestSize = size;
        }
    }
    if (best) best->used = ++useCounter_;
    return best;
}

Search::CacheEntry Search::Refine(const CacheEntry& source, const Query& query) const
{
    CacheEntry entry;
    entry.query = query.text;
    entry.time = source.time;
    entry.complete = true;
    entry.plain = true;
    std::wstring folded;
    auto filter = [&](const std::vector<Candidate>& from, std::vector<Candidate>& to)
    {
        for (const Candidate& c : from)
        {
            FoldInto(std::wstring_view(c.path).substr(c.nameAt), folded);
            if (ContainsAll(folded, query.terms)) to.push_back(c);
        }
    };
    filter(source.tree, entry.tree);
    filter(source.rest, entry.rest);
    entry.treeTotal = uint32_t(entry.tree.size());
    entry.restTotal = uint32_t(entry.rest.size());
    return entry;
}

void Search::Store(CacheEntry entry)
{
    entry.used = ++useCounter_;
    for (CacheEntry& existing : cache_)
        if (existing.query == entry.query)
        {
            existing = std::move(entry);
            return;
        }
    cache_.push_back(std::move(entry));
    // Least recently used out, by count and by memory.
    size_t bytes = 0;
    for (const CacheEntry& e : cache_) bytes += e.Bytes();
    while (cache_.size() > 1 && (cache_.size() > CacheEntries || bytes > CacheBytes))
    {
        auto oldest = std::min_element(cache_.begin(), cache_.end(), [](const CacheEntry& a, const CacheEntry& b) { return a.used < b.used; });
        bytes -= oldest->Bytes();
        cache_.erase(oldest);
    }
}

void Search::Run(const std::wstring& rawQuery, bool revalidate)
{
    std::wstring text = rawQuery;
    while (!text.empty() && text.back() == L' ') text.pop_back();
    while (!text.empty() && text.front() == L' ') text.erase(text.begin());
    lastQuery_ = text;

    // The rest-of-PC part of the previous answer, to bridge the wait for Everything.
    TierBlock previousRest{Tier::Elsewhere};
    bool previousPlain = !view_.query.empty() && Parse(view_.query).plain;
    for (TierBlock& block : view_.tiers)
        if (block.tier == Tier::Elsewhere) previousRest = std::move(block);

    uint32_t generation = ++generation_;
    everything_.SetGeneration(generation);
    startedAt_ = PreciseMs();
    view_.query = text;
    view_.generation = generation;
    view_.pending = false;
    view_.fromCache = false;
    view_.everythingMissing = false;
    view_.deferred = false;
    view_.processMs = 0;
    pending_ = Pending{};

    if (text.empty())
    {
        ComposeRecent();
        view_.elapsedMs = PreciseMs() - startedAt_;
        return;
    }

    Query query = Parse(text);
    RefreshDesktop(revalidate);

    bool fresh = false;
    if (CacheEntry* hit = FindCached(text))
    {
        Compose(*hit, query);
        view_.fromCache = true;
        fresh = !revalidate && NowMs() - hit->time < FreshMs;
    }
    else if (CacheEntry* source = query.plain ? FindRefinable(query) : nullptr)
    {
        CacheEntry refined = Refine(*source, query);
        fresh = !revalidate && NowMs() - refined.time < FreshMs;
        Compose(refined, query);
        Store(std::move(refined));
        view_.fromCache = true;
    }
    else
    {
        ComposeLocal(query);
        // Typing on: the previous results that still match stay while Everything works, so the list does not flash.
        if (query.plain && previousPlain && !previousRest.items.empty())
        {
            std::vector<Result> kept;
            std::wstring folded;
            for (Result& r : previousRest.items)
            {
                FoldInto(r.Name(), folded);
                if (ContainsAll(folded, query.terms)) kept.push_back(std::move(r));
            }
            if (!kept.empty())
            {
                TierBlock rest{Tier::Elsewhere, uint32_t(kept.size()), RankResults(std::move(kept), query, ShowRest)};
                view_.tiers.push_back(std::move(rest));
            }
        }
    }
    view_.elapsedMs = PreciseMs() - startedAt_;
    if (fresh) return;

    if (!Everything::IsRunning())
    {
        view_.everythingMissing = true;
        return;
    }
    view_.pending = true;
    pending_.generation = generation;
    pending_.query = std::move(query);
    // One letter matches most of the disk and a second letter usually follows: the desktop tiers are local,
    // and Everything is asked only if typing pauses.
    if (pending_.query.plain && text.size() == 1 && !revalidate)
    {
        pending_.deferred = view_.deferred = true;
        return;
    }
    TRACE(L"  send gen %u '%ls'", generation, text.c_str());
    everything_.Query(generation, TagGlobal, text, GlobalFetch, EvSortDateModifiedDesc);
}

// What is known before Everything answers: the desktop, and its folders once the index is there.
void Search::ComposeLocal(const Query& query)
{
    view_.tiers.clear();
    if (!query.plain) return;
    TierBlock desktop{Tier::Desktop}, tree{Tier::DesktopFolders};
    desktop.items = LocalDesktop(query, desktop.total);
    if (UseIndex(query)) tree.items = LocalTree(query, tree.total);
    for (TierBlock* block : {&desktop, &tree})
        if (!block->items.empty()) view_.tiers.push_back(std::move(*block));
}

bool Search::OnReply(EvReply* raw)
{
    std::unique_ptr<EvReply> reply(raw);
    TRACE(L"  reply tag %u gen %u (current %u) ok=%d notReady=%d items=%zu total=%u", reply->tag, reply->generation, pending_.generation,
          reply->ok, reply->notReady, reply->items.size(), reply->total);
    if (reply->tag == TagIndex)
    {
        indexInFlight_ = false;
        // Hidden and trimmed meanwhile: the answer is not wanted any more.
        if (!reply->ok || indexDiscard_)
        {
            indexDiscard_ = false;
            return false;
        }
        BuildIndex(*reply);
        TRACE(L"index: %zu entries, %zu folders, %zu KB", index_.entries.size(), index_.dirs.size(), IndexBytes() / 1024);
        // Show the folders tier from the index right away if the current query can use it.
        if (view_.query.empty()) return false;
        Query query = Parse(view_.query);
        if (!UseIndex(query)) return false;
        if (view_.pending)
        {
            if (!view_.fromCache) ComposeLocal(query);
            return true;
        }
        if (CacheEntry* hit = FindCached(view_.query)) Compose(*hit, query);
        return true;
    }
    if (reply->generation != pending_.generation || !view_.pending) return false;

    if (!reply->ok)
    {
        // Everything vanished, timed out or is still loading: keep what is shown, say so.
        view_.pending = false;
        view_.everythingMissing = reply->notReady || !Everything::IsRunning();
        view_.elapsedMs = PreciseMs() - startedAt_;
        return true;
    }

    const std::wstring& text = pending_.query.text;
    switch (reply->tag)
    {
    case TagGlobal:
        pending_.global = std::move(reply);
        if (pending_.global->total > pending_.global->items.size() && !UseIndex(pending_.query))
        {
            // Too many matches to split locally: ask for the desktop folders on their own.
            pending_.waitingTree = true;
            everything_.Query(pending_.generation, TagTree, TreeQuery(text), TreeFetch, EvSortDateModifiedDesc);
            return false;
        }
        break;
    case TagTree:
        pending_.tree = std::move(reply);
        pending_.waitingTree = false;
        if (!pending_.query.plain && pending_.tree->total > pending_.tree->items.size())
        {
            pending_.waitingDirect = true;
            everything_.Query(pending_.generation, TagDirect, DirectQuery(text), DirectFetch, EvSortDateModifiedDesc);
            return false;
        }
        break;
    case TagDirect:
        pending_.direct = std::move(reply);
        pending_.waitingDirect = false;
        break;
    default:
        return false;
    }
    if (pending_.waitingTree || pending_.waitingDirect) return false;
    double started = PreciseMs();
    BuildFromReplies();
    view_.processMs = PreciseMs() - started;
    return true;
}

void Search::BuildFromReplies()
{
    const Query& query = pending_.query;
    CacheEntry entry;
    entry.query = query.text;
    entry.time = NowMs();
    entry.plain = query.plain;

    auto make = [](const EvItem& item, uint32_t order)
    {
        Candidate c;
        c.path.reserve(size_t(item.dirLen) + item.nameLen + 1);
        c.path.assign(item.dir, item.dirLen);
        if (!c.path.empty() && c.path.back() != L'\\') c.path.push_back(L'\\');
        c.nameAt = uint32_t(c.path.size());
        c.path.append(item.name, item.nameLen);
        c.order = order;
        c.folder = (item.flags & EvFolder) != 0;
        return c;
    };
    auto skipName = [](const EvItem& item)
    {
        std::wstring_view name(item.name, item.nameLen);
        return EqualsNoCase(name, L"desktop.ini") || EqualsNoCase(name, L"thumbs.db");
    };

    const EvReply& global = *pending_.global;
    bool complete = global.total <= global.items.size();
    uint32_t order = 0, directCount = 0, treeCount = 0, restCount = 0;
    for (const EvItem& item : global.items)
    {
        order++;
        if ((item.flags & EvRoot) || skipName(item)) continue;
        std::wstring_view dir(item.dir, item.dirLen);
        if (DesktopIndex(dir) >= 0)
        {
            if (IsDesktopRoot(dir))
            {
                directCount++;
                if (!query.plain && complete) entry.desktop.push_back(make(item, order));
            }
            else if (!IsNoise(dir))
            {
                treeCount++;
                if (complete) entry.tree.push_back(make(item, order));
            }
            continue;
        }
        if (IsNoise(dir) || IsSystem(dir)) continue;
        restCount++;
        entry.rest.push_back(make(item, order));
    }

    if (complete)
    {
        entry.complete = true;
        entry.desktopTotal = directCount;
        entry.treeTotal = treeCount;
        entry.restTotal = restCount;
    }
    else
    {
        // The rest of the PC is what Everything counts beyond the desktop folders; noise included, as in Everything itself.
        const EvReply* tree = pending_.tree.get();
        uint32_t treeDirect = 0;
        if (tree)
        {
            uint32_t treeOrder = 0;
            for (const EvItem& item : tree->items)
            {
                treeOrder++;
                if ((item.flags & EvRoot) || skipName(item)) continue;
                std::wstring_view dir(item.dir, item.dirLen);
                if (IsDesktopRoot(dir))
                {
                    treeDirect++;
                    if (!query.plain) entry.desktop.push_back(make(item, treeOrder));
                }
                else if (!IsNoise(dir))
                {
                    entry.tree.push_back(make(item, treeOrder));
                }
            }
            bool treeComplete = tree->total <= tree->items.size();
            entry.desktopTotal = treeDirect;
            entry.treeTotal = treeComplete ? uint32_t(entry.tree.size()) : tree->total - std::min(tree->total, treeDirect);
            entry.restTotal = global.total - std::min(global.total, tree->total);
        }
        else
        {
            // The folders tier comes from the index: count what it and the desktop take out of Everything's total.
            uint32_t local = 0, desktop = 0;
            if (UseIndex(query))
            {
                LocalTree(query, local);
                LocalDesktop(query, desktop);
            }
            entry.restTotal = global.total - std::min(global.total, local + desktop);
        }
        if (const EvReply* direct = pending_.direct.get())
        {
            entry.desktop.clear();
            uint32_t directOrder = 0;
            for (const EvItem& item : direct->items)
            {
                directOrder++;
                if (!(item.flags & EvRoot) && !skipName(item)) entry.desktop.push_back(make(item, directOrder));
            }
            entry.desktopTotal = direct->total;
        }
        // Not refinable, so only what can be shown is worth keeping.
        auto trim = [&](std::vector<Candidate>& list, Tier tier, size_t show)
        {
            std::vector<Result> ranked = Rank(list, tier, query, show);
            std::vector<Candidate> kept;
            kept.reserve(ranked.size());
            for (Result& r : ranked) kept.push_back(Candidate{std::move(r.path), r.nameAt, r.order, r.folder});
            list = std::move(kept);
        };
        trim(entry.desktop, Tier::Desktop, ShowDesktop);
        trim(entry.tree, Tier::DesktopFolders, ShowTree);
        trim(entry.rest, Tier::Elsewhere, ShowRest);
    }

    Compose(entry, query);
    Store(std::move(entry));
    view_.pending = false;
    view_.fromCache = false;
    view_.elapsedMs = PreciseMs() - startedAt_;
    pending_.global.reset();
    pending_.tree.reset();
    pending_.direct.reset();
}

std::wstring Search::DescribeLocation(std::wstring_view dir) const
{
    for (size_t i = 0; i < desktops_.size(); i++)
    {
        const std::wstring& desktop = desktops_[i];
        const wchar_t* label = i == 0 ? Lang::Get().desktop : Lang::Get().publicDesktop;
        if (EqualsNoCase(dir, desktop)) return label;
        if (HasPrefixDir(dir, desktop))
        {
            std::wstring inner = ShortenPath(dir.substr(desktop.size() + 1), 56), out = label;
            out += L" › ";
            for (wchar_t c : inner)
            {
                if (c == L'\\') out += L" › ";
                else out.push_back(c);
            }
            return out;
        }
    }
    return ShortenPath(dir, 72);
}

std::wstring_view Search::DisplayName(std::wstring_view name, bool folder) { return StripShortcut(name, folder); }
