#include "common.h"

#include <shlobj.h>

#include "lang.h"

namespace
{
// Folding for U+0000..U+04FF (Latin, Greek, Cyrillic); built once from the OS normalization and case tables.
constexpr wchar_t FoldTableSize = 0x500;

struct FoldTable
{
    wchar_t map[FoldTableSize];   // case and diacritics removed
    wchar_t lower[FoldTableSize]; // case only

    FoldTable()
    {
        for (wchar_t c = 0; c < FoldTableSize; c++)
        {
            wchar_t base = c;
            if (c >= 0xC0)
            {
                wchar_t decomposed[8];
                int n = NormalizeString(NormalizationD, &c, 1, decomposed, 8);
                if (n >= 1) base = decomposed[0];
            }
            wchar_t folded = base;
            LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, &base, 1, &folded, 1, nullptr, nullptr, 0);
            map[c] = folded;
            lower[c] = c;
            LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, &c, 1, &lower[c], 1, nullptr, nullptr, 0);
        }
        // Letters NFD keeps whole; folded the way people type them on a keyboard without the key.
        map[0x131] = L'i'; // ı
        map[0x130] = L'i'; // İ
        map[0xDF] = L's';  // ß
        map[0xC6] = map[0xE6] = L'a'; // Æ æ
        map[0xD8] = map[0xF8] = L'o'; // Ø ø
        map[0x110] = map[0x111] = L'd'; // Đ đ
        map[0x141] = map[0x142] = L'l'; // Ł ł
        map[0x126] = map[0x127] = L'h'; // Ħ ħ
        map[0x152] = map[0x153] = L'o'; // Œ œ
    }
};

const FoldTable& Table()
{
    static const FoldTable table;
    return table;
}
} // namespace

wchar_t Fold(wchar_t c)
{
    if (c < 0x80) return (c >= L'A' && c <= L'Z') ? wchar_t(c + 32) : c;
    if (c < FoldTableSize) return Table().map[c];
    // Typographic apostrophes and dashes as their keyboard forms.
    if (c == 0x2018 || c == 0x2019) return L'\'';
    if (c >= 0x2010 && c <= 0x2015) return L'-';
    return c;
}

void FoldInto(std::wstring_view text, std::wstring& out)
{
    out.resize(text.size());
    for (size_t i = 0; i < text.size(); i++) out[i] = Fold(text[i]);
}

std::wstring Folded(std::wstring_view text)
{
    std::wstring out;
    FoldInto(text, out);
    return out;
}

namespace
{
inline wchar_t Lower(wchar_t c)
{
    if (c < 0x80) return (c >= L'A' && c <= L'Z') ? wchar_t(c + 32) : c;
    // Case only, keeping diacritics: paths must not collide on ü/u.
    return c < FoldTableSize ? Table().lower[c] : c;
}
} // namespace

wchar_t LowerChar(wchar_t c) { return Lower(c); }

// FNV-1a over the lower-cased text, continuing from `hash`.
uint64_t HashLower(uint64_t hash, std::wstring_view text)
{
    for (wchar_t c : text)
    {
        hash ^= uint64_t(Lower(c));
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t DirHash(std::wstring_view dir)
{
    uint64_t hash = HashLower(HashSeed, dir);
    return !dir.empty() && dir.back() != L'\\' ? HashLower(hash, L"\\") : hash;
}

uint64_t PathHash(std::wstring_view dir, std::wstring_view name)
{
    return name.empty() ? HashLower(HashSeed, dir) : HashLower(DirHash(dir), name);
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i] && Lower(a[i]) != Lower(b[i])) return false;
    return true;
}

bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix)
{
    return text.size() >= prefix.size() && EqualsNoCase(text.substr(0, prefix.size()), prefix);
}

bool ContainsNoCase(std::wstring_view text, std::wstring_view part)
{
    if (part.empty()) return true;
    if (text.size() < part.size()) return false;
    wchar_t first = Lower(part[0]);
    for (size_t i = 0; i + part.size() <= text.size(); i++)
    {
        if (Lower(text[i]) != first) continue;
        if (EqualsNoCase(text.substr(i, part.size()), part)) return true;
    }
    return false;
}

std::wstring FormatCount(uint64_t value)
{
    std::wstring digits = std::to_wstring(value), out;
    wchar_t separator = Lang::Get().thousands;
    out.reserve(digits.size() + digits.size() / 3);
    for (size_t i = 0; i < digits.size(); i++)
    {
        if (i > 0 && (digits.size() - i) % 3 == 0) out.push_back(separator);
        out.push_back(digits[i]);
    }
    return out;
}

std::wstring KnownFolder(const GUID& id)
{
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &path)) && path) out = path;
    CoTaskMemFree(path);
    while (out.size() > 3 && out.back() == L'\\') out.pop_back();
    return out;
}

std::wstring LocalDataDir()
{
    std::wstring dir = KnownFolder(FOLDERID_LocalAppData) + L"\\DeskFind";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

namespace
{
void AppendLine(const wchar_t* fileName, const std::wstring& line)
{
    std::wstring path = LocalDataDir() + L"\\" + fileName;
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), utf8.data(), bytes, nullptr, nullptr);
    DWORD written;
    WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(file);
}
} // namespace

void LogError(const wchar_t* text)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[64];
    swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    AppendLine(L"error.log", stamp + std::wstring(text) + L"\r\n");
}

double PreciseMs()
{
    static const double scale = []
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1000.0 / double(f.QuadPart);
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart) * scale;
}

#ifdef DF_TRACE
#include <cstdarg>

void Trace(const wchar_t* format, ...)
{
    static double start = PreciseMs();
    wchar_t line[512];
    int n = swprintf_s(line, L"%9.2f  ", PreciseMs() - start);
    va_list args;
    va_start(args, format);
    vswprintf_s(line + n, ARRAYSIZE(line) - n, format, args);
    va_end(args);
    AppendLine(L"trace.log", std::wstring(line) + L"\r\n");
}
#endif
