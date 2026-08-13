// Approved layout constants, in DIPs. These come straight from the design
// bench's measurement sheet — change them there first, then here.
#pragma once

namespace oriel::metrics {

// ── window chrome ───────────────────────────────────────────────
inline constexpr float kTitleBar   = 34.0f;
inline constexpr float kToolBar    = 44.0f;
inline constexpr float kPathBar    = 26.0f;
inline constexpr float kActionBar  = 58.0f;   // picker mode only
inline constexpr float kButtonW    = 96.0f;
inline constexpr float kButtonH    = 30.0f;
inline constexpr float kSideBar    = 198.0f;
inline constexpr float kInspector  = 376.0f;
inline constexpr float kCaptionW   = 46.0f;   // per button
inline constexpr float kCaptionH   = 32.0f;

// ── column view (the primary view) ──────────────────────────────
inline constexpr float kColumn     = 206.0f;
inline constexpr float kPreview    = 250.0f;
inline constexpr float kRow        = 26.0f;
inline constexpr float kGripW      = 7.0f;    // divider hit area; the line is 1px

// ── radii ───────────────────────────────────────────────────────
inline constexpr float kRadiusWin  = 10.0f;
inline constexpr float kRadiusPill = 6.0f;

// ── glass ───────────────────────────────────────────────────────
inline constexpr float kBlurRadius = 24.0f;
inline constexpr float kSaturate   = 1.90f;

// ── motion: one easing curve, a fixed ladder of durations ───────
// cubic-bezier(.32, .72, 0, 1)
inline constexpr float kEaseX1 = 0.32f, kEaseY1 = 0.72f;
inline constexpr float kEaseX2 = 0.00f, kEaseY2 = 1.00f;

inline constexpr int kMsHover  =  60;   // rows: the pointer must never wait
inline constexpr int kMsMenu   = 110;
inline constexpr int kMsPill   = 180;
inline constexpr int kMsThumb  = 220;
inline constexpr int kMsColumn = 260;
inline constexpr int kMsPanel  = 280;

// Chrome reacts a touch slower than rows: these are targets, not a list being
// scanned, so the lift can be seen rather than just felt.
inline constexpr int kMsChrome  = 120;  // toolbar, tabs, sidebar rows
inline constexpr int kMsCaption = 100;  // window buttons
inline constexpr int kMsTab     = 220;  // a new tab opening to width
inline constexpr int kMsInk     = 140;  // panel contents fading up
inline constexpr int kMsInkWait =  60;  // ...after the frame has started moving

// ── icons ───────────────────────────────────────────────────────
// The weight is absolute, not proportional: a 12 DIP chevron and an 18 DIP
// folder carry the same stroke, which is what makes them read as one family.
// The set's own default works out to about 1.33 at this size; Windows renders
// thinner than the web does, so a little more is needed or the marks starve.
inline constexpr float kIconStroke  = 1.6f;
inline constexpr float kIconSide    = 16.0f;   // rows, toolbar
inline constexpr float kIconSideBar = 17.0f;   // sidebar, a touch larger
// Large previews are the exception: an absolute weight there reads as a hairline,
// so they keep the set's own proportion.
inline constexpr float kIconBigRatio = 2.0f / 24.0f;

// ── type ────────────────────────────────────────────────────────
// Optical tracking: small sizes loosen, large sizes tighten, crossing zero
// around 13px. Tracking is in em, applied as size * em.
struct TypeRole { float size; float trackingEm; float weight; };

inline constexpr TypeRole kBody  { 13.0f,  0.000f, 500.0f };
inline constexpr TypeRole kMeta  { 12.0f,  0.004f, 500.0f };
inline constexpr TypeRole kCap   { 11.0f,  0.009f, 500.0f };
inline constexpr TypeRole kTab   { 12.5f,  0.002f, 500.0f };
inline constexpr TypeRole kTitle { 13.5f, -0.005f, 650.0f };
inline constexpr TypeRole kHead  { 15.0f, -0.011f, 650.0f };

inline constexpr float kLineHeight = 1.25f;

} // namespace oriel::metrics
