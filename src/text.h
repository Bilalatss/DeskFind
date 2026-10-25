// Text through DirectWrite, rasterized to grayscale coverage masks on the CPU (no Direct2D, no GPU device).
#pragma once
#include "common.h"

#include <dwrite_2.h>

enum class Font : uint8_t
{
    Ui,
    Icons,
};

// Colors are referenced by role, so rendered masks stay valid when the theme changes.
enum class Ink : uint8_t
{
    Primary,
    Secondary,
    Tertiary,
    Accent,
    White,
    Count,
};

struct GlyphMask
{
    int left = 0, top = 0, width = 0, height = 0; // pixels, relative to the text origin
    Ink ink = Ink::Primary;
    std::vector<uint8_t> alpha;
};

struct TextBlock
{
    std::vector<GlyphMask> masks;
    float width = 0;    // layout width in pixels (trailing spaces included)
    float height = 0;   // line height in pixels
    float baseline = 0; // from the top
};

struct TextStyle
{
    Font font = Font::Ui;
    float size = 14; // pixels
    bool semibold = false;
    Ink ink = Ink::Primary;
};

struct Span
{
    uint32_t start, length; // highlighted: semibold, accent colored
};

class TextEngine
{
public:
    bool Init();
    void Shutdown();

    // A single line; with a max width and `trim` it is cut with an ellipsis. The caller releases it.
    IDWriteTextLayout* CreateLayout(std::wstring_view text, const TextStyle& style, float maxWidth, bool trim) const;
    TextBlock Render(IDWriteTextLayout* layout, Ink ink, const std::vector<Span>* highlights = nullptr) const;
    TextBlock Render(std::wstring_view text, const TextStyle& style, float maxWidth = 0, const std::vector<Span>* highlights = nullptr) const;
    float Measure(std::wstring_view text, const TextStyle& style) const;
    float LineHeight(const TextStyle& style) const;

    float Gamma() const { return gamma_; }
    float GrayscaleContrast() const { return grayscaleContrast_; }
    void ReleaseFormats(); // DPI changes make every cached size useless

private:
    struct FormatEntry
    {
        Font font;
        float size;
        bool semibold;
        IDWriteTextFormat* format;
        IDWriteInlineObject* ellipsis;
    };
    const FormatEntry& Format(const TextStyle& style) const;

    IDWriteFactory2* factory_ = nullptr;
    IDWriteRenderingParams* params_ = nullptr;
    std::wstring uiFamily_, iconFamily_;
    float gamma_ = 1.8f, grayscaleContrast_ = 1.0f;
    mutable std::vector<FormatEntry> formats_;
};
