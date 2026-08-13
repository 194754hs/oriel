#pragma once
#include <d2d1_1.h>
#include <wrl/client.h>
#include "entry.h"

namespace oriel {

// The marks, as path geometry on a 24 unit grid.
//
// The outlines come from an open icon set rather than being drawn here: hand
// authoring a coherent family is a job of its own, and the result read as cheap
// next to a real one. The path data lives in the generated icon_data.inc; the
// order below IS the table's order and a static_assert holds them together.
// Rerun design/gen-icons.ps1 after changing this enum.
enum class Icon {
    None,
    Folder, Doc, Pdf, Image, Video, Archive, Audio, Code, App,
    Clock, Desktop, Download, Star, Pc, Drive, Cloud, Network,
    ChevronRight, ChevronLeft, ChevronDown, SidebarToggle,
    Sort, Share, More, Search, Plus, Gear,
    ViewColumn, ViewList, ViewIcon,
    Count
};

class IconSet {
public:
    bool init(ID2D1Factory1*);
    // `size` is the side the drawn part of the mark should occupy, in DIPs, and
    // `stroke` is the final on-screen weight - it does not scale with size, so
    // marks at different sizes still look like one family.
    void draw(ID2D1DeviceContext*, ID2D1SolidColorBrush*, Icon,
              float cx, float cy, float size, float stroke) const;

private:
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geo_[static_cast<int>(Icon::Count)];
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> stroke_;
};

Icon iconFor(const Entry&);

// True when our own family already has something specific to say about this
// extension (lowercased, no dot). Where it does not, the file is some
// application's document and that application's mark is far more informative
// than another generic page outline.
bool haveMarkForExt(const std::wstring& ext);

// CSS `border-radius: 999px` produces a stadium because both radii are scaled
// down together. D2D clamps radiusX and radiusY independently, so passing a
// huge value there yields an ellipse instead. This is the shape we actually
// meant every time the design sheet says "全丸".
inline D2D1_ROUNDED_RECT stadium(const D2D1_RECT_F& r) {
    const float k = 0.5f * ((r.right - r.left) < (r.bottom - r.top)
                            ? (r.right - r.left) : (r.bottom - r.top));
    return D2D1_ROUNDED_RECT{ r, k, k };
}

} // namespace oriel
