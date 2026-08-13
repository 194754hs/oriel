#include "svg_path.h"
#include <d2d1helper.h>
#include <cstdlib>
#include <cctype>

namespace oriel {
namespace {

struct Cursor {
    const char* s;

    void skip() {
        while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n' || *s == '\r') ++s;
    }
    float num() {
        skip();
        char* end = nullptr;
        const float v = std::strtof(s, &end);
        if (end == s) { if (*s) ++s; return 0.0f; }   // never spin on bad input
        s = end;
        return v;
    }
    // Arc flags are single characters, not numbers: "0 015 5" is legal SVG and
    // means 0, 1, 5, 5. Reading them with strtof would swallow the coordinates.
    int flag() {
        skip();
        if (*s == '0' || *s == '1') { const int v = *s - '0'; ++s; return v; }
        return num() != 0.0f ? 1 : 0;
    }
};

bool isCmd(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

} // namespace

bool appendSvgPath(ID2D1GeometrySink* sink, const char* d) {
    if (!sink || !d) return false;
    Cursor p{ d };

    float cx = 0, cy = 0;     // current point
    float sx = 0, sy = 0;     // where this subpath started, for Z
    float kx = 0, ky = 0;     // last control point, for the S / T reflections
    bool  open = false;
    char  prev = 0;

    auto begin = [&](float x, float y) {
        if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
        // HOLLOW: these geometries are only ever stroked, never filled.
        sink->BeginFigure(D2D1_POINT_2F{ x, y }, D2D1_FIGURE_BEGIN_HOLLOW);
        open = true; sx = x; sy = y;
    };
    auto ensure = [&] { if (!open) begin(cx, cy); };

    for (;;) {
        p.skip();
        if (!*p.s) break;

        char cmd;
        if (isCmd(*p.s)) {
            cmd = *p.s++;
        } else if (prev) {
            // Bare coordinates repeat the last command; after a moveto they
            // become linetos, which is the one case that is not a plain repeat.
            cmd = (prev == 'M') ? 'L' : (prev == 'm') ? 'l' : prev;
        } else {
            return false;
        }

        const bool rel = (cmd >= 'a' && cmd <= 'z');
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
        const float ox = rel ? cx : 0.0f;
        const float oy = rel ? cy : 0.0f;

        switch (c) {
        case 'M': {
            const float x = p.num() + ox, y = p.num() + oy;
            begin(x, y); cx = x; cy = y; kx = x; ky = y;
            break;
        }
        case 'L': {
            const float x = p.num() + ox, y = p.num() + oy;
            ensure(); sink->AddLine(D2D1_POINT_2F{ x, y });
            cx = x; cy = y; kx = x; ky = y;
            break;
        }
        case 'H': {
            const float x = p.num() + ox;
            ensure(); sink->AddLine(D2D1_POINT_2F{ x, cy });
            cx = x; kx = x; ky = cy;
            break;
        }
        case 'V': {
            const float y = p.num() + oy;
            ensure(); sink->AddLine(D2D1_POINT_2F{ cx, y });
            cy = y; kx = cx; ky = y;
            break;
        }
        case 'C': {
            const float x1 = p.num() + ox, y1 = p.num() + oy;
            const float x2 = p.num() + ox, y2 = p.num() + oy;
            const float x  = p.num() + ox, y  = p.num() + oy;
            ensure();
            sink->AddBezier(D2D1::BezierSegment({ x1, y1 }, { x2, y2 }, { x, y }));
            cx = x; cy = y; kx = x2; ky = y2;
            break;
        }
        case 'S': {
            const float x2 = p.num() + ox, y2 = p.num() + oy;
            const float x  = p.num() + ox, y  = p.num() + oy;
            // The first control point mirrors the previous one, but only if the
            // previous command actually was a cubic.
            const char pu = static_cast<char>(std::toupper(static_cast<unsigned char>(prev)));
            const bool mirror = (pu == 'C' || pu == 'S');
            const float x1 = mirror ? 2 * cx - kx : cx;
            const float y1 = mirror ? 2 * cy - ky : cy;
            ensure();
            sink->AddBezier(D2D1::BezierSegment({ x1, y1 }, { x2, y2 }, { x, y }));
            cx = x; cy = y; kx = x2; ky = y2;
            break;
        }
        case 'Q': {
            const float x1 = p.num() + ox, y1 = p.num() + oy;
            const float x  = p.num() + ox, y  = p.num() + oy;
            ensure();
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({ x1, y1 }, { x, y }));
            cx = x; cy = y; kx = x1; ky = y1;
            break;
        }
        case 'T': {
            const float x = p.num() + ox, y = p.num() + oy;
            const char pu = static_cast<char>(std::toupper(static_cast<unsigned char>(prev)));
            const bool mirror = (pu == 'Q' || pu == 'T');
            const float x1 = mirror ? 2 * cx - kx : cx;
            const float y1 = mirror ? 2 * cy - ky : cy;
            ensure();
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({ x1, y1 }, { x, y }));
            cx = x; cy = y; kx = x1; ky = y1;
            break;
        }
        case 'A': {
            const float rx = p.num(), ry = p.num(), rot = p.num();
            const int large = p.flag(), sweep = p.flag();
            const float x = p.num() + ox, y = p.num() + oy;
            ensure();
            D2D1_ARC_SEGMENT a{};
            a.point = D2D1_POINT_2F{ x, y };
            a.size = D2D1_SIZE_F{ rx, ry };
            a.rotationAngle = rot;
            a.sweepDirection = sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                     : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
            a.arcSize = large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
            sink->AddArc(a);
            cx = x; cy = y; kx = x; ky = y;
            break;
        }
        case 'Z': {
            if (open) { sink->EndFigure(D2D1_FIGURE_END_CLOSED); open = false; }
            cx = sx; cy = sy; kx = sx; ky = sy;
            break;
        }
        default:
            return false;   // an element we were never meant to see
        }
        prev = cmd;
    }

    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    return true;
}

} // namespace oriel
