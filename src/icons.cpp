#include "icons.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <commoncontrols.h>

#include <cmath>

namespace
{
constexpr size_t MaxQueued = 96;

std::wstring_view Extension(std::wstring_view path)
{
    size_t dot = path.rfind(L'.'), slash = path.rfind(L'\\');
    if (dot == std::wstring_view::npos || (slash != std::wstring_view::npos && dot < slash)) return {};
    return path.substr(dot);
}

bool IsAny(std::wstring_view ext, std::initializer_list<const wchar_t*> list)
{
    for (const wchar_t* item : list)
        if (EqualsNoCase(ext, item)) return true;
    return false;
}

// Icons read from the file itself (exact size, no shell image list growth).
bool IsResourceIcon(std::wstring_view ext) { return IsAny(ext, {L".exe", L".ico", L".cpl", L".scr", L".dll"}); }
bool IsPerFile(std::wstring_view ext) { return IsResourceIcon(ext) || IsAny(ext, {L".lnk", L".url", L".appref-ms", L".msc", L".cur", L".ani"}); }

// Premultiplied pixels of an icon at its own size.
bool IconPixels(HICON icon, std::vector<uint32_t>& out, int& width, int& height)
{
    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) return false;
    bool ok = false;
    BITMAP bm{};
    HBITMAP color = info.hbmColor;
    if (color && GetObjectW(color, sizeof(bm), &bm))
    {
        width = bm.bmWidth;
        height = bm.bmHeight;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        out.assign(size_t(width) * height, 0);
        HDC dc = GetDC(nullptr);
        if (GetDIBits(dc, color, 0, height, out.data(), &bi, DIB_RGB_COLORS))
        {
            bool hasAlpha = false;
            for (uint32_t p : out)
                if (p >> 24)
                {
                    hasAlpha = true;
                    break;
                }
            if (!hasAlpha && info.hbmMask)
            {
                // Old icons: opacity lives in the AND mask (black = opaque).
                std::vector<uint32_t> mask(out.size());
                if (GetDIBits(dc, info.hbmMask, 0, height, mask.data(), &bi, DIB_RGB_COLORS))
                    for (size_t i = 0; i < out.size(); i++) out[i] = (mask[i] & 0xFFFFFF) ? 0 : (out[i] | 0xFF000000);
            }
            for (uint32_t& p : out)
            {
                uint32_t a = p >> 24;
                if (a == 255) continue;
                uint32_t r = (((p >> 16) & 0xFF) * a + 127) / 255, g = (((p >> 8) & 0xFF) * a + 127) / 255, b = ((p & 0xFF) * a + 127) / 255;
                p = (a << 24) | (r << 16) | (g << 8) | b;
            }
            ok = true;
        }
        ReleaseDC(nullptr, dc);
    }
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return ok;
}

// Area-average resampling of premultiplied pixels (bilinear-ish when enlarging).
std::vector<uint32_t> Resample(const std::vector<uint32_t>& src, int sw, int sh, int size)
{
    if (sw == size && sh == size) return src;
    std::vector<uint32_t> dst(size_t(size) * size);
    float sx = float(sw) / size, sy = float(sh) / size;
    for (int y = 0; y < size; y++)
    {
        float y0 = y * sy, y1 = std::max(y0 + 1e-3f, (y + 1) * sy);
        if (sy < 1) y1 = y0 + 1; // enlarging: sample one source pixel area
        for (int x = 0; x < size; x++)
        {
            float x0 = x * sx, x1 = std::max(x0 + 1e-3f, (x + 1) * sx);
            if (sx < 1) x1 = x0 + 1;
            float acc[4] = {}, total = 0;
            for (int py = int(y0); py < int(std::ceil(y1)) && py < sh; py++)
            {
                float wy = std::min(y1, float(py + 1)) - std::max(y0, float(py));
                if (wy <= 0) continue;
                for (int px = int(x0); px < int(std::ceil(x1)) && px < sw; px++)
                {
                    float wx = std::min(x1, float(px + 1)) - std::max(x0, float(px));
                    if (wx <= 0) continue;
                    float w = wx * wy;
                    uint32_t p = src[size_t(py) * sw + px];
                    acc[0] += float(p & 0xFF) * w;
                    acc[1] += float((p >> 8) & 0xFF) * w;
                    acc[2] += float((p >> 16) & 0xFF) * w;
                    acc[3] += float(p >> 24) * w;
                    total += w;
                }
            }
            if (total <= 0) continue;
            uint32_t c[4];
            for (int i = 0; i < 4; i++) c[i] = uint32_t(std::min(255.0f, acc[i] / total + 0.5f));
            dst[size_t(y) * size + x] = c[0] | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
        }
    }
    return dst;
}

class Extractor
{
public:
    std::vector<uint32_t> Load(const std::wstring& path, bool folder, int size)
    {
        size_ = size;
        if (folder) return FromImageList(SystemIndex(L"folder", FILE_ATTRIBUTE_DIRECTORY, true));
        std::wstring_view ext = Extension(path);
        if (IsResourceIcon(ext))
        {
            auto pixels = FromResource(path, 0);
            if (!pixels.empty()) return pixels;
        }
        else if (EqualsNoCase(ext, L".lnk"))
        {
            auto pixels = FromShortcut(path);
            if (!pixels.empty()) return pixels;
            return FromImageList(SystemIndex(path.c_str(), 0, false));
        }
        else if (EqualsNoCase(ext, L".url"))
        {
            wchar_t file[MAX_PATH] = {};
            GetPrivateProfileStringW(L"InternetShortcut", L"IconFile", L"", file, MAX_PATH, path.c_str());
            int index = int(GetPrivateProfileIntW(L"InternetShortcut", L"IconIndex", 0, path.c_str()));
            if (file[0])
            {
                auto pixels = FromResource(file, index);
                if (!pixels.empty()) return pixels;
            }
        }
        else if (IsPerFile(ext))
        {
            return FromImageList(SystemIndex(path.c_str(), 0, false));
        }
        return Generic(ext);
    }

private:
    std::vector<uint32_t> Generic(std::wstring_view ext)
    {
        std::wstring fake = L"x" + std::wstring(ext);
        return FromImageList(SystemIndex(fake.c_str(), FILE_ATTRIBUTE_NORMAL, true));
    }

    static int SystemIndex(const wchar_t* path, DWORD attributes, bool generic)
    {
        SHFILEINFOW info{};
        UINT flags = SHGFI_SYSICONINDEX | (generic ? SHGFI_USEFILEATTRIBUTES : 0);
        if (!SHGetFileInfoW(path, attributes, &info, sizeof(info), flags)) return -1;
        return info.iIcon;
    }

    std::vector<uint32_t> Finish(HICON icon)
    {
        std::vector<uint32_t> pixels;
        int w = 0, h = 0;
        if (icon && IconPixels(icon, pixels, w, h) && w > 0 && h > 0) pixels = Resample(pixels, w, h, size_);
        else pixels.clear();
        if (icon) DestroyIcon(icon);
        return pixels;
    }

    std::vector<uint32_t> FromImageList(int index)
    {
        if (index < 0) return {};
        // The smallest system list at least as big as the target, so it only ever shrinks.
        static const int lists[] = {SHIL_SMALL, SHIL_LARGE, SHIL_EXTRALARGE};
        IImageList* chosen = nullptr;
        for (int list : lists)
        {
            IImageList* images = nullptr;
            if (FAILED(SHGetImageList(list, IID_IImageList, reinterpret_cast<void**>(&images)))) continue;
            int cx = 0, cy = 0;
            images->GetIconSize(&cx, &cy);
            if (chosen) chosen->Release();
            chosen = images;
            if (cx >= size_) break;
        }
        if (!chosen) return {};
        HICON icon = nullptr;
        chosen->GetIcon(index, ILD_TRANSPARENT, &icon);
        chosen->Release();
        return Finish(icon);
    }

    std::vector<uint32_t> FromResource(const std::wstring& file, int index)
    {
        wchar_t expanded[MAX_PATH * 2];
        if (!ExpandEnvironmentStringsW(file.c_str(), expanded, ARRAYSIZE(expanded))) return {};
        // Ask for a size at least as big as needed; the icon resource picks its best image.
        int want = size_ <= 16 ? 16 : size_ <= 24 ? 24 : size_ <= 32 ? 32 : size_ <= 48 ? 48 : size_ <= 64 ? 64 : 256;
        HICON icon = nullptr;
        UINT id = 0;
        if (PrivateExtractIconsW(expanded, index, want, want, &icon, &id, 1, LR_DEFAULTCOLOR) != 1 || !icon) return {};
        return Finish(icon);
    }

    std::vector<uint32_t> FromShortcut(const std::wstring& path)
    {
        ComPtr<IShellLinkW> link;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return {};
        ComPtr<IPersistFile> file;
        if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file))) || FAILED(file->Load(path.c_str(), STGM_READ))) return {};
        wchar_t location[MAX_PATH * 2] = {};
        int index = 0;
        if (SUCCEEDED(link->GetIconLocation(location, ARRAYSIZE(location), &index)) && location[0])
        {
            std::wstring_view ext = Extension(location);
            if (IsResourceIcon(ext) || EqualsNoCase(ext, L".ico") || EqualsNoCase(ext, L".icl")) return FromResource(location, index);
        }
        wchar_t target[MAX_PATH * 2] = {};
        WIN32_FIND_DATAW data{};
        if (FAILED(link->GetPath(target, ARRAYSIZE(target), &data, SLGP_RAWPATH)) || !target[0]) return {};
        wchar_t expanded[MAX_PATH * 2];
        ExpandEnvironmentStringsW(target, expanded, ARRAYSIZE(expanded));
        DWORD attributes = GetFileAttributesW(expanded);
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            return FromImageList(SystemIndex(L"folder", FILE_ATTRIBUTE_DIRECTORY, true));
        std::wstring_view ext = Extension(expanded);
        if (IsResourceIcon(ext))
        {
            auto pixels = FromResource(expanded, 0);
            if (!pixels.empty()) return pixels;
        }
        if (!ext.empty() && !IsPerFile(ext)) return Generic(ext);
        return {};
    }

    int size_ = 24;
};
} // namespace

std::vector<uint32_t> IconLoader::AppIcon(int size)
{
    HICON icon = nullptr;
    std::vector<uint32_t> pixels;
    int w = 0, h = 0;
    if (FAILED(LoadIconWithScaleDown(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), size, size, &icon)) || !icon) return pixels;
    if (IconPixels(icon, pixels, w, h) && w > 0 && h > 0) pixels = Resample(pixels, w, h, size);
    else pixels.clear();
    DestroyIcon(icon);
    return pixels;
}

std::wstring IconLoader::KeyFor(std::wstring_view path, bool folder)
{
    if (folder) return L"<folder>";
    std::wstring_view ext = Extension(path);
    if (IsPerFile(ext)) return std::wstring(path);
    std::wstring key = L"*";
    for (wchar_t c : ext) key.push_back(Fold(c));
    return key;
}

void IconLoader::Start(HWND notify)
{
    notify_ = notify;
}

void IconLoader::Request(const std::wstring& key, const std::wstring& path, bool folder, int size)
{
    AcquireSRWLockExclusive(&lock_);
    if (!thread_ && !stop_) thread_ = CreateThread(nullptr, 256 * 1024, ThreadMain, this, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    bool queued = false;
    for (const Job& job : jobs_)
        if (job.key == key && job.size == size) queued = true;
    IconResult* dropped = nullptr;
    if (!queued)
    {
        jobs_.push_back(Job{key, path, folder, size});
        if (jobs_.size() > MaxQueued)
        {
            // The oldest request scrolled away long ago; tell the UI so it asks again if that row comes back.
            dropped = new IconResult{std::move(jobs_.front().key), jobs_.front().size, {}, true};
            jobs_.erase(jobs_.begin());
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    WakeConditionVariable(&wake_);
    if (dropped && !PostMessageW(notify_, WM_APP_ICON, 0, reinterpret_cast<LPARAM>(dropped))) delete dropped;
}

void IconLoader::Cancel()
{
    AcquireSRWLockExclusive(&lock_);
    jobs_.clear();
    jobs_.shrink_to_fit();
    ReleaseSRWLockExclusive(&lock_);
}

void IconLoader::Stop()
{
    AcquireSRWLockExclusive(&lock_);
    stop_ = true;
    jobs_.clear();
    HANDLE thread = thread_;
    ReleaseSRWLockExclusive(&lock_);
    WakeAllConditionVariable(&wake_);
    if (thread)
    {
        WaitForSingleObject(thread, 2000);
        CloseHandle(thread);
    }
    thread_ = nullptr;
}

bool IconLoader::Next(Job& job)
{
    AcquireSRWLockExclusive(&lock_);
    while (jobs_.empty() && !stop_) SleepConditionVariableSRW(&wake_, &lock_, INFINITE, 0);
    bool ok = !stop_;
    if (ok)
    {
        job = std::move(jobs_.back());
        jobs_.pop_back();
    }
    ReleaseSRWLockExclusive(&lock_);
    return ok;
}

DWORD WINAPI IconLoader::ThreadMain(void* self)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    static_cast<IconLoader*>(self)->Work();
    CoUninitialize();
    return 0;
}

void IconLoader::Work()
{
    Extractor extractor;
    Job job;
    while (Next(job))
    {
        auto* result = new IconResult;
        result->key = job.key;
        result->size = job.size;
        result->pixels = extractor.Load(job.path, job.folder, job.size);
        if (!PostMessageW(notify_, WM_APP_ICON, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }
}
