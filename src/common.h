// Shared includes and small helpers for DeskFind.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <utility>

// Private window messages, shared by the modules that post to the main window.
enum : UINT
{
    WM_APP_EVERYTHING = WM_APP + 1, // lParam: EvReply*, receiver deletes
    WM_APP_ICON,                    // lParam: IconResult*, receiver deletes
    WM_APP_TRAY,                    // tray icon callback
    WM_APP_SHOW,                    // another instance asked us to show the search box
    WM_APP_QUIT,                    // the installer is replacing or removing us
    WM_APP_SETTINGS,                // another instance asked us to open the settings
};

// The main window, which other copies of DeskFind look for.
constexpr wchar_t MainWindowClass[] = L"DeskFind.Search";

// Releases a COM pointer on scope exit.
template <class T>
struct ComPtr
{
    T* p = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { Reset(); }
    void Reset()
    {
        if (p) p->Release();
        p = nullptr;
    }
    T** operator&()
    {
        Reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    T* Get() const { return p; }
};

// Case and diacritic insensitive matching, one UTF-16 unit in, one out, so indices stay aligned.
wchar_t Fold(wchar_t c);
void FoldInto(std::wstring_view text, std::wstring& out);
std::wstring Folded(std::wstring_view text);
wchar_t LowerChar(wchar_t c); // case only, diacritics kept
bool EqualsNoCase(std::wstring_view a, std::wstring_view b); // ordinal, ASCII and Latin case only
bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix);
bool ContainsNoCase(std::wstring_view text, std::wstring_view part);
// Case-insensitive path hashes: PathHash(dir, name) == PathHash(joined) == HashLower(DirHash(dir), name).
constexpr uint64_t HashSeed = 14695981039346656037ull;
uint64_t HashLower(uint64_t hash, std::wstring_view text);
uint64_t DirHash(std::wstring_view dir);
uint64_t PathHash(std::wstring_view dir, std::wstring_view name = {});

std::wstring FormatCount(uint64_t value); // 1,234,567 or 1.234.567, as the UI language writes it
std::wstring KnownFolder(const GUID& id);
std::wstring LocalDataDir();               // %LOCALAPPDATA%\DeskFind, created on demand
void LogError(const wchar_t* text);

inline uint64_t NowMs() { return GetTickCount64(); }
double PreciseMs(); // QueryPerformanceCounter in milliseconds

// Timings for the trace build only (build.cmd trace); compiled out otherwise.
#ifdef DF_TRACE
void Trace(const wchar_t* format, ...);
#define TRACE(...) Trace(__VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif
