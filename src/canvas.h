// A small software 2D canvas: premultiplied BGRA in a DIB section, so the alpha reaches DWM's backdrop.
#pragma once
#include "common.h"
#include "text.h"

// Straight (not premultiplied) 0xAARRGGBB, as theme colors are written.
using Argb = uint32_t;

struct Palette
{
    Argb ink[size_t(Ink::Count)] = {};
    uint8_t lut[size_t(Ink::Count)][256] = {}; // coverage -> alpha after DirectWrite's contrast and gamma correction
    void Build(float gamma, float grayscaleContrast);
};

class Canvas
{
public:
    ~Canvas() { Release(); }
    bool Resize(int width, int height);
    void Release();
    bool Valid() const { return pixels_ != nullptr; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    HDC Dc() const { return dc_; }
    uint32_t* Pixels() const { return pixels_; }

    void SetClip(const RECT& clip);
    void ResetClip() { clip_ = RECT{0, 0, width_, height_}; }
    const RECT& Clip() const { return clip_; }

    // Content opacity and vertical offset, for the open animation. The background ignores both.
    float opacity = 1;
    int offsetY = 0;

    void FillBackground(const RECT& area, Argb color); // replaces pixels, no blending
    void FillRect(int left, int top, int right, int bottom, Argb color);
    void FillRoundRect(float left, float top, float right, float bottom, float radius, Argb color);
    void StrokeRoundRect(float left, float top, float right, float bottom, float radius, float width, Argb color);
    void DrawText(const TextBlock& text, int x, int y, const Palette& palette, Argb override = 0);
    void DrawImage(const uint32_t* premultiplied, int width, int height, int x, int y);
    // Blends what is drawn toward the background along a horizontal ramp (fully background at the `toLeft` side).
    void FadeToBackground(int left, int top, int right, int bottom, Argb background, bool toLeft);

private:
    void Coverage(int x, int y, uint32_t premultiplied, int coverage); // coverage 0..256

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    uint32_t* pixels_ = nullptr;
    int width_ = 0, height_ = 0;
    RECT clip_{};
};

uint32_t Premultiply(Argb color, float opacity = 1);
