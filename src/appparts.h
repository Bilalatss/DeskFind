// What app.cpp, appview.cpp and appinput.cpp share about the search window: its timers and its measurements.
#pragma once
#include "common.h"

enum : UINT_PTR
{
    TimerCaret = 1,
    TimerAnim,
    TimerTrim,
    TimerInterim,
    TimerEverything,
    TimerDefer,
};

// Sizes in device-independent pixels.
constexpr float SearchHeight = 60, ListMax = 456, ListPad = 6, FooterHeight = 36;
constexpr float RowPitch = 50, RowRadius = 6, HeaderTop = 10, HeaderBottom = 4;
constexpr float IconSize = 24, IconLeft = 14, TextLeft = 50, RowRight = 12, TextGap = 10;
constexpr float Track = 10, ThumbWidth = 4, ThumbMargin = 6, ThumbMin = 24;
constexpr float GlyphLeft = 20, GlyphGap = 14;
constexpr float BoxFont = 18, NameFont = 14, SmallFont = 12, KeyFont = 11.5f;
constexpr double FadeMs = 120, SlideMs = 180, SlideDistance = 6;
constexpr UINT InterimMs = 150, TrimDelayMs = 4000, DeferMs = 250; // a second letter usually comes within 250 ms
constexpr size_t MaxIcons = 128, MaxQueryLength = 400;
