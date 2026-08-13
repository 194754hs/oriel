#include "icons.h"
#include "svg_path.h"
#include <d2d1helper.h>
#include <cwctype>

namespace oriel {

using Microsoft::WRL::ComPtr;

#include "icon_data.inc"

namespace {
// The set draws inside roughly this much of its 24 unit grid. Scaling by the
// full grid would make every mark read a size smaller than the text beside it.
constexpr float kIconInk = 20.0f;
} // namespace

bool IconSet::init(ID2D1Factory1* factory) {
    if (!factory) return false;

    // Round caps and round joins, once, for everything. Without them corners
    // come out cut square and the whole family looks assembled rather than
    // drawn - the single biggest tell of a cheap icon set.
    const auto props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
    if (FAILED(factory->CreateStrokeStyle(props, nullptr, 0, &stroke_))) return false;

    for (int i = 1; i < static_cast<int>(Icon::Count); ++i) {
        const IconSrc& src = kIconSrc[i];
        if (src.count <= 0) continue;

        ComPtr<ID2D1PathGeometry> g;
        if (FAILED(factory->CreatePathGeometry(&g))) return false;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(g->Open(&sink))) return false;
        for (int k = 0; k < src.count; ++k) appendSvgPath(sink.Get(), src.d[k]);
        if (FAILED(sink->Close())) return false;
        geo_[i] = g;
    }
    return true;
}

void IconSet::draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, Icon ic,
                   float cx, float cy, float size, float stroke) const {
    const int i = static_cast<int>(ic);
    if (i <= 0 || i >= static_cast<int>(Icon::Count) || !geo_[i]) return;

    const float k = size / kIconInk;
    const float half = kIconGrid * 0.5f;

    D2D1_MATRIX_3X2_F saved;
    dc->GetTransform(&saved);
    dc->SetTransform(D2D1::Matrix3x2F::Scale(k, k) *
                     D2D1::Matrix3x2F::Translation(cx - half * k, cy - half * k) *
                     saved);
    // The weight is given in final DIPs, so undo the scale for it. That is what
    // keeps a 12 DIP chevron and an 18 DIP folder looking like one family.
    dc->DrawGeometry(geo_[i].Get(), brush, stroke / k, stroke_.Get());
    dc->SetTransform(saved);
}

namespace {
// The extensions we have a specific mark for. Everything else is somebody's
// document, and the "somebody" is the useful part.
struct ExtMark { const wchar_t* ext; Icon ic; };
constexpr ExtMark kExtMarks[] = {
        { L"png", Icon::Image }, { L"jpg", Icon::Image }, { L"jpeg", Icon::Image },
        { L"gif", Icon::Image }, { L"webp", Icon::Image }, { L"bmp", Icon::Image },
        { L"svg", Icon::Image }, { L"ico", Icon::Image }, { L"tif", Icon::Image },
        { L"mp4", Icon::Video }, { L"mov", Icon::Video }, { L"mkv", Icon::Video },
        { L"avi", Icon::Video }, { L"webm", Icon::Video },
        { L"mp3", Icon::Audio }, { L"m4a", Icon::Audio }, { L"wav", Icon::Audio },
        { L"flac", Icon::Audio }, { L"ogg", Icon::Audio },
        { L"zip", Icon::Archive }, { L"7z", Icon::Archive }, { L"rar", Icon::Archive },
        { L"tar", Icon::Archive }, { L"gz", Icon::Archive },
        { L"exe", Icon::App }, { L"msi", Icon::App }, { L"dll", Icon::App },
        { L"cpp", Icon::Code }, { L"h", Icon::Code }, { L"c", Icon::Code },
        { L"cs", Icon::Code }, { L"ts", Icon::Code }, { L"js", Icon::Code },
        { L"py", Icon::Code }, { L"ps1", Icon::Code }, { L"html", Icon::Code },
        { L"css", Icon::Code }, { L"json", Icon::Code }, { L"xml", Icon::Code },
        { L"ninja", Icon::Code }, { L"cmake", Icon::Code }, { L"jsx", Icon::Code },
        { L"inc", Icon::Code }, { L"cmd", Icon::Code }, { L"rs", Icon::Code },
        // Plain text is ours as well. Without these the default text handler's
        // mark leaks in, and a note should not look like whatever opens it.
        { L"txt", Icon::Doc }, { L"md", Icon::Doc }, { L"log", Icon::Doc },
        { L"csv", Icon::Doc }, { L"rtf", Icon::Doc }, { L"ini", Icon::Doc },
        { L"yml", Icon::Doc }, { L"yaml", Icon::Doc }, { L"toml", Icon::Doc },
};

std::wstring extOf(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= name.size()) return {};
    std::wstring ext = name.substr(dot + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext;
}
} // namespace

bool haveMarkForExt(const std::wstring& ext) {
    for (const auto& t : kExtMarks)
        if (ext == t.ext) return true;
    return false;
}

Icon iconFor(const Entry& e) {
    if (e.isDir) return Icon::Folder;
    const std::wstring ext = extOf(e.name);
    for (const auto& t : kExtMarks)
        if (ext == t.ext) return t.ic;
    return Icon::Doc;
}

} // namespace oriel
