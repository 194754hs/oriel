#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include "metrics.h"

namespace oriel {

// One easing curve for the whole application, from the design sheet:
// cubic-bezier(.32, .72, 0, 1). Solved for t given x, then evaluated for y.
inline float ease(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    const float x1 = metrics::kEaseX1, y1 = metrics::kEaseY1;
    const float x2 = metrics::kEaseX2, y2 = metrics::kEaseY2;
    auto bez = [](float t, float a, float b) {
        const float u = 1.0f - t;
        return 3.0f * u * u * t * a + 3.0f * u * t * t * b + t * t * t;
    };
    // Newton on x(t) - x, with a bisection guard because this curve has a
    // near-vertical stretch where the derivative is useless.
    float t = x, lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 24; ++i) {
        const float cx = bez(t, x1, x2) - x;
        if (std::fabs(cx) < 1e-5f) break;
        if (cx > 0) hi = t; else lo = t;
        t = 0.5f * (lo + hi);
    }
    return bez(t, y1, y2);
}

// A scalar that slides from one value to another. Cheap enough to keep several
// per window; ask `active()` to decide whether another frame is needed.
class Tween {
public:
    void set(float v) { from_ = to_ = v; start_ = 0; }
    void to(float v, int ms) {
        if (to_ == v) return;
        from_ = value();
        to_ = v;
        durationMs_ = ms;
        start_ = GetTickCount64();
    }
    float value() const {
        if (!start_) return to_;
        const ULONGLONG dt = GetTickCount64() - start_;
        if (dt >= static_cast<ULONGLONG>(durationMs_)) return to_;
        const float p = static_cast<float>(dt) / static_cast<float>(durationMs_);
        return from_ + (to_ - from_) * ease(p);
    }
    bool active() const {
        return start_ && (GetTickCount64() - start_) < static_cast<ULONGLONG>(durationMs_);
    }
    float target() const { return to_; }

private:
    float from_ = 0, to_ = 0;
    ULONGLONG start_ = 0;
    int durationMs_ = 200;
};

} // namespace oriel
