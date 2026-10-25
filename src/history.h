// What was opened from the search box, so it ranks first next time (frecency) and fills the empty box.
#pragma once
#include "common.h"

#include <unordered_map>

class History
{
public:
    void Load();
    void Record(const std::wstring& path, bool folder);
    void Clear();
    float Boost(std::wstring_view path) const { return Boost(path, {}); }
    float Boost(std::wstring_view dir, std::wstring_view name) const; // without joining them

    struct Entry
    {
        std::wstring path;
        uint32_t count = 0;
        int64_t last = 0; // unix seconds
        bool folder = false;
    };
    bool Empty() const { return entries_.empty(); }
    uint32_t Version() const { return version_; } // changes with every Record
    const Entry* Find(uint64_t pathHash) const; // a candidate: the caller confirms the path
    static float BoostOf(const Entry& entry);
    const std::vector<Entry>& Entries() const { return entries_; } // most recent first

private:
    void Save() const;
    void Reindex();
    std::vector<Entry> entries_;
    std::unordered_map<uint64_t, uint32_t> index_; // path hash -> entry
    uint32_t version_ = 1;
    bool loaded_ = false;
};
