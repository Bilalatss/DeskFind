#include "text.h"

#include <cmath>

#include "lang.h"

namespace
{
// Drawing effect that only carries an Ink. Static, never freed, so reference counting is a no-op.
class InkEffect final : public IUnknown
{
public:
    explicit InkEffect(Ink value) : ink(value) {}
    const Ink ink;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == __uuidof(IUnknown))
        {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
};

InkEffect g_inks[] = {InkEffect(Ink::Primary), InkEffect(Ink::Secondary), InkEffect(Ink::Tertiary), InkEffect(Ink::Accent), InkEffect(Ink::White)};

Ink InkOf(IUnknown* effect, Ink fallback)
{
    for (InkEffect& e : g_inks)
        if (effect == &e) return e.ink;
    return fallback;
}

// Collects every glyph run of a layout as a grayscale coverage mask. Lives on the stack for one Draw call.
class MaskRenderer final : public IDWriteTextRenderer
{
public:
    MaskRenderer(IDWriteFactory2* factory, TextBlock& out, Ink ink) : factory_(factory), out_(out), ink_(ink) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) || riid == __uuidof(IDWriteTextRenderer))
        {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override
    {
        *disabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* m) override
    {
        *m = DWRITE_MATRIX{1, 0, 0, 1, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* ppd) override
    {
        *ppd = 1; // layouts are built in pixels
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mode, const DWRITE_GLYPH_RUN* run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown* effect) override
    {
        // Whole-pixel baselines keep horizontal strokes crisp.
        y = std::round(y);
        ComPtr<IDWriteGlyphRunAnalysis> analysis;
        HRESULT hr = factory_->CreateGlyphRunAnalysis(run, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, mode, DWRITE_GRID_FIT_MODE_DEFAULT,
                                                      DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, x, y, &analysis);
        if (FAILED(hr)) return S_OK;
        RECT bounds{};
        if (FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds))) return S_OK;
        int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0) return S_OK;
        GlyphMask mask;
        mask.left = bounds.left;
        mask.top = bounds.top;
        mask.width = width;
        mask.height = height;
        mask.ink = InkOf(effect, ink_);
        mask.alpha.resize(size_t(width) * height);
        if (FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds, mask.alpha.data(), UINT32(mask.alpha.size())))) return S_OK;
        out_.masks.push_back(std::move(mask));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void* context, FLOAT x, FLOAT y, IDWriteInlineObject* object, BOOL sideways, BOOL rtl,
                                               IUnknown* effect) override
    {
        // The ellipsis of trimmed text draws itself back through DrawGlyphRun.
        return object->Draw(context, this, x, y, sideways, rtl, effect);
    }

private:
    IDWriteFactory2* factory_;
    TextBlock& out_;
    Ink ink_;
};
} // namespace

bool TextEngine::Init()
{
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), reinterpret_cast<IUnknown**>(&factory_)))) return false;
    if (SUCCEEDED(factory_->CreateRenderingParams(&params_)))
    {
        gamma_ = params_->GetGamma();
        ComPtr<IDWriteRenderingParams1> params1;
        if (SUCCEEDED(params_->QueryInterface(__uuidof(IDWriteRenderingParams1), reinterpret_cast<void**>(&params1))))
            grayscaleContrast_ = params1->GetGrayscaleEnhancedContrast();
    }

    ComPtr<IDWriteFontCollection> fonts;
    factory_->GetSystemFontCollection(&fonts);
    auto has = [&](const wchar_t* family)
    {
        UINT32 index;
        BOOL exists = FALSE;
        return fonts && SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists;
    };
    uiFamily_ = has(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    iconFamily_ = has(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    return true;
}

void TextEngine::ReleaseFormats()
{
    for (FormatEntry& entry : formats_)
    {
        if (entry.ellipsis) entry.ellipsis->Release();
        entry.format->Release();
    }
    formats_.clear();
}

void TextEngine::Shutdown()
{
    ReleaseFormats();
    if (params_) params_->Release();
    if (factory_) factory_->Release();
    params_ = nullptr;
    factory_ = nullptr;
}

const TextEngine::FormatEntry& TextEngine::Format(const TextStyle& style) const
{
    for (const FormatEntry& entry : formats_)
        if (entry.font == style.font && entry.size == style.size && entry.semibold == style.semibold) return entry;

    FormatEntry entry{style.font, style.size, style.semibold, nullptr, nullptr};
    const std::wstring& family = style.font == Font::Icons ? iconFamily_ : uiFamily_;
    factory_->CreateTextFormat(family.c_str(), nullptr, style.semibold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL, style.size, Lang::Get().locale, &entry.format);
    entry.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    factory_->CreateEllipsisTrimmingSign(entry.format, &entry.ellipsis);
    formats_.push_back(entry);
    return formats_.back();
}

IDWriteTextLayout* TextEngine::CreateLayout(std::wstring_view text, const TextStyle& style, float maxWidth, bool trim) const
{
    const FormatEntry& format = Format(style);
    IDWriteTextLayout* layout = nullptr;
    float width = maxWidth > 0 ? maxWidth : 100000.0f;
    if (FAILED(factory_->CreateTextLayout(text.data(), UINT32(text.size()), format.format, width, style.size * 2, &layout))) return nullptr;
    DWRITE_TRIMMING trimming{trim ? DWRITE_TRIMMING_GRANULARITY_CHARACTER : DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
    layout->SetTrimming(&trimming, trim ? format.ellipsis : nullptr);
    return layout;
}

TextBlock TextEngine::Render(IDWriteTextLayout* layout, Ink ink, const std::vector<Span>* highlights) const
{
    TextBlock block;
    if (!layout) return block;
    if (highlights)
    {
        for (const Span& span : *highlights)
        {
            DWRITE_TEXT_RANGE range{span.start, span.length};
            layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
            layout->SetDrawingEffect(&g_inks[int(Ink::Accent)], range);
        }
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    DWRITE_LINE_METRICS line{};
    UINT32 lines = 0;
    layout->GetLineMetrics(&line, 1, &lines);
    block.width = metrics.width;
    block.height = metrics.height;
    block.baseline = line.baseline;
    MaskRenderer renderer(factory_, block, ink);
    layout->Draw(nullptr, &renderer, 0, 0);
    return block;
}

TextBlock TextEngine::Render(std::wstring_view text, const TextStyle& style, float maxWidth, const std::vector<Span>* highlights) const
{
    IDWriteTextLayout* layout = CreateLayout(text, style, maxWidth, maxWidth > 0);
    TextBlock block = Render(layout, style.ink, highlights);
    if (layout) layout->Release();
    return block;
}

float TextEngine::Measure(std::wstring_view text, const TextStyle& style) const
{
    IDWriteTextLayout* layout = CreateLayout(text, style, 0, false);
    if (!layout) return 0;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    layout->Release();
    return metrics.widthIncludingTrailingWhitespace;
}

float TextEngine::LineHeight(const TextStyle& style) const
{
    IDWriteTextLayout* layout = CreateLayout(L"Ag", style, 0, false);
    if (!layout) return style.size * 1.33f;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    layout->Release();
    return metrics.height;
}
