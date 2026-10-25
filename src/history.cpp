#include "history.h"

#include <cmath>
#include <ctime>

namespace
{
constexpr size_t MaxEntries = 300;

std::wstring FilePath() { return LocalDataDir() + L"\\history.txt"; }

std::string ToUtf8(std::wstring_view text)
{
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), bytes, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(std::string_view text)
{
    int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out(chars, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), chars);
    return out;
}
} // namespace

// One line per entry: count \t last-used (unix seconds) \t folder (0/1) \t path
void History::Load()
{
    if (loaded_) return;
    loaded_ = true;
    HANDLE file = CreateFileW(FilePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::string data(size_t(std::min<LONGLONG>(size.QuadPart, 1 << 20)), '\0');
    DWORD read = 0;
    ReadFile(file, data.data(), DWORD(data.size()), &read, nullptr);
    CloseHandle(file);
    data.resize(read);

    size_t start = 0;
    while (start < data.size() && entries_.size() < MaxEntries)
    {
        size_t end = data.find('\n', start);
        if (end == std::string::npos) end = data.size();
        std::string_view line(data.data() + start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        size_t a = line.find('\t'), b = line.find('\t', a + 1), c = line.find('\t', b + 1);
        if (c == std::string_view::npos) continue;
        Entry entry;
        entry.count = uint32_t(strtoul(std::string(line.substr(0, a)).c_str(), nullptr, 10));
        entry.last = _strtoi64(std::string(line.substr(a + 1, b - a - 1)).c_str(), nullptr, 10);
        entry.folder = line.substr(b + 1, c - b - 1) == "1";
        entry.path = FromUtf8(line.substr(c + 1));
        if (entry.count > 0 && !entry.path.empty()) entries_.push_back(std::move(entry));
    }
    Reindex();
}

void History::Reindex()
{
    version_++;
    index_.clear();
    for (size_t i = 0; i < entries_.size(); i++) index_.emplace(PathHash(entries_[i].path), uint32_t(i));
}

void History::Save() const
{
    std::string data;
    for (const Entry& entry : entries_)
    {
        data += std::to_string(entry.count) + '\t' + std::to_string(entry.last) + '\t' + (entry.folder ? '1' : '0') + '\t';
        data += ToUtf8(entry.path);
        data += "\r\n";
    }
    // Write next to the real file and swap, so a crash never leaves half a history.
    std::wstring path = FilePath(), temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    bool ok = WriteFile(file, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(file);
    if (ok) MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    else DeleteFileW(temp.c_str());
}

void History::Record(const std::wstring& path, bool folder)
{
    Load();
    Entry entry;
    for (size_t i = 0; i < entries_.size(); i++)
    {
        if (!EqualsNoCase(entries_[i].path, path)) continue;
        entry = std::move(entries_[i]);
        entries_.erase(entries_.begin() + i);
        break;
    }
    entry.path = path;
    entry.folder = folder;
    entry.count++;
    entry.last = int64_t(time(nullptr));
    entries_.insert(entries_.begin(), std::move(entry));
    if (entries_.size() > MaxEntries) entries_.resize(MaxEntries);
    Reindex();
    Save();
}

const History::Entry* History::Find(uint64_t pathHash) const
{
    auto it = index_.find(pathHash);
    return it == index_.end() ? nullptr : &entries_[it->second];
}

// log2(1 + uses), fading with a one-week half-life-ish curve.
float History::BoostOf(const Entry& entry)
{
    double days = double(time(nullptr) - entry.last) / 86400.0;
    return float(std::log2(1.0 + entry.count) / (1.0 + std::max(0.0, days) / 7.0));
}

// 0 for anything never opened here.
float History::Boost(std::wstring_view dir, std::wstring_view name) const
{
    if (index_.empty()) return 0;
    const Entry* found = Find(PathHash(dir, name));
    if (!found) return 0;
    const Entry& entry = *found;
    // Confirm, so a hash collision never boosts the wrong file.
    std::wstring_view path = entry.path;
    if (name.empty())
    {
        if (!EqualsNoCase(path, dir)) return 0;
    }
    else
    {
        size_t cut = dir.size() + (dir.empty() || dir.back() == L'\\' ? 0 : 1);
        if (path.size() != cut + name.size() || !StartsWithNoCase(path, dir) || !EqualsNoCase(path.substr(cut), name)) return 0;
    }
    return BoostOf(entry);
}
