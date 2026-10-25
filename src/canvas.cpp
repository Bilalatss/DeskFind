#include "canvas.h"

#include <cmath>

namespace
{
// src-over for premultiplied BGRA, two channels per multiply.
inline uint32_t Over(uint32_t d, uint32_t s)
{
    uint32_t ia = 255 - (s >> 24);
    if (ia == 0) return s;
    uint32_t rb = (d & 0x00FF00FF) * ia;
    uint32_t ag = ((d >> 8) & 0x00FF00FF) * ia;
    rb = ((rb + 0x00800080 + ((rb >> 8) & 0x00FF00FF)) >> 8) & 0x00FF00FF;
    ag = (ag + 0x00800080 + ((ag >> 8) & 0x00FF00FF)) & 0xFF00FF00;
    return s + rb + ag;
}

// Scales a premultiplied color by c in 0..256.
inline uint32_t Scale(uint32_t s, uint32_t c)
{
    uint32_t rb = (((s & 0x00FF00FF) * c) >> 8) & 0x00FF00FF;
    uint32_t ag = (((s >> 8) & 0x00FF00FF) * c) & 0xFF00FF00;
    return rb | ag;
}

inline float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// Signed distance from a pixel center to a rounded rectangle, negative inside.
inline float RoundRectDistance(float cx, float cy, float left, float top, float right, float bottom, float radius)
{
    float qx = std::max(left + radius - cx, cx - (right - radius));
    float qy = std::max(top + radius - cy, cy - (bottom - radius));
    if (qx > 0 && qy > 0) return std::sqrt(qx * qx + qy * qy) - radius;
    return std::max(qx, qy) - radius;
}

// DirectWrite's grayscale blend (enhanced contrast, then alpha correction for the display gamma),
// as Windows Terminal ports it from Direct2D.
void GammaRatios(float gamma, float out[4])
{
    static constexpr float ratios[13][4] = {
        {0.0000f, 0.0000f, 0.0000f, 0.0000f},  {0.0166f, -0.0807f, 0.2227f, -0.0751f}, {0.0350f, -0.1760f, 0.4325f, -0.1370f},
        {0.0543f, -0.2821f, 0.6302f, -0.1876f}, {0.0739f, -0.3963f, 0.8167f, -0.2287f}, {0.0933f, -0.5161f, 0.9926f, -0.2616f},
        {0.1121f, -0.6395f, 1.1588f, -0.2877f}, {0.1300f, -0.7649f, 1.3159f, -0.3080f}, {0.1469f, -0.8911f, 1.4644f, -0.3234f},
        {0.1627f, -1.0170f, 1.6051f, -0.3347f}, {0.1773f, -1.1420f, 1.7385f, -0.3426f}, {0.1908f, -1.2652f, 1.8650f, -0.3476f},
        {0.2031f, -1.3864f, 1.9851f, -0.3501f},
    };
    constexpr float norm13 = float(double(0x10000) / (255 * 255) * 4), norm24 = float(double(0x100) / 255 * 4);
    gamma = std::clamp(gamma, 1.0f, 2.2f);
    const float* r = ratios[size_t(std::lround((gamma - 1.0f) * 10.0f))];
    out[0] = norm13 * r[0] / 4;
    out[1] = norm24 * r[1] / 4;
    out[2] = norm13 * r[2] / 4;
    out[3] = norm24 * r[3] / 4;
}
} // namespace

uint32_t Premultiply(Argb color, float opacity)
{
    uint32_t a = uint32_t(std::lround(float(color >> 24) * Clamp01(opacity)));
    uint32_t r = (((color >> 16) & 0xFF) * a + 127) / 255;
    uint32_t g = (((color >> 8) & 0xFF) * a + 127) / 255;
    uint32_t b = ((color & 0xFF) * a + 127) / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void Palette::Build(float gamma, float grayscaleContrast)
{
    float g[4];
    GammaRatios(gamma, g);
    for (size_t i = 0; i < size_t(Ink::Count); i++)
    {
        float r = float((ink[i] >> 16) & 0xFF) / 255, gr = float((ink[i] >> 8) & 0xFF) / 255, b = float(ink[i] & 0xFF) / 255;
        float luma = r * 0.30f + gr * 0.59f + b * 0.11f;
        float k = grayscaleContrast * Clamp01(luma * -4.0f + 3.0f); // light text on dark gets no extra weight
        float f = r * 0.25f + gr * 0.5f + b * 0.25f;
        for (int c = 0; c < 256; c++)
        {
            float a = float(c) / 255;
            a = a * (k + 1) / (a * k + 1);
            a = a + a * (1 - a) * ((g[0] * f + g[1]) * a + (g[2] * f + g[3]));
            lut[i][c] = uint8_t(std::lround(Clamp01(a) * 255));
        }
    }
}

bool Canvas::Resize(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (pixels_ && width == width_ && height == height_) return true;
    Release();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap_) return false;
    dc_ = CreateCompatibleDC(nullptr);
    old_ = SelectObject(dc_, bitmap_);
    pixels_ = static_cast<uint32_t*>(bits);
    width_ = width;
    height_ = height;
    ResetClip();
    return true;
}

void Canvas::Release()
{
    if (dc_)
    {
        SelectObject(dc_, old_);
        DeleteDC(dc_);
    }
    if (bitmap_) DeleteObject(bitmap_);
    dc_ = nullptr;
    bitmap_ = nullptr;
    pixels_ = nullptr;
    width_ = height_ = 0;
}

void Canvas::SetClip(const RECT& clip)
{
    clip_.left = std::max<LONG>(0, clip.left);
    clip_.top = std::max<LONG>(0, clip.top);
    clip_.right = std::min<LONG>(width_, clip.right);
    clip_.bottom = std::min<LONG>(height_, clip.bottom);
}

void Canvas::FillBackground(const RECT& area, Argb color)
{
    uint32_t p = Premultiply(color);
    int left = std::max<int>(area.left, clip_.left), right = std::min<int>(area.right, clip_.right);
    int top = std::max<int>(area.top, clip_.top), bottom = std::min<int>(area.bottom, clip_.bottom);
    for (int y = top; y < bottom; y++) std::fill(pixels_ + size_t(y) * width_ + left, pixels_ + size_t(y) * width_ + right, p);
}

inline void Canvas::Coverage(int x, int y, uint32_t premultiplied, int coverage)
{
    if (coverage <= 0) return;
    uint32_t& d = pixels_[size_t(y) * width_ + x];
    d = Over(d, coverage >= 256 ? premultiplied : Scale(premultiplied, uint32_t(coverage)));
}

void Canvas::FillRect(int left, int top, int right, int bottom, Argb color)
{
    top += offsetY;
    bottom += offsetY;
    uint32_t p = Premultiply(color, opacity);
    if (!(p >> 24)) return;
    left = std::max<int>(left, clip_.left);
    right = std::min<int>(right, clip_.right);
    top = std::max<int>(top, clip_.top);
    bottom = std::min<int>(bottom, clip_.bottom);
    for (int y = top; y < bottom; y++)
    {
        uint32_t* row = pixels_ + size_t(y) * width_;
        for (int x = left; x < right; x++) row[x] = Over(row[x], p);
    }
}

void Canvas::FillRoundRect(float left, float top, float right, float bottom, float radius, Argb color)
{
    top += float(offsetY);
    bottom += float(offsetY);
    uint32_t p = Premultiply(color, opacity);
    if (!(p >> 24) || right <= left || bottom <= top) return;
    radius = std::min({radius, (right - left) / 2, (bottom - top) / 2});
    int x0 = std::max<int>(int(std::floor(left)), clip_.left), x1 = std::min<int>(int(std::ceil(right)), clip_.right);
    int y0 = std::max<int>(int(std::floor(top)), clip_.top), y1 = std::min<int>(int(std::ceil(bottom)), clip_.bottom);
    for (int y = y0; y < y1; y++)
    {
        float cy = float(y) + 0.5f;
        for (int x = x0; x < x1; x++)
        {
            float d = RoundRectDistance(float(x) + 0.5f, cy, left, top, right, bottom, radius);
            Coverage(x, y, p, int(Clamp01(0.5f - d) * 256.0f + 0.5f));
        }
    }
}

void Canvas::StrokeRoundRect(float left, float top, float right, float bottom, float radius, float width, Argb color)
{
    top += float(offsetY);
    bottom += float(offsetY);
    uint32_t p = Premultiply(color, opacity);
    if (!(p >> 24) || right <= left || bottom <= top) return;
    radius = std::min({radius, (right - left) / 2, (bottom - top) / 2});
    float innerRadius = std::max(0.0f, radius - width);
    int x0 = std::max<int>(int(std::floor(left)), clip_.left), x1 = std::min<int>(int(std::ceil(right)), clip_.right);
    int y0 = std::max<int>(int(std::floor(top)), clip_.top), y1 = std::min<int>(int(std::ceil(bottom)), clip_.bottom);
    for (int y = y0; y < y1; y++)
    {
        float cy = float(y) + 0.5f;
        for (int x = x0; x < x1; x++)
        {
            float cx = float(x) + 0.5f;
            float outer = Clamp01(0.5f - RoundRectDistance(cx, cy, left, top, right, bottom, radius));
            float inner = Clamp01(0.5f - RoundRectDistance(cx, cy, left + width, top + width, right - width, bottom - width, innerRadius));
            Coverage(x, y, p, int((outer - inner) * 256.0f + 0.5f));
        }
    }
}

void Canvas::DrawText(const TextBlock& text, int x, int y, const Palette& palette, Argb override)
{
    y += offsetY;
    for (const GlyphMask& mask : text.masks)
    {
        Argb color = override ? override : palette.ink[size_t(mask.ink)];
        uint32_t p = Premultiply(color, opacity);
        if (!(p >> 24)) continue;
        const uint8_t* lut = palette.lut[size_t(mask.ink)];
        int left = x + mask.left, top = y + mask.top;
        int sx0 = std::max(0, int(clip_.left) - left), sy0 = std::max(0, int(clip_.top) - top);
        int sx1 = std::min(mask.width, int(clip_.right) - left), sy1 = std::min(mask.height, int(clip_.bottom) - top);
        for (int sy = sy0; sy < sy1; sy++)
        {
            const uint8_t* src = mask.alpha.data() + size_t(sy) * mask.width;
            uint32_t* row = pixels_ + size_t(top + sy) * width_ + left;
            for (int sx = sx0; sx < sx1; sx++)
            {
                uint32_t a = src[sx];
                if (!a) continue;
                a = lut[a];
                row[sx] = Over(row[sx], Scale(p, a + (a >> 7)));
            }
        }
    }
}

void Canvas::DrawImage(const uint32_t* image, int width, int height, int x, int y)
{
    y += offsetY;
    uint32_t fade = uint32_t(std::lround(Clamp01(opacity) * 256.0f));
    int sx0 = std::max(0, int(clip_.left) - x), sy0 = std::max(0, int(clip_.top) - y);
    int sx1 = std::min(width, int(clip_.right) - x), sy1 = std::min(height, int(clip_.bottom) - y);
    for (int sy = sy0; sy < sy1; sy++)
    {
        const uint32_t* src = image + size_t(sy) * width;
        uint32_t* row = pixels_ + size_t(y + sy) * width_ + x;
        for (int sx = sx0; sx < sx1; sx++)
        {
            uint32_t s = src[sx];
            if (!(s >> 24)) continue;
            row[sx] = Over(row[sx], fade >= 256 ? s : Scale(s, fade));
        }
    }
}

void Canvas::FadeToBackground(int left, int top, int right, int bottom, Argb background, bool toLeft)
{
    top += offsetY;
    bottom += offsetY;
    uint32_t bg = Premultiply(background);
    int width = right - left;
    if (width <= 0) return;
    int x0 = std::max<int>(left, clip_.left), x1 = std::min<int>(right, clip_.right);
    int y0 = std::max<int>(top, clip_.top), y1 = std::min<int>(bottom, clip_.bottom);
    for (int x = x0; x < x1; x++)
    {
        // Weight of what is drawn: 0 at the faded edge, 256 at the other end.
        float t = (float(x - left) + 0.5f) / float(width);
        uint32_t keep = uint32_t(std::lround((toLeft ? t : 1 - t) * 256.0f));
        for (int y = y0; y < y1; y++)
        {
            uint32_t& d = pixels_[size_t(y) * width_ + x];
            d = Scale(d, keep) + Scale(bg, 256 - keep);
        }
    }
}
