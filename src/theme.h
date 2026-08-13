// Colour tokens. Two appearances, resolved once and then read by the renderer.
#pragma once
#include <d2d1_1.h>

namespace oriel {

struct Theme {
    D2D1_COLOR_F chrome;      // title bar, path bar
    D2D1_COLOR_F content;     // the file list surface
    D2D1_COLOR_F ink;
    D2D1_COLOR_F ink2;        // secondary text
    D2D1_COLOR_F hair;        // 1px separators
    D2D1_COLOR_F edge;        // window outline
    D2D1_COLOR_F spec;        // 1px specular highlight along a glass edge
    D2D1_COLOR_F hover;
    D2D1_COLOR_F selSoft;     // quiet fill (capsules, unfocused rows)
    D2D1_COLOR_F selIdle;     // selection left behind in an ancestor column
    D2D1_COLOR_F accent;      // follows the system accent
    D2D1_COLOR_F captionX;    // close-button hover
    bool dark;
};

// rgba() with 0-255 channels, because the design tokens are written that way.
constexpr D2D1_COLOR_F rgba(int r, int g, int b, float a = 1.0f) {
    return D2D1_COLOR_F{ r / 255.0f, g / 255.0f, b / 255.0f, a };
}

// Ink that has to travel between two tokens while a fill fades in under it.
// Straight lerp, not premultiplied: both ends are opaque wherever this is used.
constexpr D2D1_COLOR_F lerpColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return D2D1_COLOR_F{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                         a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}

inline Theme lightTheme(D2D1_COLOR_F accent) {
    return Theme{
        /*chrome */ rgba(0xF7, 0xF7, 0xF8),
        /*content*/ rgba(0xFF, 0xFF, 0xFF),
        /*ink    */ rgba(0x1C, 0x1C, 0x1E),
        /*ink2   */ rgba(0x8A, 0x8A, 0x8E),
        /*hair   */ rgba(0, 0, 0, 0.075f),
        /*edge   */ rgba(0, 0, 0, 0.16f),
        /*spec   */ rgba(255, 255, 255, 0.72f),
        /*hover  */ rgba(0, 0, 0, 0.045f),
        /*selSoft*/ rgba(0, 0, 0, 0.075f),
        /*selIdle*/ rgba(0, 0, 0, 0.115f),
        /*accent */ accent,
        /*capX   */ rgba(0xC4, 0x2B, 0x1C),
        /*dark   */ false,
    };
}

inline Theme darkTheme(D2D1_COLOR_F accent) {
    return Theme{
        rgba(0x1C, 0x1C, 0x1F),
        rgba(0x15, 0x15, 0x17),
        rgba(0xF0, 0xF0, 0xF2),
        rgba(0x8E, 0x8E, 0x93),
        rgba(255, 255, 255, 0.085f),
        rgba(255, 255, 255, 0.13f),
        rgba(255, 255, 255, 0.16f),
        rgba(255, 255, 255, 0.055f),
        rgba(255, 255, 255, 0.09f),
        rgba(255, 255, 255, 0.145f),
        accent,
        rgba(0xC4, 0x2B, 0x1C),
        true,
    };
}

} // namespace oriel
