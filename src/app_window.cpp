#include "i18n.h"
#include "app_window.h"
#include "assoc.h"
#include "actions.h"
#include "metrics.h"
#include "log.h"
#include "icons.h"

#include <dwmapi.h>
#include <dxgi1_3.h>
#include <d2d1helper.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

// Present in the Windows 11 SDK, declared defensively so an older SDK still builds.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif

namespace oriel {

// Tags are named by colour, so the colour belongs to the model rather than to
// whichever surface happened to draw them first.
static const D2D1_COLOR_F kTagColors[kTagCount] = {
    rgba(0xE5,0x54,0x4B), rgba(0xE8,0xA3,0x3D),
    rgba(0x4F,0xA9,0x6B), rgba(0x5A,0x8F,0xD6) };

#define STEP(hr, what)                                                      \
    do { HRESULT _hr = (hr);                                                \
         if (FAILED(_hr)) { logf("FAIL %s -> 0x%08X", what, _hr); return false; } \
    } while (0)

// Posted, not sent: it lands after every pending input message, so the build
// happens in the gap while the user is still looking at the row they picked.
constexpr UINT WM_ORIEL_PREP_MENU = WM_APP + 2;

namespace M = metrics;
static const wchar_t* kClass = L"OrielShell";
static const wchar_t* kUiFont = L"Segoe UI Variable Text";  // bundled face lands in M1

// ── the system accent, so the app agrees with the rest of the desktop ──
static D2D1_COLOR_F systemAccent() {
    DWORD argb = 0; BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque))) {
        return D2D1_COLOR_F{ ((argb >> 16) & 0xFF) / 255.0f,
                             ((argb >>  8) & 0xFF) / 255.0f,
                             ( argb        & 0xFF) / 255.0f, 1.0f };
    }
    return rgba(0x00, 0x78, 0xD4);
}

static bool systemUsesDark() {
    DWORD v = 1, cb = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &cb);
    return v == 0;
}

int  AppWindow::frameBorder() const {
    return GetSystemMetricsForDpi(SM_CXFRAME, dpi_) +
           GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
}
bool AppWindow::maximized() const {
    WINDOWPLACEMENT wp{ sizeof(wp) };
    return GetWindowPlacement(hwnd_, &wp) && wp.showCmd == SW_SHOWMAXIMIZED;
}

// ══ creation ═══════════════════════════════════════════════════════
bool AppWindow::create(HINSTANCE hinst, int nCmdShow, const std::wstring& startPath) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = &AppWindow::wndProc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;           // we paint every pixel ourselves
    wc.lpszClassName = kClass;
    // A picker is a second window of the same class, so a repeat registration
    // is expected rather than an error.
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    // Before the window exists: creating and sizing it sends WM_SIZE, and the
    // handlers for that read model(), which needs a tab to already be there.
    tabs_.emplace_back();
    tabBorn_.push_back(0);   // 0 = already at width; the first tab does not open
    activeTab_ = 0;
    tags_.load();
    // A picker borrows this window class but not the user's session: it must
    // not adopt the last folder or write the window position back.
    if (!picker_) loadSettings();
    applyTheme();

    // WS_EX_NOREDIRECTIONBITMAP is mandatory when the window's content comes from
    // DirectComposition. Without it the window keeps an opaque GDI redirection
    // surface that composites in front of our alpha, and nothing is ever see-through.
    hwnd_ = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP, kClass, L"Oriel", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 720,
        nullptr, nullptr, hinst, this);
    if (!hwnd_) { logf("FAIL CreateWindowExW -> %lu", GetLastError()); return false; }

    dpi_ = GetDpiForWindow(hwnd_);

    // CreateWindowExW takes physical pixels, so the default size would shrink to
    // half on a 200% display. Restate it in DIPs and centre it on the work area.
    {
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int w = settings_.getInt(L"window.w", 0);
        int h = settings_.getInt(L"window.h", 0);
        int x = settings_.getInt(L"window.x", INT_MIN);
        int y = settings_.getInt(L"window.y", INT_MIN);
        // A remembered position is only honoured if it still lands on a screen:
        // unplugging a monitor must not hide the window off the edge.
        RECT want{ x, y, x + w, y + h };
        const bool usable = !picker_ && w > 200 && h > 200 && x != INT_MIN && y != INT_MIN &&
                            MonitorFromRect(&want, MONITOR_DEFAULTTONULL) != nullptr;
        if (!usable) {
            w = static_cast<int>(px(1180.0f));
            h = static_cast<int>(px(760.0f));
            x = work.left + ((work.right - work.left) - w) / 2;
            y = work.top  + ((work.bottom - work.top) - h) / 2;
        }
        SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    BOOL dark = theme_.dark;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // Slightly rounder than the platform default, per the measurement sheet.
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    // The acrylic backdrop is what the sidebar glass samples: we leave those
    // pixels transparent and the compositor shows the desktop behind.
    // The frame has to be extended across the whole client area first, or DWM
    // only draws the backdrop under the (now nonexistent) standard frame.
    MARGINS all{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd_, &all);
    DWORD backdrop = DWMSBT_TRANSIENTWINDOW;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    if (!createDevices()) return false;

    hinst_ = hinst;
    thumbs_.start();
    // Only the resident window answers shims; a picker must not spawn pickers.
    if (!picker_) dialogServer_.start(hwnd_);

    // Start where asked, else where the user's own files are.
    // An explicit path always wins; otherwise pick up where the last session
    // left off, and only fall back to a default if that place is gone.
    std::wstring root = startPath;
    if (root.empty() && !picker_) {
        const std::wstring last = settings_.getStr(L"session.folder");
        if (!last.empty()) {
            const DWORD a = GetFileAttributesW(last.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
                root = last;
                sideSelected_ = std::clamp(settings_.getInt(L"session.sidebarRow", 2), 0, 11);
            }
        }
    }
    if (root.empty()) root = documentsFolder();
    if (!picker_ && settings_.getBool(L"general.showHidden", false))
        model().setShowHidden(hwnd_, true);
    model().setRoot(hwnd_, root.empty() ? Place::thisPC() : Place::directory(root));
    sidePill_.set(sidebarPillY(sideSelected_));
    sideOpen_ = picker_ ? true : settings_.getBool(L"view.sidebar", true);
    sideW_.set(sideOpen_ ? M::kSideBar : 0.0f);
    pushHistory(model().columns().empty() ? Place::thisPC() : model().columns().front().place);

    // Recompute the non-client area now that we own the frame.
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hwnd_, (!picker_ && settings_.getBool(L"window.max", false))
                          ? SW_SHOWMAXIMIZED : nCmdShow);
    // Well after the window is up and the first folder has drawn: warming costs
    // a third of a second and must not be part of the startup the user sees.
    if (!picker_) SetTimer(hwnd_, 5, 2000, nullptr);
    return true;
}

bool AppWindow::createDevices() {
    const UINT base = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL got{};
    bool debugLayer = false;
    HRESULT hr = E_FAIL;

    // The debug layer is an optional Windows feature. Ask for it, but never let
    // its absence stop the app: that returns DXGI_ERROR_SDK_COMPONENT_MISSING.
    auto tryCreate = [&](D3D_DRIVER_TYPE type, UINT flags) {
        return D3D11CreateDevice(nullptr, type, nullptr, flags, nullptr, 0,
                                 D3D11_SDK_VERSION, &d3d_, &got, nullptr);
    };
#ifdef _DEBUG
    hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE, base | D3D11_CREATE_DEVICE_DEBUG);
    if (SUCCEEDED(hr)) debugLayer = true;
    else logf("no D3D debug layer (0x%08X); continuing without it", hr);
#endif
    if (FAILED(hr)) hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE, base);
    if (FAILED(hr)) {
        logf("hardware device failed 0x%08X, falling back to WARP", hr);
        // Software fallback keeps the app usable on machines without a usable GPU.
        STEP(tryCreate(D3D_DRIVER_TYPE_WARP, base), "D3D11CreateDevice");
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    STEP(d3d_.As(&dxgiDevice), "QI IDXGIDevice");

    ComPtr<IDXGIAdapter> adapter;
    STEP(dxgiDevice->GetAdapter(&adapter), "GetAdapter");
    ComPtr<IDXGIFactory2> factory;
    STEP(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIFactory2");

    RECT rc{}; GetClientRect(hwnd_, &rc);
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width  = std::max<UINT>(1, rc.right  - rc.left);
    sd.Height = std::max<UINT>(1, rc.bottom - rc.top);
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // Premultiplied alpha is the whole point: transparent pixels reveal the
    // DWM backdrop rather than painting black.
    sd.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;
    STEP(factory->CreateSwapChainForComposition(d3d_.Get(), &sd, nullptr, &swap_),
         "CreateSwapChainForComposition");

    STEP(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcomp_)),
         "DCompositionCreateDevice");
    STEP(dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &dtarget_), "CreateTargetForHwnd");
    STEP(dcomp_->CreateVisual(&dvisual_), "CreateVisual");
    dvisual_->SetContent(swap_.Get());
    dtarget_->SetRoot(dvisual_.Get());
    dcomp_->Commit();

    D2D1_FACTORY_OPTIONS fo{};
    // Only meaningful when the matching debug layer is actually present.
    if (debugLayer) fo.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
    STEP(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
             __uuidof(ID2D1Factory1), &fo,
             reinterpret_cast<void**>(d2dFactory_.GetAddressOf())), "D2D1CreateFactory");
    STEP(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_), "D2D CreateDevice");
    STEP(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_),
         "CreateDeviceContext");

    STEP(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
             reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())), "DWriteCreateFactory");

    if (!icons_.init(d2dFactory_.Get())) { logf("FAIL icon set"); return false; }

    resizeSwapChain(sd.Width, sd.Height);
    STEP(dc_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_), "CreateSolidColorBrush");
    logf("devices ready (dpi=%u)", dpi_);
    return true;
}

void AppWindow::resizeSwapChain(UINT w, UINT h) {
    if (!swap_ || !dc_) return;
    dc_->SetTarget(nullptr);
    if (FAILED(swap_->ResizeBuffers(0, std::max<UINT>(1, w), std::max<UINT>(1, h),
                                    DXGI_FORMAT_UNKNOWN, 0)))
        return;

    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return;
    auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    ComPtr<ID2D1Bitmap1> target;
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target))) return;
    dc_->SetTarget(target.Get());
    // Drawing happens in DIPs from here on.
    dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
}

// ══ painting ═══════════════════════════════════════════════════════
void AppWindow::drawIconBitmap(ID2D1Bitmap* bmp, float cx, float cy, float side) {
    if (!bmp) return;
    const auto s = bmp->GetSize();
    if (s.width <= 0 || s.height <= 0) return;
    // Fit, never fill: a stretched application mark reads as broken.
    const float k = side / (s.width > s.height ? s.width : s.height);
    const float w = s.width * k, h = s.height * k;
    dc_->DrawBitmap(bmp, D2D1_RECT_F{ cx - w * 0.5f, cy - h * 0.5f,
                                      cx + w * 0.5f, cy + h * 0.5f },
                    1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void AppWindow::fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    dc_->FillRectangle(r, brush_.Get());
}

void AppWindow::hairline(float x0, float y0, float x1, float y1, const D2D1_COLOR_F& c) {
    // Snap to a whole device pixel so the line stays crisp at any scale.
    const float unit = 96.0f / static_cast<float>(dpi_);
    brush_->SetColor(c);
    dc_->FillRectangle(D2D1_RECT_F{ x0, y0, (x1 > x0 ? x1 : x0 + unit),
                                    (y1 > y0 ? y1 : y0 + unit) }, brush_.Get());
}

// Text formats are immutable and were being rebuilt for every row of every
// frame - dozens of COM allocations per paint. Cached by size+weight, and
// shared by drawing and measuring so the two can never disagree.
IDWriteTextFormat* AppWindow::formatFor(float size, float weight) {
    const uint64_t key = static_cast<uint64_t>(size * 100) * 1000 +
                         static_cast<uint64_t>(weight);
    ComPtr<IDWriteTextFormat>& fmt = formats_[key];
    if (!fmt) {
        if (FAILED(dwrite_->CreateTextFormat(kUiFont, nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(static_cast<int>(weight)),
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                size, L"ja-jp", &fmt)))
            return nullptr;
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        // Long file names are the norm, so clip with an ellipsis rather than
        // cutting a glyph in half at the column edge.
        DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(fmt.Get(), &ellipsis)))
            fmt->SetTrimming(&trim, ellipsis.Get());
    }
    return fmt.Get();
}

void AppWindow::text(const wchar_t* s, const D2D1_RECT_F& box, const D2D1_COLOR_F& c,
                     float size, float trackingEm, float weight,
                     DWRITE_TEXT_ALIGNMENT align) {
    IDWriteTextFormat* fmt = formatFor(size, weight);
    if (!fmt) return;
    fmt->SetTextAlignment(align);

    // Optical tracking. DirectWrite has no letter-spacing on the format, so it
    // goes through the typography axis on a text layout.
    ComPtr<IDWriteTextLayout> layout;
    UINT32 len = 0; while (s[len]) ++len;
    if (FAILED(dwrite_->CreateTextLayout(s, len, fmt,
            box.right - box.left, box.bottom - box.top, &layout)))
        return;
    ComPtr<IDWriteTextLayout1> layout1;
    if (SUCCEEDED(layout.As(&layout1))) {
        const float track = size * trackingEm;
        layout1->SetCharacterSpacing(track * 0.5f, track * 0.5f, 0.0f,
                                     DWRITE_TEXT_RANGE{ 0, len });
    }
    brush_->SetColor(c);
    dc_->DrawTextLayout(D2D1_POINT_2F{ box.left, box.top }, layout.Get(), brush_.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

float AppWindow::textWidth(const wchar_t* s, float size, float trackingEm,
                           float weight, int len) {
    if (!s || !*s) return 0.0f;
    const UINT32 n = (len >= 0) ? static_cast<UINT32>(len)
                                : static_cast<UINT32>(wcslen(s));
    if (!n) return 0.0f;
    IDWriteTextFormat* fmt = formatFor(size, weight);
    if (!fmt) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(s, n, fmt, 100000.0f, 100.0f, &layout)))
        return 0.0f;
    ComPtr<IDWriteTextLayout1> layout1;
    if (SUCCEEDED(layout.As(&layout1))) {
        const float track = size * trackingEm;
        layout1->SetCharacterSpacing(track * 0.5f, track * 0.5f, 0.0f,
                                     DWRITE_TEXT_RANGE{ 0, n });
    }
    DWRITE_TEXT_METRICS m{};
    if (FAILED(layout->GetMetrics(&m))) return 0.0f;
    return m.widthIncludingTrailingWhitespace;
}

// A text field: rounded well, selection band, caret, placeholder. The caret
// blinks off the same frame timer as everything else rather than owning one.
void AppWindow::paintField(const D2D1_RECT_F& box, TextField& f,
                           const wchar_t* placeholder,
                           float size, float trackingEm, float weight) {
    // Its own surface, not a hashed address: sharing Fx::Search with the search
    // capsule risks two fields agreeing on a key and fading as one.
    const float g = fades_.at(fkey(Fx::Field, (&f == &nameField_) ? 0 : 1),
                              f.focused, M::kMsChrome);
    auto bg = theme_.selSoft; bg.a += 0.05f * g;
    brush_->SetColor(bg);
    dc_->FillRoundedRectangle(stadium(box), brush_.Get());
    if (g > 0.002f) {
        auto edge = theme_.accent; edge.a = g;
        brush_->SetColor(edge);
        dc_->DrawRoundedRectangle(stadium(box), brush_.Get(), dips(2));
    }

    const float tx = box.left + 12;
    const D2D1_RECT_F inner{ tx, box.top, box.right - 10, box.bottom };
    dc_->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_ALIASED);

    if (f.text.empty() && !f.focused) {
        text(placeholder, inner, theme_.ink2, size, trackingEm, weight);
    } else {
        if (f.hasSelection()) {
            const float x0 = tx + textWidth(f.text.c_str(), size, trackingEm, weight,
                                            static_cast<int>(f.selLo()));
            const float x1 = tx + textWidth(f.text.c_str(), size, trackingEm, weight,
                                            static_cast<int>(f.selHi()));
            auto s = theme_.accent; s.a = 0.28f;
            brush_->SetColor(s);
            dc_->FillRectangle(D2D1_RECT_F{ x0, box.top + 4, x1, box.bottom - 4 },
                               brush_.Get());
        }
        text(f.text.c_str(), inner, theme_.ink, size, trackingEm, weight);
        if (f.focused && ((GetTickCount64() / 530) & 1) == 0) {
            const float cx = tx + textWidth(f.text.c_str(), size, trackingEm, weight,
                                            static_cast<int>(f.caret));
            brush_->SetColor(theme_.ink);
            dc_->FillRectangle(D2D1_RECT_F{ cx, box.top + 4, cx + dips(2), box.bottom - 4 },
                               brush_.Get());
        }
    }
    dc_->PopAxisAlignedClip();
}

// ── tabs ───────────────────────────────────────────────────────────
// A tab opens to its width rather than appearing at it, and the ones already
// there slide over to make room. 220ms, same curve as everything else.
float AppWindow::tabWidth(int i) const {
    const float full = 168;
    if (i < 0 || i >= static_cast<int>(tabBorn_.size()) || !tabBorn_[i]) return full;
    const ULONGLONG age = GetTickCount64() - tabBorn_[i];
    if (age >= static_cast<ULONGLONG>(M::kMsTab)) return full;
    return full * ease(static_cast<float>(age) / static_cast<float>(M::kMsTab));
}

bool AppWindow::tabsAnimating() const {
    const ULONGLONG now = GetTickCount64();
    for (ULONGLONG born : tabBorn_)
        if (born && now - born < static_cast<ULONGLONG>(M::kMsTab)) return true;
    return false;
}

D2D1_RECT_F AppWindow::tabRect(int i) const {
    // 30 tall inside a 34 bar: 4px of chrome above, flush at the bottom.
    const float h = 30;
    float x = 8;
    for (int k = 0; k < i; ++k) x += tabWidth(k) + 2;
    return { x, M::kTitleBar - h, x + tabWidth(i), M::kTitleBar };
}

D2D1_RECT_F AppWindow::tabClose(int i) const {
    const auto b = tabRect(i);
    const float cy = (b.top + b.bottom) * 0.5f;
    return { b.right - 26, cy - 11, b.right - 4, cy + 11 };
}

void AppWindow::closeTab(int i) {
    if (i < 0 || i >= static_cast<int>(tabs_.size()) || tabs_.size() <= 1) return;
    tabs_.erase(tabs_.begin() + i);
    tabBorn_.erase(tabBorn_.begin() + i);
    if (activeTab_ >= static_cast<int>(tabs_.size()))
        activeTab_ = static_cast<int>(tabs_.size()) - 1;
    else if (activeTab_ > i) --activeTab_;
    hotTab_ = -1;
    hotTabClose_ = -1;
    // The fades are keyed by tab index, and every index just shifted.
    scrollX_ = 0;
    tick();
}

void AppWindow::addTab(const Place& p) {
    tabs_.emplace_back();
    tabBorn_.push_back(GetTickCount64());
    activeTab_ = static_cast<int>(tabs_.size()) - 1;
    model().setRoot(hwnd_, p);
    scrollX_ = 0;
    tick();     // render() alone would never start the frame timer
}

// A tab is rounded along its top edge only: it belongs to the surface beneath
// it, so the bottom has to stay square and flush with the title bar.
static void fillTabShape(ID2D1DeviceContext* dc, ID2D1Factory1* factory,
                         ID2D1SolidColorBrush* brush, const D2D1_RECT_F& b, float r) {
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> g;
    if (FAILED(factory->CreatePathGeometry(&g))) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(g->Open(&sink))) return;
    sink->BeginFigure({ b.left, b.bottom }, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine({ b.left, b.top + r });
    sink->AddArc(D2D1::ArcSegment({ b.left + r, b.top }, { r, r }, 0,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddLine({ b.right - r, b.top });
    sink->AddArc(D2D1::ArcSegment({ b.right, b.top + r }, { r, r }, 0,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddLine({ b.right, b.bottom });
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    dc->FillGeometry(g.Get(), brush);
}

void AppWindow::paintTabs(const D2D1_RECT_F&) {
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        const auto b = tabRect(i);
        const bool on = (i == activeTab_);
        if (b.right - b.left < 2) continue;          // still opening from nothing
        if (on) {
            brush_->SetColor(theme_.content);
            fillTabShape(dc_.Get(), d2dFactory_.Get(), brush_.Get(), b, 8);
        } else {
            const float g = fades_.at(fkey(Fx::Tab, i), i == hotTab_, M::kMsChrome);
            if (g > 0.002f) {
                auto c = theme_.hover; c.a *= g;
                brush_->SetColor(c);
                fillTabShape(dc_.Get(), d2dFactory_.Get(), brush_.Get(), b, 8);
            }
        }
        const auto* c = tabs_[i].deepestListed();
        const std::wstring label = c ? c->place.label : L"…";
        const float cy = (b.top + b.bottom) * 0.5f;
        // Contents are clipped to the tab so nothing spills while it opens.
        dc_->PushAxisAlignedClip(b, D2D1_ANTIALIAS_MODE_ALIASED);

        // A close button on the active tab, and on whichever is hovered: a tab
        // you can open but not close is a leak the user has to restart to fix.
        bool showClose = false;
        if (tabs_.size() > 1 && (on || i == hotTab_) && b.right - b.left > 90) {
            showClose = true;
            const auto cb = tabClose(i);
            const float ccx = (cb.left + cb.right) * 0.5f, ccy = (cb.top + cb.bottom) * 0.5f;
            const float cg = fades_.at(fkey(Fx::TabClose, i), hotTabClose_ == i, M::kMsChrome);
            if (cg > 0.002f) {
                auto hc = theme_.hover; hc.a *= cg * 2.2f;
                brush_->SetColor(hc);
                dc_->FillRoundedRectangle(stadium(cb), brush_.Get());
            }
            brush_->SetColor(lerpColor(theme_.ink2, theme_.ink, cg));
            const float s = 4.0f, w = dips(1.4f);
            dc_->DrawLine({ ccx - s, ccy - s }, { ccx + s, ccy + s }, brush_.Get(), w);
            dc_->DrawLine({ ccx + s, ccy - s }, { ccx - s, ccy + s }, brush_.Get(), w);
        }
        (void)showClose;
        // set the colour BEFORE drawing: the icon uses whatever the brush holds,
        // and leaving it on the tab fill painted white on white.
        brush_->SetColor(on ? theme_.ink : theme_.ink2);
        icons_.draw(dc_.Get(), brush_.Get(), Icon::Folder, b.left + 19, cy, M::kIconSide, M::kIconStroke);
        text(label.c_str(), { b.left + 33, b.top, b.right - 10, b.bottom },
             on ? theme_.ink : theme_.ink2,
             M::kTab.size, M::kTab.trackingEm,
             on ? M::kTitle.weight : M::kTab.weight);
        dc_->PopAxisAlignedClip();
    }
    const auto last = tabRect(static_cast<int>(tabs_.size()) - 1);
    {
        const float cx = last.right + 18, cy = (last.top + last.bottom) * 0.5f;
        const float g = fades_.at(fkey(Fx::NewTab, 0), hotChrome_ == HotNewTab,
                                  M::kMsChrome);
        if (g > 0.002f) {
            auto c = theme_.hover; c.a *= g;
            brush_->SetColor(c);
            dc_->FillRoundedRectangle(stadium({ cx - 13, cy - 13, cx + 13, cy + 13 }),
                                      brush_.Get());
        }
        brush_->SetColor(lerpColor(theme_.ink2, theme_.ink, g));
        icons_.draw(dc_.Get(), brush_.Get(), Icon::Plus, cx, cy, M::kIconSide, M::kIconStroke);
    }
}

void AppWindow::paintTitleBar(const D2D1_RECT_F& r) {
    fill(r, theme_.chrome);
    hairline(r.left, r.bottom - dips(1), r.right, r.bottom, theme_.hair);
    if (!picker_) paintTabs(r);
    paintCaptionButtons(r);
}

void AppWindow::paintCaptionButtons(const D2D1_RECT_F& r) {
    const float w = M::kCaptionW, h = M::kCaptionH;
    const int codes[3] = { HTMINBUTTON, HTMAXBUTTON, HTCLOSE };
    for (int i = 0; i < 3; ++i) {
        D2D1_RECT_F b{ r.right - w * (3 - i), r.top, r.right - w * (2 - i), r.top + h };
        const float g = fades_.at(fkey(Fx::Caption, i), hotCaption_ == codes[i],
                                  M::kMsCaption);
        if (g > 0.002f) {
            auto c = (codes[i] == HTCLOSE) ? theme_.captionX : theme_.hover;
            c.a *= g;
            fill(b, c);
        }
        // The glyph turns white only as far as the red has actually arrived.
        D2D1_COLOR_F ink = theme_.ink;
        if (codes[i] == HTCLOSE)
            ink = lerpColor(theme_.ink, rgba(255, 255, 255), g);
        brush_->SetColor(ink);
        const float cx = (b.left + b.right) * 0.5f, cy = (b.top + b.bottom) * 0.5f, s = 5.0f;
        const float stroke = dips(1);
        if (i == 0) {
            dc_->DrawLine({ cx - s, cy }, { cx + s, cy }, brush_.Get(), stroke);
        } else if (i == 1) {
            D2D1_ROUNDED_RECT rr{ { cx - s, cy - s, cx + s, cy + s }, 1.5f, 1.5f };
            dc_->DrawRoundedRectangle(rr, brush_.Get(), stroke);
        } else {
            dc_->DrawLine({ cx - s, cy - s }, { cx + s, cy + s }, brush_.Get(), stroke);
            dc_->DrawLine({ cx + s, cy - s }, { cx - s, cy + s }, brush_.Get(), stroke);
        }
    }
}

void AppWindow::paintSideBar(const D2D1_RECT_F& r) {
    // Deliberately NOT filled: these pixels stay transparent so the DWM acrylic
    // shows the desktop through them. Only the edges are drawn.
    hairline(r.right - dips(1), r.top, r.right, r.bottom, theme_.hair);
    hairline(r.left, r.top, r.left + dips(1), r.bottom, theme_.spec);

    // The three chrome buttons sit on the sidebar's own top strip, level with
    // the toolbar on the other side of the divider.
    // The three nav buttons are drawn separately, in window coordinates: they
    // have to survive the sidebar collapsing, or the control that reopens it
    // disappears with it.

    const wchar_t* sections[] = { T(L"よく使う項目"), T(L"場所"), T(L"タグ") };
    static const wchar_t* items[][5] = {
        { T(L"最近の項目"), T(L"デスクトップ"), T(L"書類"), T(L"ダウンロード"), L"Projects" },
        { T(L"この PC"), T(L"ローカル (C:)"), L"OneDrive", nullptr, nullptr },
        { T(L"至急"), T(L"確認中"), T(L"納品済"), T(L"資料"), nullptr },
    };
    static const Icon icons[][5] = {
        { Icon::Clock, Icon::Desktop, Icon::Doc, Icon::Download, Icon::Star },
        { Icon::Pc, Icon::Drive, Icon::Cloud, Icon::None, Icon::None },
        { Icon::None, Icon::None, Icon::None, Icon::None, Icon::None },
    };
    const D2D1_COLOR_F* tagColors = kTagColors;

    // The pill slides between items rather than reappearing at the new one:
    // 180ms on the shared curve, per the motion sheet.
    {
        const float py = sidePill_.value();
        auto tint = theme_.accent; tint.a = 0.15f;
        D2D1_ROUNDED_RECT rr{ { r.left + 8, py, r.right - 8, py + setRowHeight_ },
                              M::kRadiusPill, M::kRadiusPill };
        brush_->SetColor(tint);
        dc_->FillRoundedRectangle(rr, brush_.Get());
    }

    int flat = 0;
    float y = r.top + M::kToolBar;
    for (int s = 0; s < 3; ++s) {
        // Not ink2: that tone is picked against the opaque content surface, but
        // the sidebar is glass over whatever the desktop happens to be, and a
        // mid grey on a mid grey wallpaper disappears. The row icons already
        // compensated for this; the section labels had been left behind.
        {
            auto c = theme_.ink; c.a = 0.55f;
            text(sections[s], { r.left + 16, y, r.right - 8, y + 22 },
                 c, M::kCap.size, M::kCap.trackingEm, M::kCap.weight);
        }
        y += 22;
        for (int i = 0; i < 5 && items[s][i]; ++i, ++flat) {
            const bool current = (flat == sideSelected_);
            D2D1_RECT_F row{ r.left + 8, y, r.right - 8, y + setRowHeight_ };
            const float g = fades_.at(fkey(Fx::SideRow, flat),
                                      !current && flat == sideHover_, M::kMsChrome);
            if (g > 0.002f) {
                D2D1_ROUNDED_RECT rr{ row, M::kRadiusPill, M::kRadiusPill };
                auto h = theme_.hover; h.a *= g * 1.6f;
                brush_->SetColor(h);
                dc_->FillRoundedRectangle(rr, brush_.Get());
            }
            const float cy = (row.top + row.bottom) * 0.5f;
            // The label recolours over 100ms while the pill takes 180ms to get
            // there: the destination reads first and the pill catches up.
            const float on = fades_.at(fkey(Fx::SideSel, flat), current, M::kMsCaption);
            if (s == 2) {
                // tags are named by colour, so a dot says more than a glyph
                brush_->SetColor(tagColors[i]);
                D2D1_ELLIPSE dot{ { row.left + 17, cy }, 4.5f, 4.5f };
                dc_->FillEllipse(dot, brush_.Get());
            } else {
                // ink2 washes out over the glass; the sidebar needs more weight
                auto c = theme_.ink; c.a = 0.68f;
                brush_->SetColor(lerpColor(c, theme_.accent, on));
                icons_.draw(dc_.Get(), brush_.Get(), icons[s][i], row.left + 17, cy,
                            M::kIconSideBar, M::kIconStroke);
            }
            text(items[s][i], { row.left + 33, row.top, row.right - 9, row.bottom },
                 lerpColor(theme_.ink, theme_.accent, on),
                 M::kBody.size, M::kBody.trackingEm,
                 current ? M::kTitle.weight : M::kBody.weight);
            y += setRowHeight_;
        }
        y += 11;
    }
}

void AppWindow::paintToolBar(const D2D1_RECT_F& r) {
    // Glass over the content: a low-alpha wash plus the 1px specular top edge.
    auto wash = theme_.content; wash.a = setBlur_ / 100.0f;
    fill(r, wash);
    hairline(r.left, r.top, r.right, r.top + dips(1), theme_.spec);
    // The toolbar names the folder being browsed, never the selected file.
    const auto* deepest = model().deepestListed();
    const std::wstring where = deepest ? deepest->place.label : L"";
    // With the sidebar collapsed the toolbar starts at the window edge, which is
    // exactly where the nav buttons live. Start the title after them.
    const float titleLeft = std::max(r.left + 12, navButton(2).right + 14);
    text(where.c_str(), { titleLeft, r.top, r.right - 180, r.bottom },
         theme_.ink, M::kHead.size, M::kHead.trackingEm, M::kHead.weight);

    // The sort / share / more group, held in its own capsule.
    {
        const auto a = capsuleButton(0), c = capsuleButton(2);
        const auto cap = stadium({ a.left - 2, a.top - 2, c.right + 2, c.bottom + 2 });
        brush_->SetColor(theme_.selSoft);
        dc_->FillRoundedRectangle(cap, brush_.Get());
        const Icon ics[3] = { Icon::Sort, Icon::Share, Icon::More };
        for (int i = 0; i < 3; ++i) {
            const auto b = capsuleButton(i);
            const float g = fades_.at(fkey(Fx::Capsule, i), hotChrome_ == HotCapsule + i,
                                      M::kMsChrome);
            if (g > 0.002f) {
                auto h = theme_.hover; h.a *= g * 2.0f;
                brush_->SetColor(h);
                dc_->FillRoundedRectangle(stadium(b), brush_.Get());
            }
            brush_->SetColor(lerpColor(theme_.ink2, theme_.ink, g));
            icons_.draw(dc_.Get(), brush_.Get(), ics[i],
                     (b.left + b.right) * 0.5f, (b.top + b.bottom) * 0.5f, M::kIconSide, M::kIconStroke);
        }
    }

    // Search, as a capsule of its own. Typing filters the folder in view.
    {
        const auto s = searchRect();
        const float g = fades_.at(fkey(Fx::Search, 0),
                                  hotChrome_ == HotSearch || searchOn_, M::kMsChrome);
        auto bg = theme_.selSoft; bg.a += 0.05f * g;
        brush_->SetColor(bg);
        dc_->FillRoundedRectangle(stadium(s), brush_.Get());
        if (search_.focused) {
            auto edge = theme_.accent; edge.a = 0.9f;
            brush_->SetColor(edge);
            dc_->DrawRoundedRectangle(stadium(s), brush_.Get(), dips(2));
        }
        const auto ink = lerpColor(theme_.ink2, theme_.ink, g);
        brush_->SetColor(ink);
        icons_.draw(dc_.Get(), brush_.Get(), Icon::Search, s.left + 15,
                 (s.top + s.bottom) * 0.5f, 15, M::kIconStroke);

        const D2D1_RECT_F inner{ s.left + 28, s.top, s.right - 10, s.bottom };
        dc_->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_ALIASED);
        if (search_.text.empty() && !search_.focused) {
            text(T(L"検索"), inner, ink, M::kTab.size, M::kTab.trackingEm, M::kTab.weight);
        } else {
            if (search_.hasSelection()) {
                const float x0 = inner.left + textWidth(search_.text.c_str(), M::kTab.size,
                                    M::kTab.trackingEm, M::kTab.weight, static_cast<int>(search_.selLo()));
                const float x1 = inner.left + textWidth(search_.text.c_str(), M::kTab.size,
                                    M::kTab.trackingEm, M::kTab.weight, static_cast<int>(search_.selHi()));
                auto sc = theme_.accent; sc.a = 0.28f;
                brush_->SetColor(sc);
                dc_->FillRectangle({ x0, s.top + 5, x1, s.bottom - 5 }, brush_.Get());
            }
            text(search_.text.c_str(), inner, theme_.ink,
                 M::kTab.size, M::kTab.trackingEm, M::kTab.weight);
            if (search_.focused && ((GetTickCount64() / 530) & 1) == 0) {
                const float cx = inner.left + textWidth(search_.text.c_str(), M::kTab.size,
                                    M::kTab.trackingEm, M::kTab.weight, static_cast<int>(search_.caret));
                brush_->SetColor(theme_.ink);
                dc_->FillRectangle({ cx, s.top + 5, cx + dips(2), s.bottom - 5 }, brush_.Get());
            }
        }
        dc_->PopAxisAlignedClip();
    }

    // the gear that opens the inspector
    {
        const auto s = switcherButton(0);
        const float cx = s.left - 24, cy = (s.top + s.bottom) * 0.5f;
        const float g = fades_.at(fkey(Fx::Gear, 0), hotChrome_ == HotGear, M::kMsChrome);
        if (g > 0.002f) {
            auto h = theme_.hover; h.a *= g * 2.0f;
            brush_->SetColor(h);
            dc_->FillRoundedRectangle(stadium({ cx - 14, cy - 14, cx + 14, cy + 14 }),
                                      brush_.Get());
        }
        brush_->SetColor(inspOpen_ ? theme_.accent : lerpColor(theme_.ink2, theme_.ink, g));
        icons_.draw(dc_.Get(), brush_.Get(), Icon::Gear, cx, cy, M::kIconSideBar, M::kIconStroke);
    }
    paintViewSwitcher(r);
}

D2D1_RECT_F AppWindow::searchRect() const {
    const auto s = switcherButton(0);
    const float right = s.left - 44;
    const float cy = (s.top + s.bottom) * 0.5f;
    return { right - 146, cy - 13, right, cy + 13 };
}

D2D1_RECT_F AppWindow::capsuleButton(int i) const {
    const auto s = searchRect();
    const float w = 30, h = 24;
    const float left = s.left - 12 - 3 * w + i * w;
    const float cy = (s.top + s.bottom) * 0.5f;
    return { left, cy - h * 0.5f, left + w, cy + h * 0.5f };
}

// ── view switcher ──────────────────────────────────────────────────
D2D1_RECT_F AppWindow::switcherButton(int i) const {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    // The toolbar ends where the inspector begins, so these ride with it.
    const float right = dips(rc.right - rc.left) - inspectorWidth() - 12;
    const float w = 32, h = 24;
    const float left = right - 3 * w - 4 + i * w;
    const float top = M::kTitleBar + (M::kToolBar - h) * 0.5f;
    return { left, top, left + w, top + h };
}

void AppWindow::paintViewSwitcher(const D2D1_RECT_F&) {
    const auto a = switcherButton(0), c = switcherButton(2);
    const auto bg = stadium({ a.left - 2, a.top - 2, c.right + 2, c.bottom + 2 });
    brush_->SetColor(theme_.selSoft);
    dc_->FillRoundedRectangle(bg, brush_.Get());

    // The indicator slides rather than jumping: same curve as everything else.
    const float t = viewThumb_.value();
    const auto tr = stadium({ a.left + t * 32, a.top, a.left + t * 32 + 32, a.bottom });
    brush_->SetColor(theme_.accent);
    dc_->FillRoundedRectangle(tr, brush_.Get());

    const Icon ics[3] = { Icon::ViewColumn, Icon::ViewList, Icon::ViewIcon };
    for (int i = 0; i < 3; ++i) {
        const auto b = switcherButton(i);
        // How far the indicator has arrived under this button, so the glyph
        // turns white in step with the accent sliding beneath it rather than
        // flipping when it crosses the halfway mark.
        const float cover = std::max(0.0f, 1.0f - std::fabs(t - i));
        const float g = fades_.at(fkey(Fx::Switch, i), hotChrome_ == HotSwitch + i,
                                  M::kMsChrome);
        if (cover < 0.5f && g > 0.002f) {
            auto h = theme_.hover; h.a *= g * 2.0f;
            brush_->SetColor(h);
            dc_->FillRoundedRectangle(stadium(b), brush_.Get());
        }
        brush_->SetColor(lerpColor(lerpColor(theme_.ink2, theme_.ink, g),
                                   rgba(255, 255, 255), cover));
        icons_.draw(dc_.Get(), brush_.Get(), ics[i],
                    (b.left + b.right) * 0.5f, (b.top + b.bottom) * 0.5f, M::kIconSide, M::kIconStroke);
    }
}

// Fixed at the window's left edge, so they ride on the sidebar when it is open
// and on the toolbar when it is not.
D2D1_RECT_F AppWindow::navButton(int i) const {
    const float cx = 22 + i * 30;
    const float cy = M::kTitleBar + M::kToolBar * 0.5f;
    return { cx - 13, cy - 13, cx + 13, cy + 13 };
}

void AppWindow::paintNavButtons() {
    const Icon icons[3] = { Icon::SidebarToggle, Icon::ChevronLeft, Icon::ChevronRight };
    const bool live[3] = { true, canGoBack(), canGoForward() };
    for (int i = 0; i < 3; ++i) {
        const auto b = navButton(i);
        const float cx = (b.left + b.right) * 0.5f, cy = (b.top + b.bottom) * 0.5f;
        const float g = fades_.at(fkey(Fx::SideBtn, i),
                                  live[i] && hotChrome_ == HotSideBtn + i, M::kMsChrome);
        if (g > 0.002f) {
            auto h = theme_.hover; h.a *= g * 1.6f;   // the glass swallows a plain wash
            brush_->SetColor(h);
            dc_->FillRoundedRectangle(stadium(b), brush_.Get());
        }
        // A dead arrow says "nowhere to go" better than a missing one would.
        auto c = theme_.ink;
        c.a = live[i] ? (0.68f + 0.32f * g) : 0.25f;
        brush_->SetColor(c);
        icons_.draw(dc_.Get(), brush_.Get(), icons[i], cx, cy,
                    M::kIconSideBar, M::kIconStroke);
    }
}

// One place that decides what the pointer is over, so the hit test and the
// paint can never light different things.
int AppWindow::chromeHit(float x, float y) const {
    const auto sw0 = switcherButton(0);
    const float cy = (sw0.top + sw0.bottom) * 0.5f;
    auto in = [&](const D2D1_RECT_F& b) {
        return x >= b.left && x <= b.right && y >= b.top && y <= b.bottom;
    };
    auto onDot = [&](float cx, float ccy, float rad) {
        return std::fabs(x - cx) <= rad && std::fabs(y - ccy) <= rad;
    };

    if (y >= M::kTitleBar && y < M::kTitleBar + M::kToolBar) {
        // These are tested first and unconditionally: they sit at the window's
        // left edge whether or not the sidebar is there to sit on.
        for (int i = 0; i < 3; ++i) if (in(navButton(i))) return HotSideBtn + i;
        if (x <= sideWidth()) return HotNone;
        for (int i = 0; i < 3; ++i) if (in(switcherButton(i))) return HotSwitch + i;
        if (onDot(sw0.left - 24, cy, 14)) return HotGear;
        if (in(searchRect())) return HotSearch;
        for (int i = 0; i < 3; ++i) if (in(capsuleButton(i))) return HotCapsule + i;
        return HotNone;
    }
    if (!picker_ && y < M::kTitleBar && !tabs_.empty()) {
        const auto last = tabRect(static_cast<int>(tabs_.size()) - 1);
        if (onDot(last.right + 18, (last.top + last.bottom) * 0.5f, 14)) return HotNewTab;
    }
    return HotNone;
}

// ── sidebar ────────────────────────────────────────────────────────
namespace {
// index -> where it goes. The entries with no real backing (recents, tags)
// are deliberately absent rather than pretending to navigate somewhere.
Place sidebarPlace(int flat) {
    switch (flat) {
    case 0: { auto p = knownFolder(FOLDERID_Recent);    return p.empty() ? Place{} : Place::directory(p); }
    case 1: { auto p = knownFolder(FOLDERID_Desktop);   return p.empty() ? Place{} : Place::directory(p); }
    case 2: { auto p = documentsFolder();               return p.empty() ? Place{} : Place::directory(p); }
    case 3: { auto p = knownFolder(FOLDERID_Downloads); return p.empty() ? Place{} : Place::directory(p); }
    case 4: { auto p = profileFolder();
              return p.empty() ? Place{} : Place::directory(p + L"\\Projects"); }
    case 5:   return Place::thisPC();
    case 6:   return Place::directory(L"C:\\");
    case 7: { auto p = profileFolder();
              return p.empty() ? Place{} : Place::directory(p + L"\\OneDrive"); }
    default:  return Place{};
    }
}
constexpr int kSideCounts[3] = { 5, 3, 4 };
}

float AppWindow::sidebarPillY(int index) const {
    float y = M::kTitleBar + M::kToolBar;
    int flat = 0;
    for (int s = 0; s < 3; ++s) {
        y += 22;
        for (int i = 0; i < kSideCounts[s]; ++i, ++flat) {
            if (flat == index) return y;
            y += setRowHeight_;
        }
        y += 11;
    }
    return y;
}

void AppWindow::sidebarClick(float x, float y) {
    if (x > sideWidth()) return;
    float ry = M::kTitleBar + M::kToolBar;
    int flat = 0;
    for (int s = 0; s < 3; ++s) {
        ry += 22;
        for (int i = 0; i < kSideCounts[s]; ++i, ++flat) {
            if (y >= ry && y < ry + setRowHeight_) {
                // The tag section is the third one: those rows filter rather
                // than navigate, so they get a folder of their own making.
                if (s == 2) {
                    const int tag = i;
                    tagFilter_ = (tagFilter_ == tag) ? -1 : tag;
                    sideSelected_ = flat;
                    sidePill_.to(sidebarPillY(flat), M::kMsPill);
                    showTagFolder(tag);
                    tick();
                    return;
                }
                const Place p = sidebarPlace(flat);
                if (p.kind == PlaceKind::Directory && p.path.empty()) return;  // nothing there
                tagFilter_ = -1;
                sideSelected_ = flat;
                sidePill_.to(sidebarPillY(flat), M::kMsPill);
                // Through navigate(), so the jump is recorded: back and forward
                // did nothing after a sidebar click because this went straight
                // to the model.
                navigate(p);
                return;
            }
            ry += setRowHeight_;
        }
        ry += 11;
    }
}

// A tag view is a folder we assemble: the rows live all over the disk, so each
// one carries its own path rather than being found relative to a directory.
Listing AppWindow::buildTagListing(int tag) const {
    Listing listing;
    for (const auto& path : tags_.pathsWith(tag)) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) continue;
        Entry e;
        const size_t slash = path.find_last_of(L'\\');
        e.name   = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
        e.source = path;
        e.attrs  = fad.dwFileAttributes;
        e.isDir  = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size   = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        e.written = fad.ftLastWriteTime;
        listing.entries.push_back(std::move(e));
    }
    return listing;
}

void AppWindow::showTagFolder(int tag) {
    navigate(Place::forTag(tag, TagStore::name(tag)));
}

// ── commands ───────────────────────────────────────────────────────
// One vocabulary, reachable from the toolbar, the right-click and the keyboard.
// Ids sit well above anything the shell menu uses so the two can share a popup.
namespace cmd {
enum : int {
    First = 0x3000,
    Open = First, OpenWith, Rename, Duplicate, CopyItem, CutItem, Paste,
    CopyPath, CopyName, NewFolder, Trash, Shortcut, Share, Properties,
    Reveal, Info, Refresh, ToggleHidden, SelectAll,
    SortName, SortModified, SortSize, SortKind, SortAscending, SortDescending,
    Last
};
} // namespace cmd

std::wstring AppWindow::targetPath() const {
    return model().selectedFullPath();
}

// Commands act on everything chosen, not just the lead.
std::vector<std::wstring> AppWindow::targetPaths() const {
    return model().selectedPaths();
}

std::wstring AppWindow::targetFolder() const {
    const auto* c = model().deepestListed();
    if (c && c->place.kind == PlaceKind::Directory && !c->place.path.empty())
        return c->place.path;
    return {};
}

void AppWindow::refreshVisible() {
    model().refreshAll(hwnd_);
    tick();
}

void AppWindow::runCommand(int id) {
    const std::wstring path = targetPath();
    const std::wstring folder = targetFolder();
    const auto paths = targetPaths();
    logf("command 0x%04X on '%ls' in '%ls'", id, path.c_str(), folder.c_str());

    switch (id) {
    case cmd::Open:       openSelected(); break;
    case cmd::OpenWith:   if (!path.empty()) act::openWith(hwnd_, path); break;
    case cmd::Rename:     beginRename(); break;
    case cmd::Duplicate:  if (!path.empty() && act::duplicate(hwnd_, path)) refreshVisible(); break;
    case cmd::CopyItem:   if (!paths.empty()) act::copyToClipboard(hwnd_, paths, false); break;
    case cmd::CutItem:    if (!paths.empty()) act::copyToClipboard(hwnd_, paths, true); break;
    case cmd::Paste:      if (!folder.empty() && act::pasteInto(hwnd_, folder)) refreshVisible(); break;
    case cmd::CopyPath: {
        if (paths.empty()) break;
        std::wstring all;
        for (const auto& p : paths) { if (!all.empty()) all += L"\r\n"; all += p; }
        act::copyTextToClipboard(hwnd_, all);
        break;
    }
    case cmd::CopyName: {
        if (paths.empty()) break;
        std::wstring all;
        for (const auto& p : paths) {
            const size_t slash = p.find_last_of(L'\\');
            if (!all.empty()) all += L"\r\n";
            all += (slash == std::wstring::npos) ? p : p.substr(slash + 1);
        }
        act::copyTextToClipboard(hwnd_, all);
        break;
    }
    case cmd::NewFolder: {
        if (folder.empty()) break;
        const std::wstring made = act::newFolder(hwnd_, folder);
        if (made.empty()) break;
        // Refresh, then let the user name it: a folder called "新規フォルダ"
        // that you have to find and rename separately is half a feature.
        model().refreshAll(hwnd_);
        pendingRenameName_ = made;
        tick();
        break;
    }
    case cmd::Trash:
        if (!paths.empty() && act::moveToRecycleBin(hwnd_, paths, setConfirmDelete_))
            refreshVisible();
        break;
    case cmd::SelectAll: {
        auto& cols = model().columnsMut();
        if (cols.empty()) break;
        size_t d = cols.size() - 1;
        while (d > 0 && cols[d].listing.entries.empty()) --d;
        const auto rows = visibleRows(cols[d]);
        if (rows.empty()) break;
        model().setSelection(hwnd_, d, rows, rows.back());
        tick();
        break;
    }
    case cmd::Shortcut:   if (!path.empty() && act::createShortcut(hwnd_, path)) refreshVisible(); break;
    case cmd::Share:      if (!path.empty()) act::share(hwnd_, path); break;
    case cmd::Properties: if (!path.empty()) act::showProperties(hwnd_, path); break;
    case cmd::Reveal:     if (!path.empty()) act::revealInExplorer(hwnd_, path); break;
    case cmd::Info:       toggleInspector(); break;
    case cmd::Refresh:    refreshVisible(); break;
    case cmd::ToggleHidden:
        model().setShowHidden(hwnd_, !model().showHidden());
        settingsChanged();
        tick();
        break;
    case cmd::SortName:       model().setSort(SortKey::Name,     model().sortDescending()); settingsChanged(); tick(); break;
    case cmd::SortModified:   model().setSort(SortKey::Modified, model().sortDescending()); settingsChanged(); tick(); break;
    case cmd::SortSize:       model().setSort(SortKey::Size,     model().sortDescending()); settingsChanged(); tick(); break;
    case cmd::SortKind:       model().setSort(SortKey::Kind,     model().sortDescending()); settingsChanged(); tick(); break;
    case cmd::SortAscending:  model().setSort(model().sortKey(), false); settingsChanged(); tick(); break;
    case cmd::SortDescending: model().setSort(model().sortKey(), true);  settingsChanged(); tick(); break;
    default: break;
    }
}

namespace {
// A popup that matches the rest of the window: the shell's own menus are the
// one place we cannot restyle, so ours at least keep the same wording.
void addItem(HMENU m, int id, const wchar_t* label, bool enabled = true, bool checked = false) {
    MENUITEMINFOW mi{ sizeof(mi) };
    mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
    mi.wID = static_cast<UINT>(id);
    mi.dwTypeData = const_cast<wchar_t*>(label);
    mi.fState = (enabled ? MFS_ENABLED : MFS_DISABLED) | (checked ? MFS_CHECKED : 0u);
    InsertMenuItemW(m, GetMenuItemCount(m), TRUE, &mi);
}
void addSeparator(HMENU m) {
    MENUITEMINFOW mi{ sizeof(mi) };
    mi.fMask = MIIM_FTYPE;
    mi.fType = MFT_SEPARATOR;
    InsertMenuItemW(m, GetMenuItemCount(m), TRUE, &mi);
}
} // namespace

void AppWindow::showSortMenu() {
    HMENU m = CreatePopupMenu();
    if (!m) return;
    const SortKey k = model().sortKey();
    addItem(m, cmd::SortName,     T(L"名前"),   true, k == SortKey::Name);
    addItem(m, cmd::SortModified, T(L"変更日"), true, k == SortKey::Modified);
    addItem(m, cmd::SortSize,     T(L"サイズ"), true, k == SortKey::Size);
    addItem(m, cmd::SortKind,     T(L"種類"),   true, k == SortKey::Kind);
    addSeparator(m);
    addItem(m, cmd::SortAscending,  T(L"昇順"), true, !model().sortDescending());
    addItem(m, cmd::SortDescending, T(L"降順"), true,  model().sortDescending());

    const auto b = capsuleButton(0);
    POINT pt{ static_cast<LONG>(px(b.left)), static_cast<LONG>(px(b.bottom + 4)) };
    ClientToScreen(hwnd_, &pt);
    const int chosen = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                        pt.x, pt.y, hwnd_, nullptr);
    DestroyMenu(m);
    if (chosen) runCommand(chosen);
}

// The menu for the folder itself, shown when the click landed past the rows.
void AppWindow::showFolderMenu(POINT screenPt) {
    HMENU m = CreatePopupMenu();
    if (!m) return;
    logf("folder menu for '%ls'", targetFolder().c_str());
    const bool inFolder = !targetFolder().empty();
    bool cut = false;
    const bool clip = !act::clipboardPaths(hwnd_, &cut).empty();

    addItem(m, cmd::NewFolder, T(L"新規フォルダ\tCtrl+Shift+N"), inFolder);
    addItem(m, cmd::Paste,     T(L"貼り付け\tCtrl+V"),           inFolder && clip);
    addSeparator(m);
    const SortKey k = model().sortKey();
    addItem(m, cmd::SortName,     T(L"名前で並べ替え"),   true, k == SortKey::Name);
    addItem(m, cmd::SortModified, T(L"変更日で並べ替え"), true, k == SortKey::Modified);
    addItem(m, cmd::SortSize,     T(L"サイズで並べ替え"), true, k == SortKey::Size);
    addItem(m, cmd::SortKind,     T(L"種類で並べ替え"),   true, k == SortKey::Kind);
    addSeparator(m);
    addItem(m, cmd::ToggleHidden, T(L"隠しファイルを表示\tCtrl+H"), true, model().showHidden());
    addItem(m, cmd::Refresh,      T(L"再読み込み\tF5"));
    addSeparator(m);
    addItem(m, cmd::Info,         T(L"情報を見る\tCtrl+I"), true, inspOpen_);

    const int chosen = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON |
                                           TPM_LEFTALIGN | TPM_TOPALIGN,
                                        screenPt.x, screenPt.y, hwnd_, nullptr);
    DestroyMenu(m);
    if (chosen) runCommand(chosen);
}

void AppWindow::showMoreMenu() {
    HMENU m = CreatePopupMenu();
    if (!m) return;
    const bool has = !targetPath().empty();
    const bool inFolder = !targetFolder().empty();
    bool cut = false;
    const bool clip = !act::clipboardPaths(hwnd_, &cut).empty();

    addItem(m, cmd::Open,      T(L"開く"),                 has);
    addItem(m, cmd::OpenWith,  T(L"このアプリケーションで開く…"), has);
    addSeparator(m);
    addItem(m, cmd::NewFolder, T(L"新規フォルダ\tCtrl+Shift+N"), inFolder);
    addItem(m, cmd::Rename,    T(L"名前を変更\tF2"),        has);
    addItem(m, cmd::Duplicate, T(L"複製\tCtrl+D"),          has);
    addItem(m, cmd::Shortcut,  T(L"ショートカットを作成"),   has);
    addSeparator(m);
    addItem(m, cmd::CopyItem,  T(L"コピー\tCtrl+C"),        has);
    addItem(m, cmd::CutItem,   T(L"切り取り\tCtrl+X"),      has);
    addItem(m, cmd::Paste,     T(L"貼り付け\tCtrl+V"),      inFolder && clip);
    addItem(m, cmd::CopyPath,  T(L"パスをコピー\tCtrl+Shift+C"), has);
    addItem(m, cmd::CopyName,  T(L"名前をコピー"),           has);
    addSeparator(m);
    addItem(m, cmd::Trash,     T(L"ごみ箱に入れる\tDelete"), has);
    addSeparator(m);
    addItem(m, cmd::Info,      T(L"情報を見る\tCtrl+I"),    true, inspOpen_);
    addItem(m, cmd::Properties,T(L"プロパティ\tAlt+Enter"), has);
    addItem(m, cmd::Reveal,    T(L"エクスプローラーで表示"), has);
    addSeparator(m);
    addItem(m, cmd::ToggleHidden, T(L"隠しファイルを表示"), true, model().showHidden());
    addItem(m, cmd::Refresh,   T(L"再読み込み\tF5"));

    const auto b = capsuleButton(2);
    POINT pt{ static_cast<LONG>(px(b.left)), static_cast<LONG>(px(b.bottom + 4)) };
    ClientToScreen(hwnd_, &pt);
    const int chosen = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                        pt.x, pt.y, hwnd_, nullptr);
    DestroyMenu(m);
    if (chosen) runCommand(chosen);
}

// ── navigation ─────────────────────────────────────────────────────
void AppWindow::pushHistory(const Place& p) {
    if (!history_.empty() && historyAt_ < history_.size() && history_[historyAt_] == p)
        return;                                   // already standing there
    // Anything ahead of the cursor is a branch the user just left.
    if (historyAt_ + 1 < history_.size())
        history_.resize(historyAt_ + 1);
    history_.push_back(p);
    historyAt_ = history_.size() - 1;
    logf("history: %zu entries, at %zu ('%ls')", history_.size(), historyAt_,
         p.label.c_str());
    if (history_.size() > 128) {                  // bounded; the far past is noise
        history_.erase(history_.begin());
        --historyAt_;
    }
}

void AppWindow::navigate(const Place& p, bool record) {
    if (record) pushHistory(p);
    // A tag view has no directory behind it, so it is rebuilt here rather than
    // enumerated. Going back to one has to work the same as arriving at it.
    if (p.kind == PlaceKind::Tag) {
        tagFilter_ = p.tag;
        model().setRootListing(p, buildTagListing(p.tag));
    } else {
        tagFilter_ = -1;
        model().setRoot(hwnd_, p);
    }
    scrollX_ = 0;
    flatSelected_ = -1;
    flatScrollY_ = 0;
    tick();
}

void AppWindow::goBack() {
    if (!canGoBack()) return;
    --historyAt_;
    navigate(history_[historyAt_], false);
}

void AppWindow::goForward() {
    if (!canGoForward()) return;
    ++historyAt_;
    navigate(history_[historyAt_], false);
}

void AppWindow::toggleSidebar() {
    sideOpen_ = !sideOpen_;
    sideW_.to(sideOpen_ ? M::kSideBar : 0.0f, M::kMsPanel);
    settingsChanged();
    tick();
}

// ── settings ───────────────────────────────────────────────────────
namespace {
// The six swatches the panel offers, so a saved choice can be resolved back.
const D2D1_COLOR_F kAccents[6] = {
    rgba(0x00,0x78,0xD4), rgba(0x2F,0x6F,0xEB), rgba(0x3A,0x8B,0x5C),
    rgba(0x8B,0x5C,0xB8), rgba(0xB8,0x5C,0x38), rgba(0x5A,0x65,0x70) };
} // namespace

void AppWindow::applyTheme() {
    const bool dark = (setThemeMode_ == 0) ? systemUsesDark() : (setThemeMode_ == 2);
    const D2D1_COLOR_F accent =
        (setAccent_ >= 0 && setAccent_ < 6) ? kAccents[setAccent_] : systemAccent();
    theme_ = dark ? darkTheme(accent) : lightTheme(accent);
    if (hwnd_) {
        BOOL d = dark;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
    }
}

void AppWindow::loadSettings() {
    settings_.load();
    setThemeMode_    = settings_.getInt  (L"appearance.mode",    setThemeMode_);
    setAccent_       = settings_.getInt  (L"appearance.accent",  setAccent_);
    setBlur_         = settings_.getFloat(L"appearance.wash",    setBlur_);
    setRowHeight_    = settings_.getFloat(L"view.rowHeight",     setRowHeight_);
    setColumnW_      = settings_.getFloat(L"view.columnWidth",   setColumnW_);
    setPreview_      = settings_.getBool (L"view.preview",       setPreview_);
    setAppIcons_     = settings_.getBool (L"view.appIcons",      setAppIcons_);
    setPrebuildMenu_ = settings_.getBool (L"view.prebuildMenu",  setPrebuildMenu_);
    setOrielKeys_    = settings_.getBool (L"keys.orielScheme",   setOrielKeys_);
    setConfirmDelete_= settings_.getBool (L"general.confirmDelete", setConfirmDelete_);
    view_            = static_cast<View>(std::clamp(settings_.getInt(L"view.mode", 0), 0, 2));
    viewThumb_.set(static_cast<float>(static_cast<int>(view_)));
    model().setSort(static_cast<SortKey>(std::clamp(settings_.getInt(L"view.sortKey", 0), 0, 3)),
                    settings_.getBool(L"view.sortDesc", false));
    // Clamped, not trusted: a hand-edited file must not be able to produce a
    // window with 2px rows that cannot be used to fix the setting.
    setRowHeight_ = std::clamp(setRowHeight_, 20.0f, 36.0f);
    setColumnW_   = std::clamp(setColumnW_,  150.0f, 300.0f);
    setBlur_      = std::clamp(setBlur_,       0.0f, 100.0f);
}

void AppWindow::saveSettings() {
    settings_.set(L"appearance.mode",       setThemeMode_);
    settings_.set(L"appearance.accent",     setAccent_);
    settings_.set(L"appearance.wash",       setBlur_);
    settings_.set(L"view.rowHeight",        setRowHeight_);
    settings_.set(L"view.columnWidth",      setColumnW_);
    settings_.set(L"view.preview",          setPreview_);
    settings_.set(L"view.appIcons",         setAppIcons_);
    settings_.set(L"view.prebuildMenu",     setPrebuildMenu_);
    settings_.set(L"view.mode",             static_cast<int>(view_));
    settings_.set(L"view.sidebar",          sideOpen_);
    settings_.set(L"view.sortKey",          static_cast<int>(model().sortKey()));
    settings_.set(L"view.sortDesc",         model().sortDescending());
    settings_.set(L"keys.orielScheme",      setOrielKeys_);
    settings_.set(L"general.confirmDelete", setConfirmDelete_);
    settings_.set(L"general.showHidden",    model().showHidden());

    // Where to come back to. The deepest folder actually listed, not the
    // selection, so reopening lands where the eye was.
    if (const auto* c = model().deepestListed(); c && c->place.kind == PlaceKind::Directory)
        settings_.set(L"session.folder", c->place.path);
    settings_.set(L"session.sidebarRow", sideSelected_);

    if (hwnd_) {
        WINDOWPLACEMENT wp{ sizeof(wp) };
        if (GetWindowPlacement(hwnd_, &wp)) {
            settings_.set(L"window.x",   static_cast<int>(wp.rcNormalPosition.left));
            settings_.set(L"window.y",   static_cast<int>(wp.rcNormalPosition.top));
            settings_.set(L"window.w",   static_cast<int>(wp.rcNormalPosition.right - wp.rcNormalPosition.left));
            settings_.set(L"window.h",   static_cast<int>(wp.rcNormalPosition.bottom - wp.rcNormalPosition.top));
            settings_.set(L"window.max", wp.showCmd == SW_SHOWMAXIMIZED);
        }
    }
    settings_.save();
    settingsDirty_ = false;
}

void AppWindow::settingsChanged() {
    if (picker_) return;             // a picker is transient; it owns nothing
    settingsDirty_ = true;
    // Dragging a slider changes a value every frame. Write once it settles.
    SetTimer(hwnd_, 4, 1200, nullptr);
}

// ── rename and open ────────────────────────────────────────────────
void AppWindow::beginRename() {
    const auto& cols = model().columns();
    for (size_t d = cols.size(); d-- > 0; ) {
        const auto& c = cols[d];
        if (c.selected < 0 || c.selected >= static_cast<int>(c.listing.entries.size()))
            continue;
        renaming_ = true;
        renameDepth_ = d;
        renameIndex_ = c.selected;
        renameField_.set(c.listing.entries[c.selected].name);
        // Select the stem, not the extension: renaming almost never means
        // changing the type, and re-typing ".blend" every time is a tax.
        const std::wstring& n = renameField_.text;
        const size_t dot = n.find_last_of(L'.');
        renameField_.anchor = 0;
        renameField_.caret = (dot == std::wstring::npos || dot == 0) ? n.size() : dot;
        renameField_.focused = true;
        tick();
        return;
    }
}

void AppWindow::commitRename() {
    if (!renaming_) { return; }
    const auto& cols = model().columns();
    std::wstring want = renameField_.text;
    renaming_ = false;
    renameField_.focused = false;
    if (renameDepth_ >= cols.size() || want.empty()) { tick(); return; }
    const auto& c = cols[renameDepth_];
    if (renameIndex_ < 0 || renameIndex_ >= static_cast<int>(c.listing.entries.size())) {
        tick(); return;
    }
    const Entry& e = c.listing.entries[renameIndex_];
    if (want == e.name) { tick(); return; }
    // Anything that would change where the file lives is a move, not a rename.
    if (want.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
        logf("rename refused, illegal characters: %ls", want.c_str());
        tick();
        return;
    }
    const std::wstring from = childOf(c.place, e).path;
    const size_t slash = from.find_last_of(L'\\');
    if (slash == std::wstring::npos) { tick(); return; }
    const std::wstring to = from.substr(0, slash + 1) + want;
    if (!MoveFileExW(from.c_str(), to.c_str(), 0))
        logf("rename failed %lu: %ls -> %ls", GetLastError(), from.c_str(), to.c_str());
    else
        model().refresh(hwnd_, renameDepth_);
    tick();
}

// Hand the item to whatever owns it. Never our business to decide how.
void AppWindow::openSelected() {
    const std::wstring path = model().selectedFullPath();
    if (path.empty()) return;
    // Opening a folder means going into it. Handing it to the shell would
    // launch a second file manager, which is a strange thing for a file manager
    // to do.
    if (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) {
        model().expandSelected(hwnd_);
        scrollColumnsToEnd(contentArea().right - contentArea().left);
        tick();
        return;
    }
    SHELLEXECUTEINFOW ei{};
    ei.cbSize = sizeof(ei);
    ei.fMask  = SEE_MASK_ASYNCOK | SEE_MASK_FLAG_NO_UI;
    ei.hwnd   = hwnd_;
    ei.lpVerb = nullptr;              // the type's own default action
    ei.lpFile = path.c_str();
    ei.nShow  = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&ei)) logf("open failed %lu: %ls", GetLastError(), path.c_str());
}

void AppWindow::setHover(size_t depth, int index) {
    if (depth == hoverDepth_ && index == hoverIndex_) return;
    hoverDepth_ = depth;
    hoverIndex_ = index;
    tick();     // fades_ works out which row lights and which lets go
}

// ── inspector ──────────────────────────────────────────────────────
void AppWindow::toggleInspector() {
    inspOpen_ = !inspOpen_;
    RECT wr{}; GetWindowRect(hwnd_, &wr);
    if (!inspOpen_) inspBaseW_ = (wr.right - wr.left) - static_cast<int>(px(M::kInspector));
    else            inspBaseW_ = wr.right - wr.left;
    inspW_.to(inspOpen_ ? M::kInspector : 0.0f, M::kMsPanel);
    // Contents come up after the frame has started moving, and leave at once
    // when it closes: text sliding out with the edge reads as a glitch.
    if (inspOpen_) { inspInk_.set(0); SetTimer(hwnd_, 3, M::kMsInkWait, nullptr); }
    else           inspInk_.to(0.0f, M::kMsInk);
    tick();
}

void AppWindow::paintInspector(const D2D1_RECT_F& r) {
    const float w = r.right - r.left;
    if (w < 2) return;

    // Glass, like the sidebar: the panel belongs to the window chrome, not to
    // the content surface.
    auto glass = theme_.chrome; glass.a = theme_.dark ? 0.54f : 0.60f;
    fill(r, glass);
    hairline(r.left, r.top, r.left + dips(1), r.bottom, theme_.hair);

    // Contents keep a fixed width and are clipped, so nothing squashes while
    // the panel is still opening, and fade up as a body rather than arriving
    // with the edge.
    const float x0 = r.right - M::kInspector;
    dc_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
    const float ink = inspInk_.value();
    if (ink < 0.999f)
        dc_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                             D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                             D2D1::IdentityMatrix(), ink),
                       nullptr);

    float y = r.top + 12;
    text(T(L"設定"), { x0 + 16, y, r.right - 44, y + 24 }, theme_.ink,
         M::kHead.size, M::kHead.trackingEm, M::kHead.weight);
    y += 32;

    const wchar_t* tabs[4] = { T(L"一般"), T(L"外観"), T(L"表示"), T(L"キー操作") };
    const float tw = (M::kInspector - 28) / 4.0f;
    D2D1_ROUNDED_RECT strip{ { x0 + 14, y, r.right - 14, y + 28 }, 8, 8 };
    brush_->SetColor(theme_.selSoft);
    dc_->FillRoundedRectangle(strip, brush_.Get());
    for (int i = 0; i < 4; ++i) {
        const D2D1_RECT_F b{ x0 + 14 + i * tw, y, x0 + 14 + (i + 1) * tw, y + 28 };
        const float g = fades_.at(fkey(Fx::InspTab, i), i == inspTab_, M::kMsChrome);
        if (g > 0.002f) {
            D2D1_ROUNDED_RECT sel{ { b.left + 2, b.top + 2, b.right - 2, b.bottom - 2 }, 6, 6 };
            auto c = theme_.content; c.a *= g;
            brush_->SetColor(c);
            dc_->FillRoundedRectangle(sel, brush_.Get());
        }
        text(tabs[i], b, lerpColor(theme_.ink2, theme_.ink, g),
             M::kCap.size, M::kCap.trackingEm,
             i == inspTab_ ? M::kTitle.weight : M::kCap.weight,
             DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    y += 28 + 24;

    inspSliders_.clear();
    auto label = [&](const wchar_t* s) {
        text(s, { x0 + 16, y, x0 + 150, y + 22 }, theme_.ink2,
             M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight);
    };
    auto slider = [&](const wchar_t* name, float* v, float lo, float hi, const wchar_t* unit) {
        label(name);
        const float sx = x0 + 150, sw = M::kInspector - 150 - 66;
        const float cy = y + 11;
        brush_->SetColor(theme_.selIdle);
        D2D1_ROUNDED_RECT track{ { sx, cy - 2, sx + sw, cy + 2 }, 2, 2 };
        dc_->FillRoundedRectangle(track, brush_.Get());
        const float t = (*v - lo) / (hi - lo);
        brush_->SetColor(theme_.accent);
        D2D1_ROUNDED_RECT done{ { sx, cy - 2, sx + sw * t, cy + 2 }, 2, 2 };
        dc_->FillRoundedRectangle(done, brush_.Get());
        D2D1_ELLIPSE knob{ { sx + sw * t, cy }, 7, 7 };
        brush_->SetColor(theme_.content);
        dc_->FillEllipse(knob, brush_.Get());
        brush_->SetColor(theme_.accent);
        dc_->DrawEllipse(knob, brush_.Get(), dips(2));

        wchar_t val[32];
        swprintf_s(val, L"%.0f %s", *v, unit);
        text(val, { r.right - 62, y, r.right - 16, y + 22 }, theme_.ink2,
             M::kCap.size, M::kCap.trackingEm, M::kCap.weight, DWRITE_TEXT_ALIGNMENT_TRAILING);
        inspSliders_.push_back(Slider{ sx, cy, sw, lo, hi, v, name, unit });
        y += 34;
    };

    if (inspTab_ == 1) {                       // 外観
        label(T(L"外観"));
        for (int i = 0; i < 2; ++i) {
            const float rx = x0 + 150 + i * 92;
            D2D1_ELLIPSE dot{ { rx + 8, y + 11 }, 7.5f, 7.5f };
            const bool on = (i == 1) == theme_.dark;
            brush_->SetColor(on ? theme_.accent : theme_.content);
            dc_->FillEllipse(dot, brush_.Get());
            brush_->SetColor(on ? theme_.accent : theme_.ink2);
            dc_->DrawEllipse(dot, brush_.Get(), dips(1));
            if (on) {
                D2D1_ELLIPSE in{ { rx + 8, y + 11 }, 3, 3 };
                brush_->SetColor(rgba(255, 255, 255));
                dc_->FillEllipse(in, brush_.Get());
            }
            text(i == 0 ? T(L"ライト") : T(L"ダーク"),
                 { rx + 24, y, rx + 88, y + 22 }, theme_.ink,
                 M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight);
        }
        y += 34;

        label(T(L"アクセント"));
        static const D2D1_COLOR_F accents[6] = {
            rgba(0x00,0x78,0xD4), rgba(0x2F,0x6F,0xEB), rgba(0x3A,0x8B,0x5C),
            rgba(0x8B,0x5C,0xB8), rgba(0xB8,0x5C,0x38), rgba(0x5A,0x65,0x70) };
        for (int i = 0; i < 6; ++i) {
            const float cx = x0 + 158 + i * 30;
            D2D1_ELLIPSE sw2{ { cx, y + 11 }, 11, 11 };
            brush_->SetColor(accents[i]);
            dc_->FillEllipse(sw2, brush_.Get());
            const auto& a = theme_.accent;
            if (std::fabs(a.r - accents[i].r) < .01f && std::fabs(a.g - accents[i].g) < .01f &&
                std::fabs(a.b - accents[i].b) < .01f) {
                brush_->SetColor(theme_.ink);
                D2D1_ELLIPSE ring{ { cx, y + 11 }, 14, 14 };
                dc_->DrawEllipse(ring, brush_.Get(), dips(2));
            }
        }
        y += 36;
        // Not the backdrop blur - that belongs to DWM and is not ours to move.
        // This is the wash we paint over the content under the toolbar, which
        // genuinely is ours and is what reads as "how glassy".
        slider(T(L"ツールバーの濃度"), &setBlur_, 0, 100, L"%");
    } else if (inspTab_ == 2) {                // 表示
        slider(T(L"行の高さ"), &setRowHeight_, 20, 36, L"px");
        slider(T(L"カラム幅"), &setColumnW_, 150, 300, L"px");

        auto checkbox = [&](const wchar_t* name, const wchar_t* caption, bool on) {
            label(name);
            D2D1_ROUNDED_RECT box{ { x0 + 150, y + 4, x0 + 165, y + 19 }, 4, 4 };
            brush_->SetColor(on ? theme_.accent : theme_.content);
            dc_->FillRoundedRectangle(box, brush_.Get());
            brush_->SetColor(on ? theme_.accent : theme_.ink2);
            dc_->DrawRoundedRectangle(box, brush_.Get(), dips(1));
            if (on) {
                brush_->SetColor(rgba(255, 255, 255));
                dc_->DrawLine({ x0 + 153.5f, y + 11.5f }, { x0 + 156.5f, y + 15 }, brush_.Get(), dips(1.8f));
                dc_->DrawLine({ x0 + 156.5f, y + 15 }, { x0 + 161.5f, y + 8 }, brush_.Get(), dips(1.8f));
            }
            text(caption, { x0 + 174, y, r.right - 16, y + 22 }, theme_.ink,
                 M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight);
            y += 34;
        };
        checkbox(T(L"プレビュー欄"), T(L"カラムの右端に表示"), setPreview_);
        checkbox(T(L"アプリの記号"), T(L"そのアプリの書類に使う"), setAppIcons_);
    } else if (inspTab_ == 0) {               // 一般
        auto checkbox = [&](const wchar_t* name, const wchar_t* caption, bool on) {
            label(name);
            D2D1_ROUNDED_RECT box{ { x0 + 150, y + 4, x0 + 165, y + 19 }, 4, 4 };
            brush_->SetColor(on ? theme_.accent : theme_.content);
            dc_->FillRoundedRectangle(box, brush_.Get());
            brush_->SetColor(on ? theme_.accent : theme_.ink2);
            dc_->DrawRoundedRectangle(box, brush_.Get(), dips(1));
            if (on) {
                brush_->SetColor(rgba(255, 255, 255));
                dc_->DrawLine({ x0 + 153.5f, y + 11.5f }, { x0 + 156.5f, y + 15 }, brush_.Get(), dips(1.8f));
                dc_->DrawLine({ x0 + 156.5f, y + 15 }, { x0 + 161.5f, y + 8 }, brush_.Get(), dips(1.8f));
            }
            text(caption, { x0 + 174, y, r.right - 16, y + 22 }, theme_.ink,
                 M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight);
            y += 34;
        };
        checkbox(T(L"隠しファイル"), T(L"一覧に表示する"), model().showHidden());
        checkbox(T(L"削除"), T(L"ごみ箱へ移す前に確認する"), setConfirmDelete_);

        label(T(L"起動時"));
        text(T(L"最後に見ていた場所"), { x0 + 174, y, r.right - 16, y + 22 }, theme_.ink,
             M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight);
        y += 34;

        label(T(L"タグの保存先"));
        text(L"%LOCALAPPDATA%\\Oriel\\tags.tsv", { x0 + 174, y, r.right - 16, y + 22 },
             theme_.ink2, M::kCap.size, M::kCap.trackingEm, M::kCap.weight);
        y += 24;
        // The shared text format has wrapping off - long file names must clip
        // rather than reflow - so a caption is written as lines, not a block.
        static const wchar_t* note[2] = {
            T(L"別のアプリでファイルを移動すると対応が切れます。"),
            T(L"テキストなので手で直せます。"),
        };
        for (const wchar_t* ln : note) {
            text(ln, { x0 + 174, y, r.right - 16, y + 20 }, theme_.ink2,
                 M::kCap.size, M::kCap.trackingEm, M::kCap.weight);
            y += 20;
        }
        y += 10;
    } else {                                   // キー操作
        label(T(L"配列"));
        for (int i = 0; i < 2; ++i) {
            const bool on = (i == 0) == setOrielKeys_;
            const float cxr = x0 + 158;
            const float cyr = y + 11 + i * 26;
            brush_->SetColor(on ? theme_.accent : theme_.ink2);
            dc_->DrawEllipse(D2D1_ELLIPSE{ { cxr, cyr }, 7, 7 }, brush_.Get(), dips(1.4f));
            if (on) {
                brush_->SetColor(theme_.accent);
                dc_->FillEllipse(D2D1_ELLIPSE{ { cxr, cyr }, 3.6f, 3.6f }, brush_.Get());
            }
            text(i == 0 ? T(L"Oriel 標準") : T(L"Windows 標準"),
                 { x0 + 174, cyr - 11, r.right - 16, cyr + 11 }, theme_.ink,
                 M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight);
        }
        y += 60;

        static const wchar_t* keys[][2] = {
            { L"↑ ↓",        T(L"同じ階層で選び変える") },
            { L"← →",        T(L"一段戻る / 一段進む") },
            { L"Ctrl + F",   T(L"検索を開く") },
            { L"Ctrl + 1〜4",T(L"タグを付け外しする") },
            { L"Ctrl + ,",   T(L"この設定を開閉する") },
            { L"Esc",        T(L"検索や入力を抜ける") },
        };
        for (const auto& k : keys) {
            text(k[0], { x0 + 16, y, x0 + 150, y + 22 }, theme_.ink2,
                 M::kCap.size, M::kCap.trackingEm, M::kCap.weight,
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
            text(k[1], { x0 + 174, y, r.right - 16, y + 22 }, theme_.ink,
                 M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight);
            y += 28;
        }
        y += 6;
        text(setOrielKeys_ ? T(L"Enter は名前の変更、Ctrl + ↓ で開きます。")
                           : T(L"Enter で開きます。名前の変更は F2 です。"),
             { x0 + 16, y, r.right - 16, y + 40 }, theme_.ink2,
             M::kCap.size, M::kCap.trackingEm, M::kCap.weight);
    }

    if (ink < 0.999f) dc_->PopLayer();
    dc_->PopAxisAlignedClip();
}

bool AppWindow::inspectorHit(float x, float y, bool dragging) {
    if (inspW_.target() <= 0) return false;
    RECT rc{}; GetClientRect(hwnd_, &rc);
    const float right = dips(rc.right - rc.left);
    const float x0 = right - M::kInspector;
    if (x < x0) return false;

    if (dragging) {
        if (inspDragSlider_ < 0 ||
            inspDragSlider_ >= static_cast<int>(inspSliders_.size())) return false;
        const auto& s = inspSliders_[inspDragSlider_];
        const float t = std::clamp((x - s.x) / s.w, 0.0f, 1.0f);
        *s.target = s.lo + t * (s.hi - s.lo);
        settingsChanged();
        render();
        return true;
    }

    const float top = M::kTitleBar;
    // tabs
    const float tabsY = top + 12 + 32;
    if (y >= tabsY && y <= tabsY + 28) {
        const float tw = (M::kInspector - 28) / 4.0f;
        const int i = static_cast<int>((x - (x0 + 14)) / tw);
        if (i >= 0 && i < 4) { inspTab_ = i; tick(); }   // the tab fade needs frames
        return true;
    }
    // sliders
    for (size_t i = 0; i < inspSliders_.size(); ++i) {
        const auto& s = inspSliders_[i];
        if (y >= s.y - 12 && y <= s.y + 12 && x >= s.x - 10 && x <= s.x + s.w + 10) {
            inspDragSlider_ = static_cast<int>(i);
            SetCapture(hwnd_);
            const float t = std::clamp((x - s.x) / s.w, 0.0f, 1.0f);
            *s.target = s.lo + t * (s.hi - s.lo);
            settingsChanged();
            render();
            return true;
        }
    }
    // appearance radios / accent swatches / preview checkbox, by row
    const float rowsTop = tabsY + 28 + 24;
    if (inspTab_ == 1) {
        if (y >= rowsTop && y <= rowsTop + 22) {
            // Explicit, not derived from the current colours: otherwise the
            // next system theme change silently overrules the choice.
            const int want = (x >= x0 + 150 + 92) ? 2 : 1;
            if (want != setThemeMode_) {
                setThemeMode_ = want;
                applyTheme();
                settingsChanged();
                tick();
            }
            return true;
        }
        if (y >= rowsTop + 34 && y <= rowsTop + 34 + 22) {
            const int i = static_cast<int>((x - (x0 + 147)) / 30);
            if (i >= 0 && i < 6) {
                setAccent_ = i;
                applyTheme();
                settingsChanged();
                tick();
            }
            return true;
        }
    } else if (inspTab_ == 2) {
        // Two sliders, then one checkbox per 34px row - the same ladder the
        // paint walks, so a row added there has to be added here too.
        const float cbY = rowsTop + 34 * 2;
        if (y >= cbY && y < cbY + 34)      { setPreview_ = !setPreview_; settingsChanged(); tick(); return true; }
        if (y >= cbY + 34 && y < cbY + 68) { setAppIcons_ = !setAppIcons_; settingsChanged(); tick(); return true; }
    } else if (inspTab_ == 0) {
        if (y >= rowsTop && y < rowsTop + 34) {
            model().setShowHidden(hwnd_, !model().showHidden());
            settingsChanged();
            tick();
            return true;
        }
        if (y >= rowsTop + 34 && y < rowsTop + 68) {
            setConfirmDelete_ = !setConfirmDelete_; tick(); return true;
        }
    } else if (inspTab_ == 3) {
        // Two radios, 26px apart, starting on the first row.
        if (y >= rowsTop && y < rowsTop + 26)      { setOrielKeys_ = true;  settingsChanged(); tick(); return true; }
        if (y >= rowsTop + 26 && y < rowsTop + 52) { setOrielKeys_ = false; settingsChanged(); tick(); return true; }
    }
    return true;   // clicks inside the panel never fall through to the file list
}

void AppWindow::setView(View v) {
    if (view_ == v) return;
    view_ = v;
    flatScrollY_ = 0;
    flatSelected_ = -1;
    viewThumb_.to(static_cast<float>(static_cast<int>(v)), M::kMsThumb);
    settingsChanged();
    tick();
}

void AppWindow::tick() {
    // Grow or shrink the frame in step with the panel, so the content keeps the
    // width it had. If that would run past the screen, stay put and let the
    // content give up the space instead - a window off the edge is worse.
    if (inspW_.active() || inspW_.target() != inspW_.value()) {
        RECT wr{}; GetWindowRect(hwnd_, &wr);
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int want = inspBaseW_ + static_cast<int>(px(inspW_.value()));
        if (!IsZoomed(hwnd_) && wr.left + want <= work.right)
            SetWindowPos(hwnd_, nullptr, 0, 0, want, wr.bottom - wr.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    render();
    // Keep frames coming only while something is actually moving. fades_ has to
    // be asked after render(), because that is when it learns what is on screen.
    const bool moving = viewThumb_.active() || inspW_.active() || inspInk_.active() || sideW_.active() ||
                        sidePill_.active() || columnsAnimating() || tabsAnimating() ||
                        fades_.active();
    if (moving) {
        SetTimer(hwnd_, 1, 8, nullptr);
    } else if (focusedField()) {
        // Only the caret is left to move. A frame every 8ms for a blink would
        // be 125 full repaints a second to toggle one rectangle.
        SetTimer(hwnd_, 1, 265, nullptr);
    } else {
        KillTimer(hwnd_, 1);
    }
}

D2D1_RECT_F AppWindow::contentArea() const {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    return { sideWidth(), M::kTitleBar,
             dips(rc.right - rc.left) - inspectorWidth(),
             dips(rc.bottom - rc.top) - M::kPathBar
                 - (picker_ ? actionBarHeight() : 0.0f) };
}

std::vector<AppWindow::ColRect> AppWindow::columnLayout(const D2D1_RECT_F& area) const {
    std::vector<ColRect> out;
    float x = area.left - scrollX_;
    const auto& cols = model().columns();
    for (size_t i = 0; i < cols.size(); ++i) {
        out.push_back(ColRect{ i, x, setColumnW_ });
        x += setColumnW_;
    }
    if (model().previewEntry()) out.push_back(ColRect{ cols.size(), x, M::kPreview });
    return out;
}

void AppWindow::scrollColumnsToEnd(float areaWidth) {
    const auto& cols = model().columns();
    float total = cols.size() * setColumnW_ + (model().previewEntry() ? M::kPreview : 0.0f);
    scrollX_ = std::max(0.0f, total - areaWidth);
}

void AppWindow::scrollSelectionIntoView() {
    auto& cols = model().columnsMut();
    if (cols.empty()) return;
    const auto area = contentArea();
    const float viewTop = M::kToolBar + 4;
    const float viewH = (area.bottom - area.top) - viewTop - 8;
    for (auto& c : cols) {
        if (c.selected < 0) continue;
        // Scroll to where the row is drawn, not to its index in the listing.
        // With a filter on, those are different numbers and the view lands
        // somewhere the selection is not.
        const auto rows = visibleRows(c);
        int slot = -1;
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i] == c.selected) { slot = static_cast<int>(i); break; }
        if (slot < 0) continue;                 // filtered out; nothing to show

        const float rowTop = slot * setRowHeight_;
        if (rowTop < c.scrollY) c.scrollY = rowTop;
        else if (rowTop + setRowHeight_ > c.scrollY + viewH) c.scrollY = rowTop + setRowHeight_ - viewH;
        const float maxScroll =
            std::max(0.0f, rows.size() * setRowHeight_ - viewH);
        c.scrollY = std::clamp(c.scrollY, 0.0f, maxScroll);
    }
}

bool AppWindow::hitTestRow(float x, float y, size_t* depth, int* index) const {
    const auto area = contentArea();
    if (y < area.top + M::kToolBar || y > area.bottom) return false;
    const auto& cols = model().columns();
    for (const auto& cr : columnLayout(area)) {
        if (cr.depth >= cols.size()) break;           // the preview takes no clicks
        if (x < cr.x || x >= cr.x + cr.w) continue;
        const auto& c = cols[cr.depth];
        const float top = area.top + M::kToolBar + 4 - c.scrollY;
        // Screen slot, then back through the same filter the paint used - the
        // two must agree or clicks land on a different row than the eye sees.
        const int slot = static_cast<int>((y - top) / setRowHeight_);
        const auto rows = visibleRows(c);
        if (slot < 0 || slot >= static_cast<int>(rows.size())) return false;
        *depth = cr.depth; *index = rows[static_cast<size_t>(slot)];
        return true;
    }
    return false;
}

// ── search ─────────────────────────────────────────────────────────
namespace {
bool containsFold(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    // Ordinal, case-insensitive: file names are compared the way the file
    // system compares them, not the way the current locale would like to.
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i)
        if (CompareStringOrdinal(hay.c_str() + i, static_cast<int>(needle.size()),
                                 needle.c_str(), static_cast<int>(needle.size()),
                                 TRUE) == CSTR_EQUAL)
            return true;
    return false;
}
} // namespace

// Which rows of a column are on screen right now, as indices into its listing.
// Filtering has to go through here rather than through the listing itself, so
// selection and navigation keep referring to the same items when the filter
// changes.
std::vector<int> AppWindow::visibleRows(const ColumnModel::Column& c) const {
    std::vector<int> out;
    const bool filtering = searchOn_ && !search_.text.empty();
    out.reserve(c.listing.entries.size());
    for (int i = 0; i < static_cast<int>(c.listing.entries.size()); ++i) {
        if (filtering && !containsFold(c.listing.entries[i].name, search_.text)) continue;
        if (picker_ && !c.listing.entries[i].isDir &&
            !passesTypeFilter(c.listing.entries[i].name)) continue;
        out.push_back(i);
    }
    return out;
}

// Arrow keys walk what is on screen, not what is in the listing. Stepping
// through hidden rows while a filter is on looks like the selection vanishing.
void AppWindow::moveSelectionVisible(int delta, bool extend) {
    auto& cols = model().columnsMut();
    if (cols.empty()) return;
    size_t depth = cols.size() - 1;
    while (depth > 0 && cols[depth].listing.entries.empty()) --depth;

    const auto rows = visibleRows(cols[depth]);
    if (rows.empty()) return;

    // Where the current selection sits in the visible list, if it is in it.
    int slot = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i] == cols[depth].selected) { slot = static_cast<int>(i); break; }

    const int next = (slot < 0) ? (delta > 0 ? 0 : static_cast<int>(rows.size()) - 1)
                                : std::clamp(slot + delta, 0, static_cast<int>(rows.size()) - 1);
    const int to = rows[static_cast<size_t>(next)];
    // Shift grows the run from the anchor rather than moving a single choice.
    if (extend) model().extendSelect(hwnd_, depth, to, rows);
    else        model().select(hwnd_, depth, to);
}

void AppWindow::setSearch(bool on) {
    if (searchOn_ == on) return;
    searchOn_ = on;
    search_.focused = on;
    if (!on) search_.clear();
    tick();
}

// ── path bar ───────────────────────────────────────────────────────
bool AppWindow::crumbClick(float x, float y) {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    const float bot = dips(rc.bottom - rc.top) - M::kPathBar
                    - (picker_ ? actionBarHeight() : 0.0f);
    if (y < bot || y > bot + M::kPathBar) return false;
    for (const auto& cb : crumbs_) {
        if (x < cb.x || x > cb.x + cb.w) continue;
        // Walking back means dropping the columns to the right of that step.
        auto& cols = model().columnsMut();
        if (cb.depth + 1 < cols.size()) cols.resize(cb.depth + 1);
        if (cb.depth < cols.size()) { cols[cb.depth].sel.clear(); cols[cb.depth].selected = -1; }
        scrollColumnsToEnd(contentArea().right - contentArea().left);
        tick();
        return true;
    }
    return false;
}

// ── picker ─────────────────────────────────────────────────────────
float AppWindow::actionBarHeight() const {
    // Saving needs a name and a type; opening needs neither.
    return pickerReq_.save ? M::kActionBar + 40.0f : M::kActionBar;
}

D2D1_RECT_F AppWindow::nameFieldRect() const {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    const float w = dips(rc.right - rc.left), h = dips(rc.bottom - rc.top);
    const float top = h - actionBarHeight() + 12;
    return { 78, top, w - 210, top + 26 };
}

D2D1_RECT_F AppWindow::typeFieldRect() const {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    const float w = dips(rc.right - rc.left), h = dips(rc.bottom - rc.top);
    const float top = h - actionBarHeight() + 12;
    return { w - 194, top, w - 16, top + 26 };
}

bool AppWindow::passesTypeFilter(const std::wstring& name) const {
    if (!picker_ || pickerReq_.types.empty()) return true;
    const int i = std::clamp(pickerReq_.typeIndex, 0,
                             static_cast<int>(pickerReq_.types.size()) - 1);
    const std::wstring& spec = pickerReq_.types[static_cast<size_t>(i)].spec;
    if (spec.empty()) return true;
    size_t start = 0;
    while (start <= spec.size()) {
        size_t end = spec.find(L';', start);
        if (end == std::wstring::npos) end = spec.size();
        std::wstring pat = spec.substr(start, end - start);
        // trim
        while (!pat.empty() && pat.front() == L' ') pat.erase(0, 1);
        while (!pat.empty() && pat.back() == L' ') pat.pop_back();
        if (pat == L"*" || pat == L"*.*") return true;
        if (!pat.empty() && PathMatchSpecW(name.c_str(), pat.c_str())) return true;
        if (end == spec.size()) break;
        start = end + 1;
    }
    return false;
}

void AppWindow::cycleType() {
    if (pickerReq_.types.size() < 2) return;
    pickerReq_.typeIndex =
        (pickerReq_.typeIndex + 1) % static_cast<int>(pickerReq_.types.size());
    tick();
}

std::wstring AppWindow::pickerResult() const {
    const std::wstring typed = nameField_.text;
    if (!pickerReq_.save) {
        // Opening: a typed name still wins, so a path can be pasted in.
        if (typed.empty()) return model().selectedFullPath();
    } else if (typed.empty()) {
        return model().selectedFullPath();
    }
    // An absolute path is taken as given; anything else hangs off the folder in
    // view, which is the one the user can actually see.
    if (typed.size() > 1 && (typed[1] == L':' || typed[0] == L'\\')) return typed;

    const auto* deepest = model().deepestListed();
    std::wstring dir = deepest ? deepest->place.path : std::wstring{};
    if (dir.empty()) return typed;
    if (dir.back() != L'\\') dir += L'\\';
    return dir + typed;
}

void AppWindow::paintColumns(const D2D1_RECT_F& r) {
    const auto& cols = model().columns();
    const size_t active = model().activeDepth();

    dc_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
    for (const auto& cr : columnLayout(r)) {
        if (cr.x > r.right || cr.x + cr.w < r.left) continue;   // fully scrolled away
        if (cr.depth >= cols.size()) {
            paintPreview({ cr.x, r.top, cr.x + cr.w, r.bottom });
            continue;
        }
        const auto& c = cols[cr.depth];

        // A newly revealed column slides in from the right and fades up, with
        // each depth started 45ms after the one before it.
        bool sliding = false;
        D2D1_MATRIX_3X2_F savedT{};
        if (c.appeared) {
            const ULONGLONG now = GetTickCount64();
            const long long delay = 45LL * static_cast<long long>(cr.depth % 4);
            const long long age = static_cast<long long>(now - c.appeared) - delay;
            if (age < M::kMsColumn) {
                const float p = age <= 0 ? 0.0f
                              : static_cast<float>(age) / static_cast<float>(M::kMsColumn);
                const float e = ease(p);
                dc_->GetTransform(&savedT);
                dc_->SetTransform(D2D1::Matrix3x2F::Translation((1.0f - e) * 20.0f, 0) * savedT);
                dc_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                                     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                     D2D1::IdentityMatrix(), e),
                               nullptr);
                sliding = true;
            }
        }

        if (c.loading) {
            text(T(L"読み込み中…"), { cr.x + 15, r.top + M::kToolBar + 4,
                                   cr.x + cr.w - 15, r.top + M::kToolBar + 4 + setRowHeight_ },
                 theme_.ink2, M::kBody.size, M::kBody.trackingEm, M::kBody.weight);
        } else if (c.listing.error != ERROR_SUCCESS) {
            text(c.listing.error == ERROR_ACCESS_DENIED ? T(L"アクセスできません")
                                                        : T(L"読み取れません"),
                 { cr.x + 15, r.top + M::kToolBar + 4,
                   cr.x + cr.w - 15, r.top + M::kToolBar + 4 + setRowHeight_ },
                 theme_.ink2, M::kBody.size, M::kBody.trackingEm, M::kBody.weight);
        } else if (const auto rows = visibleRows(c); rows.empty()) {
            text(c.listing.entries.empty() ? T(L"項目がありません") : T(L"一致する項目がありません"),
                 { cr.x + 15, r.top + M::kToolBar + 4,
                   cr.x + cr.w - 15, r.top + M::kToolBar + 4 + setRowHeight_ },
                 theme_.ink2, M::kBody.size, M::kBody.trackingEm, M::kBody.weight);
        } else {
            const float top = r.top + M::kToolBar + 4 - c.scrollY;
            // Only draw the rows that can actually be seen.
            const int first = std::max(0, static_cast<int>((r.top + M::kToolBar - top) / setRowHeight_));
            const int last  = std::min(static_cast<int>(rows.size()),
                                       static_cast<int>((r.bottom - top) / setRowHeight_) + 1);
            dc_->PushAxisAlignedClip(
                D2D1_RECT_F{ cr.x, r.top + M::kToolBar, cr.x + cr.w, r.bottom },
                D2D1_ANTIALIAS_MODE_ALIASED);
            for (int slot = first; slot < last; ++slot) {
                const int i = rows[static_cast<size_t>(slot)];
                const Entry& e = c.listing.entries[i];
                D2D1_RECT_F row{ cr.x + 6, top + slot * setRowHeight_,
                                 cr.x + cr.w - 6, top + (slot + 1) * setRowHeight_ };
                const bool sel = c.isSelected(i);
                // Rows are keyed by (depth, index) so leaving one fades it out
                // while the next fades in, instead of cutting.
                const int rowKey = static_cast<int>(cr.depth) * 4096 + i;
                const D2D1_ROUNDED_RECT rr{ row, 5.0f, 5.0f };

                const float hv = fades_.at(fkey(Fx::RowHover, rowKey),
                                           !sel && cr.depth == hoverDepth_ && i == hoverIndex_,
                                           M::kMsHover);
                if (hv > 0.002f) {
                    auto h = theme_.hover; h.a *= hv;
                    brush_->SetColor(h);
                    dc_->FillRoundedRectangle(rr, brush_.Get());
                }
                // The working column fills with the accent; the ones you came
                // through keep a quiet grey so the route stays readable.
                const float sv = fades_.at(fkey(Fx::RowSel, rowKey), sel, M::kMsHover);
                if (sv > 0.002f) {
                    auto s = (cr.depth == active) ? theme_.accent : theme_.selIdle;
                    s.a *= sv;
                    brush_->SetColor(s);
                    dc_->FillRoundedRectangle(rr, brush_.Get());
                }
                // Ink turns white only as fast as the accent arrives under it;
                // flipping it on the first frame puts white on white.
                const float onAccent = (cr.depth == active) ? sv : 0.0f;
                const float cy = (row.top + row.bottom) * 0.5f;
                float textRight = row.right - 9;

                brush_->SetColor(lerpColor(theme_.ink2, rgba(255,255,255), onAccent));
                if (ID2D1Bitmap* app = rowIcon(e, c.place, static_cast<int>(M::kIconSide)))
                    drawIconBitmap(app, row.left + 17, cy, M::kIconSide);
                else
                    icons_.draw(dc_.Get(), brush_.Get(), iconFor(e), row.left + 17, cy,
                             M::kIconSide, M::kIconStroke);
                if (e.isDir) {
                    icons_.draw(dc_.Get(), brush_.Get(), Icon::ChevronRight,
                             row.right - 13, cy, 12, M::kIconStroke);
                    textRight = row.right - 22;
                }
                // Tag dots ride at the right edge, before the chevron: a row
                // has to say it is tagged without the name having to move.
                const unsigned tm = tags_.empty() ? 0u
                                  : tags_.maskFor(childOf(c.place, e).path);
                if (tm) {
                    float tx = textRight - 5;
                    for (int t = kTagCount - 1; t >= 0; --t) {
                        if (!(tm & (1u << t))) continue;
                        brush_->SetColor(onAccent > 0.5f ? rgba(255,255,255) : kTagColors[t]);
                        dc_->FillEllipse(D2D1_ELLIPSE{ { tx, cy }, 3.5f, 3.5f }, brush_.Get());
                        tx -= 10;
                    }
                    textRight = tx - 3;
                }
                if (renaming_ && cr.depth == renameDepth_ && i == renameIndex_) {
                    paintField({ row.left + 28, row.top + 1, row.right - 4, row.bottom - 1 },
                               renameField_, L"", M::kBody.size,
                               M::kBody.trackingEm, M::kBody.weight);
                } else {
                    text(e.name.c_str(), { row.left + 33, row.top, textRight, row.bottom },
                         lerpColor(theme_.ink, rgba(255, 255, 255), onAccent),
                         M::kBody.size, M::kBody.trackingEm, M::kBody.weight);
                }
            }
            dc_->PopAxisAlignedClip();
        }
        hairline(cr.x + cr.w - dips(1), r.top, cr.x + cr.w, r.bottom, theme_.hair);
        if (sliding) { dc_->PopLayer(); dc_->SetTransform(savedT); }
    }
    dc_->PopAxisAlignedClip();
}

bool AppWindow::columnsAnimating() const {
    const ULONGLONG now = GetTickCount64();
    for (const auto& c : model().columns())
        if (c.appeared && static_cast<long long>(now - c.appeared) <
                          M::kMsColumn + 45 * 4)
            return true;
    return false;
}

ID2D1Bitmap* AppWindow::shellImage(const std::wstring& key, const std::wstring& path,
                                   int dipBox, bool iconOnly, bool* isIcon) {
    if (key.empty() || path.empty()) return nullptr;
    auto it = thumbCache_.find(key);
    if (it != thumbCache_.end()) {
        // touch: move to the front of the eviction order
        thumbLru_.remove(key);
        thumbLru_.push_front(key);
        if (isIcon) *isIcon = it->second.isIcon;
        return it->second.bmp.Get();
    }
    if (!thumbPending_.count(key)) {
        thumbPending_[key] = true;
        // Ask in device pixels so the result is crisp at this scale.
        thumbs_.request(hwnd_, key, path,
                        static_cast<int>(px(static_cast<float>(dipBox))), iconOnly);
    }
    return nullptr;
}

ID2D1Bitmap* AppWindow::thumbFor(const std::wstring& path, int dipBox, bool* isIcon) {
    return shellImage(path, path, dipBox, false, isIcon);
}

// Folders and anything Windows itself handles keep our own family; a type owned
// by a real application gets that application's mark, because "this is a Blender
// scene" is something a generic document outline can never say.
ID2D1Bitmap* AppWindow::rowIcon(const Entry& e, const Place& parent, int dipBox) {
    if (!setAppIcons_ || e.isDir) return nullptr;
    const size_t dot = e.name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= e.name.size()) return nullptr;
    std::wstring ext = e.name.substr(dot + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

    const Mark mk = markForExt(ext);
    if (mk == Mark::Glyph) return nullptr;
    // Being the registered handler is not the same as owning the type. Whatever
    // currently opens .png or .mp3 does not get to speak for it - our own family
    // says "image" and "audio" better, and letting the handler win puts a music
    // player's mark on TypeScript. The application's icon is for the types we
    // have nothing specific to say about, which is exactly the project files.
    if (mk == Mark::TypeIcon && haveMarkForExt(ext)) return nullptr;

    const std::wstring path = childOf(parent, e).path;
    if (path.empty()) return nullptr;
    // One entry per extension, except where the icon is inside the file itself.
    // The "row:" prefix keeps these out of the preview's slot: a row asks for a
    // 16 DIP icon, and the preview finding that under the bare path is exactly
    // how the preview ended up drawing a thumbnail-sized mark.
    const std::wstring key = (mk == Mark::FileIcon) ? (L"row:" + path) : (L"." + ext);
    return shellImage(key, path, dipBox, true);
}

void AppWindow::onThumbReady(ThumbBits* raw) {
    std::unique_ptr<ThumbBits> bits(raw);
    if (!bits || !dc_) return;
    thumbPending_.erase(bits->key);

    auto props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    ComPtr<ID2D1Bitmap> bmp;
    if (FAILED(dc_->CreateBitmap(D2D1::SizeU(bits->w, bits->h), bits->bgra.data(),
                                 bits->w * 4, props, &bmp)))
        return;

    const size_t bytes = static_cast<size_t>(bits->w) * bits->h * 4;
    // Replacing an entry has to give its bytes back, or the total drifts up
    // every time the same file is looked at again.
    if (const auto it = thumbCache_.find(bits->key); it != thumbCache_.end()) {
        thumbBytes_ -= it->second.bytes;
        thumbLru_.remove(bits->key);
    }
    thumbCache_[bits->key] = CachedThumb{ bmp, bits->isIcon, bytes };
    thumbBytes_ += bytes;
    thumbLru_.push_front(bits->key);

    // Evict oldest-first until back inside the budget, but never drop the last
    // few: a cache that empties itself would re-decode what is on screen.
    while (thumbLru_.size() > 8 && thumbBytes_ > kThumbBudget) {
        const auto victim = thumbCache_.find(thumbLru_.back());
        if (victim != thumbCache_.end()) {
            thumbBytes_ -= victim->second.bytes;
            thumbCache_.erase(victim);
        }
        thumbLru_.pop_back();
    }
    tick();
}

// ── list and icon views ────────────────────────────────────────────
const ColumnModel::Column* AppWindow::currentFolder() const {
    return model().deepestListed();
}

bool AppWindow::hitTestFlat(float x, float y, int* index) const {
    const auto* c = currentFolder();
    if (!c || c->loading) return false;
    const auto area = contentArea();
    const float top = area.top + M::kToolBar + (view_ == View::List ? 24.0f : 0.0f);
    if (y < top || y > area.bottom || x < area.left) return false;

    if (view_ == View::List) {
        const int i = static_cast<int>((y - top + flatScrollY_) / setRowHeight_);
        if (i < 0 || i >= static_cast<int>(c->listing.entries.size())) return false;
        *index = i;
        return true;
    }
    const float cell = 112.0f, cellH = 96.0f;
    const int perRow = std::max(1, static_cast<int>((area.right - area.left - 14) / cell));
    const int col = static_cast<int>((x - area.left - 7) / cell);
    const int row = static_cast<int>((y - top - 10 + flatScrollY_) / cellH);
    if (col < 0 || col >= perRow || row < 0) return false;
    const int i = row * perRow + col;
    if (i < 0 || i >= static_cast<int>(c->listing.entries.size())) return false;
    *index = i;
    return true;
}

void AppWindow::paintList(const D2D1_RECT_F& r) {
    const auto* c = currentFolder();
    if (!c) return;

    // Header, pinned: it names the columns, so it must not scroll away.
    const float head = r.top + M::kToolBar;
    const float cols[3] = { 108.0f, 92.0f, 96.0f };   // date, size, kind
    const float right = r.right;
    fill({ r.left, head, r.right, head + 24 }, theme_.content);
    hairline(r.left, head + 24 - dips(1), r.right, head + 24, theme_.hair);
    const wchar_t* titles[4] = { T(L"名前"), T(L"変更日"), T(L"サイズ"), T(L"種類") };
    float hx = right - 14;
    for (int i = 2; i >= 0; --i) {
        text(titles[i + 1], { hx - cols[i], head, hx, head + 24 }, theme_.ink2,
             M::kCap.size, M::kCap.trackingEm, M::kCap.weight, DWRITE_TEXT_ALIGNMENT_TRAILING);
        hx -= cols[i] + 12;
    }
    text(titles[0], { r.left + 14, head, hx, head + 24 }, theme_.ink2,
         M::kCap.size, M::kCap.trackingEm, M::kCap.weight);

    const float top = head + 24 - flatScrollY_;
    dc_->PushAxisAlignedClip({ r.left, head + 24, r.right, r.bottom },
                             D2D1_ANTIALIAS_MODE_ALIASED);
    const int first = std::max(0, static_cast<int>((head + 24 - top) / setRowHeight_));
    const int last  = std::min(static_cast<int>(c->listing.entries.size()),
                               static_cast<int>((r.bottom - top) / setRowHeight_) + 1);
    for (int i = first; i < last; ++i) {
        const Entry& e = c->listing.entries[i];
        const D2D1_RECT_F row{ r.left, top + i * setRowHeight_, r.right, top + (i + 1) * setRowHeight_ };
        const bool sel = (i == flatSelected_);
        if (sel)               fill(row, theme_.accent);
        else if (i & 1)        fill(row, theme_.hover);    // quiet zebra, as designed

        const D2D1_COLOR_F ink  = sel ? rgba(255,255,255) : theme_.ink;
        const D2D1_COLOR_F meta = sel ? rgba(255,255,255) : theme_.ink2;
        float x = right - 14;
        const std::wstring cells[3] = { formatDate(e.written), formatSize(e), formatKind(e) };
        for (int k = 2; k >= 0; --k) {
            text(cells[k].c_str(), { x - cols[k], row.top, x, row.bottom }, meta,
                 M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight,
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
            x -= cols[k] + 12;
        }
        brush_->SetColor(sel ? rgba(255,255,255) : theme_.ink2);
        if (ID2D1Bitmap* app = rowIcon(e, c->place, static_cast<int>(M::kIconSide)))
            drawIconBitmap(app, r.left + 22, (row.top + row.bottom) * 0.5f, M::kIconSide);
        else
            icons_.draw(dc_.Get(), brush_.Get(), iconFor(e), r.left + 22,
                     (row.top + row.bottom) * 0.5f, M::kIconSide, M::kIconStroke);
        text(e.name.c_str(), { r.left + 38, row.top, x, row.bottom }, ink,
             M::kBody.size, M::kBody.trackingEm, M::kBody.weight);
    }
    dc_->PopAxisAlignedClip();
}

void AppWindow::paintIcons(const D2D1_RECT_F& r) {
    const auto* c = currentFolder();
    if (!c) return;
    const float cell = 112.0f, cellH = 96.0f;
    const int perRow = std::max(1, static_cast<int>((r.right - r.left - 14) / cell));
    const float top = r.top + M::kToolBar + 10 - flatScrollY_;

    dc_->PushAxisAlignedClip({ r.left, r.top + M::kToolBar, r.right, r.bottom },
                             D2D1_ANTIALIAS_MODE_ALIASED);
    for (int i = 0; i < static_cast<int>(c->listing.entries.size()); ++i) {
        const int row = i / perRow, col = i % perRow;
        const float x = r.left + 7 + col * cell;
        const float y = top + row * cellH;
        if (y > r.bottom || y + cellH < r.top + M::kToolBar) continue;   // off-screen

        const Entry& e = c->listing.entries[i];
        const D2D1_RECT_F box{ x + 4, y + 2, x + cell - 4, y + cellH - 4 };
        if (i == flatSelected_) {
            D2D1_ROUNDED_RECT rr{ box, 8, 8 };
            brush_->SetColor(theme_.accent);
            dc_->FillRoundedRectangle(rr, brush_.Get());
        }

        ID2D1Bitmap* thumb = thumbFor(childOf(c->place, e).path, 48);
        const float cx = (box.left + box.right) * 0.5f;
        if (thumb) {
            const auto s = thumb->GetSize();
            const float k = std::min(1.0f, std::min(48.0f / s.width, 48.0f / s.height));
            const float w = s.width * k, h = s.height * k;
            dc_->DrawBitmap(thumb, D2D1_RECT_F{ cx - w * .5f, y + 14 + (48 - h) * .5f,
                                                cx + w * .5f, y + 14 + (48 + h) * .5f },
                            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            brush_->SetColor(i == flatSelected_ ? rgba(255,255,255) : theme_.ink2);
            icons_.draw(dc_.Get(), brush_.Get(), iconFor(e), cx, y + 38, 40, 40 * M::kIconBigRatio);
        }
        text(e.name.c_str(), { box.left + 4, y + 66, box.right - 4, y + 88 },
             i == flatSelected_ ? rgba(255,255,255) : theme_.ink,
             M::kCap.size, M::kCap.trackingEm, M::kCap.weight,
             DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    dc_->PopAxisAlignedClip();
}

void AppWindow::paintPreview(const D2D1_RECT_F& r) {
    const Entry* e = model().previewEntry();
    if (!e) return;
    const float cx = (r.left + r.right) * 0.5f;
    float y = r.top + M::kToolBar + 40;

    constexpr float kBox = 128.0f;
    bool isIcon = false;
    ID2D1Bitmap* thumb = thumbFor(model().selectedFullPath(), static_cast<int>(kBox), &isIcon);
    if (thumb) {
        const auto s = thumb->GetSize();
        // A photograph must not be blown up past its own resolution. An icon is
        // artwork meant to be scaled, and the shell hands those back at whatever
        // size it has - often 32 or 48 - so capping those at 1.0 is what made
        // the preview draw a mark the size of a row.
        const float fit = std::min(kBox / s.width, kBox / s.height);
        const float scale = isIcon ? fit : std::min(1.0f, fit);
        const float w = s.width * scale, h = s.height * scale;
        const D2D1_RECT_F dst{ cx - w * 0.5f, y + (kBox - h) * 0.5f,
                               cx + w * 0.5f, y + (kBox + h) * 0.5f };
        dc_->DrawBitmap(thumb, dst, 1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        // Nothing decoded yet: show what we already know it is.
        brush_->SetColor(theme_.ink2);
        icons_.draw(dc_.Get(), brush_.Get(), iconFor(*e), cx, y + kBox * 0.5f, 72, 72 * M::kIconBigRatio);
    }
    y += kBox + 18;

    text(e->name.c_str(), { r.left + 20, y, r.right - 20, y + 22 },
         theme_.ink, M::kTitle.size, M::kTitle.trackingEm, M::kTitle.weight,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    y += 30;

    const std::wstring rows[3][2] = {
        { T(L"種類"),    formatKind(*e)     },
        { T(L"サイズ"),  formatSize(*e)     },
        { T(L"変更日"),  formatDate(e->written) },
    };
    for (const auto& kv : rows) {
        text(kv[0].c_str(), { r.left + 20, y, cx - 7, y + 18 }, theme_.ink2,
             M::kCap.size, M::kCap.trackingEm, M::kCap.weight, DWRITE_TEXT_ALIGNMENT_TRAILING);
        text(kv[1].c_str(), { cx + 7, y, r.right - 20, y + 18 }, theme_.ink,
             M::kCap.size, M::kCap.trackingEm, M::kCap.weight);
        y += 20;
    }
}

void AppWindow::paintPathBar(const D2D1_RECT_F& r) {
    fill(r, theme_.chrome);
    hairline(r.left, r.top, r.right, r.top + dips(1), theme_.hair);

    // Each step is its own target: a breadcrumb that cannot be clicked is a
    // label pretending to be a control.
    crumbs_.clear();
    const auto parts = model().breadcrumb();
    float x = r.left + 12;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) {
            text(L"›", { x, r.top, x + 12, r.bottom }, theme_.ink2,
                 M::kCap.size, M::kCap.trackingEm, M::kCap.weight,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
            x += 14;
        }
        const float w = textWidth(parts[i].c_str(), M::kCap.size,
                                  M::kCap.trackingEm, M::kCap.weight) + 12;
        if (x + w > r.right - 110) break;
        const D2D1_RECT_F box{ x, r.top + 3, x + w, r.bottom - 3 };
        const float g = fades_.at(fkey(Fx::Crumb, static_cast<int>(i)),
                                  hotCrumb_ == static_cast<int>(i), M::kMsChrome);
        if (g > 0.002f) {
            auto h = theme_.hover; h.a *= g * 1.8f;
            brush_->SetColor(h);
            dc_->FillRoundedRectangle(D2D1_ROUNDED_RECT{ box, 5, 5 }, brush_.Get());
        }
        text(parts[i].c_str(), box, lerpColor(theme_.ink2, theme_.ink, g),
             M::kCap.size, M::kCap.trackingEm, M::kCap.weight,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        // Step 0 is the root the column stack starts from, so depth is i.
        crumbs_.push_back(Crumb{ x, w, i });
        x += w;
    }

    const auto* deepest = model().deepestListed();
    wchar_t count[64] = L"";
    const size_t picked = model().selectionCount();
    if (picked > 1) {
        // With several chosen, the count of them is the useful number - the
        // preview has nothing to show and the folder total says less.
        swprintf_s(count, T(L"%zu 項目を選択"), picked);
    } else if (deepest && !deepest->loading) {
        const size_t total = deepest->listing.entries.size();
        const size_t shown = visibleRows(*deepest).size();
        if (shown != total) swprintf_s(count, T(L"%zu / %zu 項目"), shown, total);
        else                swprintf_s(count, T(L"%zu 項目"), total);
    }
    text(count, { r.right - 110, r.top, r.right - 12, r.bottom },
         theme_.ink2, M::kCap.size, M::kCap.trackingEm, M::kCap.weight,
         DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// Once per run, build a menu and throw it away. Almost all of the first
// right-click's cost is loading the shell extension DLLs, and that part is
// worth paying up front: measured 310ms for the first build against 76ms for
// every one after it. Doing it once costs one thread's worth of leak instead of
// one per selection.
void AppWindow::warmShellHandlers() {
    if (menuWarmed_ || picker_) return;
    menuWarmed_ = true;
    const std::wstring path = model().selectedFullPath().empty()
                            ? targetFolder() : model().selectedFullPath();
    if (path.empty()) { menuWarmed_ = false; return; }   // nothing to warm with yet
    double ms = 0;
    ShellMenu warm;
    warm.prepare(hwnd_, path, &ms);
    warm.reset();
    logf("shell handlers warmed in %.2f ms", ms);
}

void AppWindow::scheduleMenuPrepare(std::wstring path) {
    if (!setPrebuildMenu_) return;
    if (path.empty() || path == pendingMenuPath_) return;   // already betting on this
    pendingMenuPath_ = std::move(path);
    // NOT a plain PostMessage. A posted message arrives as soon as the input
    // queue drains, which is exactly when the click's animation should be
    // running - and QueryContextMenu blocks for hundreds of milliseconds.
    // Measured: a 330ms build swallowed the whole 260ms column arrival.
    // A timer lets the motion finish first, and re-arms if it has not.
    SetTimer(hwnd_, 2, 140, nullptr);
}

// ── picker mode ────────────────────────────────────────────────────
D2D1_RECT_F AppWindow::actionButton(int which) const {
    RECT rc{}; GetClientRect(hwnd_, &rc);
    const float w = dips(rc.right - rc.left);
    const float h = dips(rc.bottom - rc.top);
    const float top = h - M::kActionBar + (M::kActionBar - M::kButtonH) * 0.5f;
    (void)0;
    const float right = w - 16 - which * (M::kButtonW + 10);
    return { right - M::kButtonW, top, right, top + M::kButtonH };
}

void AppWindow::paintActionBar(const D2D1_RECT_F& r) {
    fill(r, theme_.chrome);
    hairline(r.left, r.top, r.right, r.top + dips(1), theme_.hair);

    // Saving needs somewhere to type a name and a way to narrow the list.
    if (pickerReq_.save) {
        const auto nf = nameFieldRect();
        text(T(L"名前"), { r.left + 16, nf.top, nf.left - 10, nf.bottom }, theme_.ink2,
             M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
        paintField(nf, nameField_, T(L"ファイル名"), M::kBody.size,
                   M::kBody.trackingEm, M::kBody.weight);

        const auto tf = typeFieldRect();
        const float g = fades_.at(fkey(Fx::Action, 9), typeOpen_, M::kMsChrome);
        auto bg = theme_.selSoft; bg.a += 0.05f * g;
        brush_->SetColor(bg);
        dc_->FillRoundedRectangle(stadium(tf), brush_.Get());
        std::wstring label = T(L"すべてのファイル");
        if (!pickerReq_.types.empty()) {
            const int i = std::clamp(pickerReq_.typeIndex, 0,
                                     static_cast<int>(pickerReq_.types.size()) - 1);
            label = pickerReq_.types[static_cast<size_t>(i)].label;
        }
        text(label.c_str(), { tf.left + 12, tf.top, tf.right - 24, tf.bottom },
             theme_.ink, M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight);
        brush_->SetColor(theme_.ink2);
        icons_.draw(dc_.Get(), brush_.Get(), Icon::ChevronDown, tf.right - 14,
                    (tf.top + tf.bottom) * 0.5f, 12, M::kIconStroke);
    }

    // What the application will receive, stated plainly.
    const std::wstring path = pickerResult();
    const bool ready = !path.empty();
    std::wstring shown = ready ? path : T(L"項目を選んでください");
    const float infoTop = r.bottom - M::kActionBar;
    text(shown.c_str(), { r.left + 16, infoTop, r.right - 2 * (M::kButtonW + 10) - 30, r.bottom },
         ready ? theme_.ink : theme_.ink2,
         M::kMeta.size, M::kMeta.trackingEm, M::kMeta.weight);

    struct { const wchar_t* label; bool primary; } b[2] = {
        { pickerReq_.save ? T(L"保存") : T(L"開く"), true },
        { T(L"キャンセル"), false },
    };
    for (int i = 0; i < 2; ++i) {
        const auto box = actionButton(i);
        const bool on = (hotAction_ == i);
        const bool enabled = b[i].primary ? ready : true;
        D2D1_ROUNDED_RECT rr{ box, 6, 6 };
        if (b[i].primary) {
            auto c = theme_.accent;
            if (!enabled) c.a = 0.35f; else if (on) { c.r *= .92f; c.g *= .92f; c.b *= .92f; }
            brush_->SetColor(c);
            dc_->FillRoundedRectangle(rr, brush_.Get());
        } else {
            brush_->SetColor(on ? theme_.selSoft : theme_.hover);
            dc_->FillRoundedRectangle(rr, brush_.Get());
            brush_->SetColor(theme_.hair);
            dc_->DrawRoundedRectangle(rr, brush_.Get(), dips(1));
        }
        text(b[i].label, box,
             b[i].primary ? rgba(255, 255, 255) : theme_.ink,
             M::kBody.size, M::kBody.trackingEm, M::kTitle.weight,
             DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void AppWindow::finishPicker(bool cancelled) {
    if (!picker_ || pickerDone_) return;
    if (pickerReply_) {
        pickerReply_->cancelled = cancelled;
        pickerReply_->path = cancelled ? std::wstring{} : pickerResult();
        if (!cancelled && pickerReply_->path.empty()) pickerReply_->cancelled = true;
    }
    pickerDone_ = true;
    if (pickerReq_.owner && IsWindow(pickerReq_.owner)) SetForegroundWindow(pickerReq_.owner);
    DestroyWindow(hwnd_);
}

void AppWindow::render() {
    if (!dc_) return;
    RECT rc{}; GetClientRect(hwnd_, &rc);
    const float w = dips(rc.right - rc.left);
    const float h = dips(rc.bottom - rc.top);

    fades_.beginFrame();
    dc_->BeginDraw();
    dc_->Clear(D2D1::ColorF(0, 0.0f));   // transparent: the backdrop shows through

    const float bodyTop = M::kTitleBar;
    const float bodyBot = h - M::kPathBar - (picker_ ? actionBarHeight() : 0.0f);
    // The panel takes width off the right; the content keeps its own.
    const float inspW = inspectorWidth();
    const float contentRight = w - inspW;

    // The content column is opaque; the sidebar strip is left clear.
    fill({ sideWidth(), bodyTop, contentRight, bodyBot }, theme_.content);

    paintTitleBar({ 0, 0, w, M::kTitleBar });
    if (sideWidth() > 1) paintSideBar({ 0, bodyTop, sideWidth(), bodyBot });
    switch (view_) {
    case View::Column: paintColumns({ sideWidth(), bodyTop, contentRight, bodyBot }); break;
    case View::List:   paintList   ({ sideWidth(), bodyTop, contentRight, bodyBot }); break;
    case View::Icon:   paintIcons  ({ sideWidth(), bodyTop, contentRight, bodyBot }); break;
    }
    paintToolBar ({ sideWidth(), bodyTop, contentRight, bodyTop + M::kToolBar });
    paintNavButtons();   // after both: they belong to neither strip
    if (inspW > 1) paintInspector({ contentRight, bodyTop, w, bodyBot });
    paintPathBar ({ 0, bodyBot, w, bodyBot + M::kPathBar });
    if (picker_) paintActionBar({ 0, h - actionBarHeight(), w, h });

    dc_->EndDraw();
    swap_->Present(1, 0);
    dcomp_->Commit();
    fades_.gc();   // forget anything settled dark, or no longer on screen
}

bool runPicker(HINSTANCE hinst, const PickRequest& req, PickReply* reply) {
    AppWindow win;
    win.configureAsPicker(req, reply);
    if (!win.create(hinst, SW_SHOW, req.folder)) {
        logf("picker: window creation failed; the shim will use the genuine dialog");
        return false;
    }
    SetForegroundWindow(win.hwnd());

    // A nested loop, which is what modality is. The resident window keeps
    // running because its messages are dispatched here too.
    MSG msg{};
    while (!win.pickerFinished() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

// ══ messages ═══════════════════════════════════════════════════════
LRESULT CALLBACK AppWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<AppWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->handle(hwnd, msg, wp, lp)
                : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT AppWindow::handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    // Take over the whole frame but keep real resize borders on three sides.
    case WM_NCCALCSIZE: {
        if (!wp) break;
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        const int b = frameBorder();
        p->rgrc[0].left   += b;
        p->rgrc[0].right  -= b;
        p->rgrc[0].bottom -= b;
        if (maximized()) p->rgrc[0].top += b;   // otherwise the top row is clipped
        return 0;
    }

    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT wr{}; GetWindowRect(hwnd, &wr);
        const int b = frameBorder();
        const bool top = pt.y < wr.top + b;

        if (!maximized()) {
            const bool left  = pt.x < wr.left + b;
            const bool right = pt.x >= wr.right - b;
            const bool bot   = pt.y >= wr.bottom - b;
            if (top && left)  return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bot && left)  return HTBOTTOMLEFT;
            if (bot && right) return HTBOTTOMRIGHT;
            if (top)   return HTTOP;
            if (bot)   return HTBOTTOM;
            if (left)  return HTLEFT;
            if (right) return HTRIGHT;
        }

        POINT cp = pt; ScreenToClient(hwnd, &cp);
        const float x = dips(cp.x), y = dips(cp.y);
        if (y < M::kCaptionH) {
            const float fromRight = dips(wr.right - wr.left) - x;
            // Reporting these codes is what gives the maximise button the
            // system snap-layout flyout, even though we draw it ourselves.
            if (fromRight < M::kCaptionW * 1) return HTCLOSE;
            if (fromRight < M::kCaptionW * 2) return HTMAXBUTTON;
            if (fromRight < M::kCaptionW * 3) return HTMINBUTTON;
        }
        if (y < M::kTitleBar) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_NCMOUSEMOVE: {
        const int hot = static_cast<int>(wp);
        const int want = (hot == HTMINBUTTON || hot == HTMAXBUTTON || hot == HTCLOSE) ? hot : 0;
        if (want != hotCaption_) { hotCaption_ = want; tick(); }
        if (want) {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        break;
    }
    case WM_NCMOUSELEAVE:
        if (hotCaption_) { hotCaption_ = 0; tick(); }
        break;

    case WM_NCLBUTTONDOWN:
        if (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) return 0;  // act on release
        break;
    case WM_NCLBUTTONUP:
        if (wp == HTMINBUTTON) { ShowWindow(hwnd, SW_MINIMIZE); return 0; }
        if (wp == HTMAXBUTTON) { ShowWindow(hwnd, maximized() ? SW_RESTORE : SW_MAXIMIZE); return 0; }
        if (wp == HTCLOSE)     { PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
        break;

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_ORIEL_ENUM_DONE: {
        std::unique_ptr<EnumResult> r(reinterpret_cast<EnumResult*>(lp));
        if (model().onEnumDone(std::move(r))) {
            // A folder we just created: select it and open the name for editing,
            // which is the only reason anyone made it.
            if (!pendingRenameName_.empty()) {
                const auto& cols = model().columns();
                for (size_t d = cols.size(); d-- > 0; ) {
                    const auto& e = cols[d].listing.entries;
                    for (size_t i = 0; i < e.size(); ++i) {
                        if (e[i].name != pendingRenameName_) continue;
                        model().select(hwnd, d, static_cast<int>(i));
                        pendingRenameName_.clear();
                        beginRename();
                        break;
                    }
                    if (pendingRenameName_.empty()) break;
                }
            }
            scrollColumnsToEnd(contentArea().right - contentArea().left);
            scrollSelectionIntoView();
            tick();     // a column may have just become visible mid-animation
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        const float x = dips(GET_X_LPARAM(lp)), y = dips(GET_Y_LPARAM(lp));

        if (view_ != View::Column) {
            int index = 0;
            const auto* c = currentFolder();
            if (c && hitTestFlat(x, y, &index) && c->listing.entries[index].isDir) {
                model().select(hwnd, model().columns().size() - 1, index);
                flatSelected_ = -1;
                flatScrollY_ = 0;
                render();
            }
            return 0;
        }

        // Double-clicking a file is the same as pressing the confirm button.
        size_t depth = 0; int index = 0;
        if (picker_ && hitTestRow(x, y, &depth, &index)) {
            model().select(hwnd, depth, index);
            if (!model().selectedFullPath().empty() && model().previewEntry())
                finishPicker(false);
            else render();
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        const float x = dips(GET_X_LPARAM(lp)), y = dips(GET_Y_LPARAM(lp));
        if (picker_) {
            for (int i = 0; i < 2; ++i) {
                const auto b = actionButton(i);
                if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) {
                    if (i == 1) finishPicker(true);
                    else if (!pickerResult().empty()) finishPicker(false);
                    return 0;
                }
            }
            if (pickerReq_.save) {
                const auto nf = nameFieldRect();
                if (x >= nf.left && x <= nf.right && y >= nf.top && y <= nf.bottom) {
                    nameField_.focused = true; search_.focused = false;
                    nameField_.selectAll();
                    tick();
                    return 0;
                }
                const auto tf = typeFieldRect();
                if (x >= tf.left && x <= tf.right && y >= tf.top && y <= tf.bottom) {
                    cycleType();   // few filters in practice; a list would be more chrome
                    return 0;
                }
            }
        }
        if (inspectorHit(x, y, false)) return 0;

        // the search capsule, and anywhere else clears its focus
        {
            const auto s = searchRect();
            if (x >= s.left && x <= s.right && y >= s.top && y <= s.bottom) {
                setSearch(true);
                search_.focused = true;
                nameField_.focused = false;
                tick();
                return 0;
            }
            if (search_.focused) { search_.focused = false; tick(); }
        }
        if (crumbClick(x, y)) return 0;

        // The three at the left edge, then the toolbar capsules.
        for (int i = 0; i < 3; ++i) {
            const auto b = navButton(i);
            if (x < b.left || x > b.right || y < b.top || y > b.bottom) continue;
            if (i == 0) toggleSidebar();
            else if (i == 1) goBack();
            else goForward();
            return 0;
        }
        for (int i = 0; i < 3; ++i) {
            const auto b = capsuleButton(i);
            if (x < b.left || x > b.right || y < b.top || y > b.bottom) continue;
            if (i == 0)      showSortMenu();
            else if (i == 1) { if (!act::share(hwnd_, targetPath())) showMoreMenu(); }
            else             showMoreMenu();
            return 0;
        }

        // tabs
        if (!picker_ && y < M::kTitleBar) {
            // The close button is inside the tab, so it has to be tested first.
            if (tabs_.size() > 1)
                for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
                    if (i != activeTab_ && i != hotTab_) continue;
                    const auto cb = tabClose(i);
                    if (x >= cb.left && x <= cb.right && y >= cb.top && y <= cb.bottom) {
                        closeTab(i);
                        return 0;
                    }
                }
            for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
                const auto b = tabRect(i);
                if (x >= b.left && x <= b.right && y >= b.top) {
                    if (i != activeTab_) { activeTab_ = i; scrollX_ = 0; tick(); }
                    return 0;
                }
            }
            const auto last = tabRect(static_cast<int>(tabs_.size()) - 1);
            if (x >= last.right + 6 && x <= last.right + 32 && y >= last.top) {
                const auto* c = model().deepestListed();
                addTab(c ? c->place : Place::thisPC());
                return 0;
            }
        }

        if (x <= sideWidth() && y > M::kTitleBar + M::kToolBar) {
            sidebarClick(x, y);
            return 0;
        }

        // a column divider: grab it rather than the row behind it
        if (view_ == View::Column) {
            const auto area = contentArea();
            for (const auto& cr : columnLayout(area)) {
                if (cr.depth >= model().columns().size()) break;
                const float edge = cr.x + cr.w;
                if (std::fabs(x - edge) <= M::kGripW * 0.5f && y > area.top + M::kToolBar) {
                    dragDivider_ = static_cast<int>(cr.depth);
                    dragStartX_ = x;
                    dragStartW_ = setColumnW_;
                    SetCapture(hwnd);
                    return 0;
                }
            }
        }

        for (int i = 0; i < 3; ++i) {
            const auto b = switcherButton(i);
            if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) {
                setView(static_cast<View>(i));
                return 0;
            }
        }
        {   // the gear, just left of the view switcher
            const auto s = switcherButton(0);
            if (x >= s.left - 40 && x <= s.left - 8 && y >= s.top && y <= s.bottom) {
                toggleInspector();
                return 0;
            }
        }

        if (view_ != View::Column) {
            int index = 0;
            if (hitTestFlat(x, y, &index)) {
                flatSelected_ = index;
                render();
                const auto* c = currentFolder();
                if (c) scheduleMenuPrepare(childOf(c->place, c->listing.entries[index]).path);
            }
            return 0;
        }

        size_t depth = 0; int index = 0;
        if (hitTestRow(x, y, &depth, &index)) {
            // Ctrl adds one, Shift takes the run between, neither replaces.
            // The modifiers come from the message, not from GetKeyState: wParam
            // records what was held when the click happened, which is what the
            // user meant. GetKeyState answers about now, and now is later.
            const bool ctrlDown  = (wp & MK_CONTROL) != 0;
            const bool shiftDown = (wp & MK_SHIFT)   != 0;
            if (shiftDown && depth < model().columns().size())
                model().extendSelect(hwnd, depth, index,
                                     visibleRows(model().columns()[depth]));
            else if (ctrlDown) model().toggleSelect(hwnd, depth, index);
            else               model().select(hwnd, depth, index);
            scrollColumnsToEnd(contentArea().right - contentArea().left);
            scrollSelectionIntoView();
            tick();     // starts the frame timer the arrival animation needs
            scheduleMenuPrepare(model().selectedFullPath());
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        tracking_ = false;
        if (hotChrome_ || hotTab_ >= 0 || sideHover_ >= 0 || hoverIndex_ >= 0 ||
            hotCrumb_ >= 0) {
            hotChrome_ = 0; hotTab_ = -1; sideHover_ = -1; hoverIndex_ = -1;
            hotCrumb_ = -1;
            tick();
        }
        return 0;

    case WM_TIMER:
        if (wp == 1) { tick(); return 0; }
        if (wp == 2) {
            KillTimer(hwnd, 2);
            // Never build while anything is still moving: this call blocks.
            if (viewThumb_.active() || inspW_.active() || sidePill_.active() ||
                columnsAnimating() || tabsAnimating() || fades_.active()) {
                SetTimer(hwnd, 2, 60, nullptr);
                return 0;
            }
            PostMessageW(hwnd, WM_ORIEL_PREP_MENU, 0, 0);
            return 0;
        }
        if (wp == 3) {   // the panel is moving; bring its contents up now
            KillTimer(hwnd, 3);
            inspInk_.to(1.0f, M::kMsInk);
            tick();
            return 0;
        }
        if (wp == 4) { KillTimer(hwnd, 4); saveSettings(); return 0; }
        if (wp == 5) { KillTimer(hwnd, 5); warmShellHandlers(); return 0; }
        break;

    case WM_RBUTTONDOWN: {
        SetFocus(hwnd);
        LARGE_INTEGER freq{}, t0{}, t1{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);

        const float x = dips(GET_X_LPARAM(lp)), y = dips(GET_Y_LPARAM(lp));
        size_t depth = 0; int index = 0;
        if (hitTestRow(x, y, &depth, &index)) {
            model().select(hwnd, depth, index);
            scrollSelectionIntoView();
            render();
        }
        // Right-clicking past the last row is a click on the folder, not on
        // nothing: it is how you make something here.
        if (!hitTestRow(x, y, &depth, &index)) {
            POINT bg{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(hwnd, &bg);
            showFolderMenu(bg);
            return 0;
        }

        const std::wstring path = model().selectedFullPath();
        if (path.empty()) {
            POINT bg{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(hwnd, &bg);
            showFolderMenu(bg);
            return 0;
        }

        // Almost always already true; the fallback only runs if the user beat
        // the idle build to it.
        const bool prebuilt = shellMenu_.readyFor(path);
        double buildMs = 0;
        if (!prebuilt) shellMenu_.prepare(hwnd, path, &buildMs);

        QueryPerformanceCounter(&t1);
        const double toShowMs =
            freq.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart) : 0;
        logf("context menu: %s, click->show %.2f ms%s",
             prebuilt ? "prebuilt" : "built on demand", toShowMs,
             prebuilt ? "" : " (fallback)");

        // Our own commands go above the shell's, so the right-click and the
        // toolbar offer the same words for the same things.
        if (HMENU m = shellMenu_.menuHandle()) {
            shellMenu_.clearOwnItems();   // the menu is cached; ours would pile up
            bool cut = false;
            const bool clip = !act::clipboardPaths(hwnd, &cut).empty();
            int at = 0;
            auto ins = [&](int id, const wchar_t* label, bool on) {
                MENUITEMINFOW mi{ sizeof(mi) };
                mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
                mi.wID = static_cast<UINT>(id);
                mi.dwTypeData = const_cast<wchar_t*>(label);
                mi.fState = on ? MFS_ENABLED : MFS_DISABLED;
                InsertMenuItemW(m, at++, TRUE, &mi);
            };
            auto sep = [&] {
                MENUITEMINFOW mi{ sizeof(mi) };
                mi.fMask = MIIM_FTYPE; mi.fType = MFT_SEPARATOR;
                InsertMenuItemW(m, at++, TRUE, &mi);
            };
            ins(cmd::Open,      T(L"開く"), true);
            ins(cmd::Rename,    T(L"名前を変更"), true);
            ins(cmd::Duplicate, T(L"複製"), true);
            sep();
            ins(cmd::CopyItem,  T(L"コピー"), true);
            ins(cmd::CutItem,   T(L"切り取り"), true);
            ins(cmd::Paste,     T(L"貼り付け"), clip && !targetFolder().empty());
            ins(cmd::CopyPath,  T(L"パスをコピー"), true);
            sep();
            ins(cmd::Trash,     T(L"ごみ箱に入れる"), true);
            sep();
        }

        if (HMENU m = shellMenu_.menuHandle())
            logf("context menu items: %d", GetMenuItemCount(m));

        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hwnd, &pt);
        const int chosen = shellMenu_.show(hwnd, pt);
        // Ours came back as an id; the shell's were already invoked inside.
        if (chosen >= ShellMenu::kOwnFirst && chosen < ShellMenu::kOwnLast)
            runCommand(chosen);
        else if (chosen)
            refreshVisible();   // the shell may well have changed the folder
        return 0;
    }

    case WM_ORIEL_PICK: {
        auto* job = reinterpret_cast<PickJob*>(lp);
        if (!job) return 0;
        PickRequest req;
        req.save = job->save;
        req.owner = job->owner;
        req.folder = job->folder;
        req.fileName = job->fileName;
        for (const auto& t : job->types) req.types.push_back(FileType{ t.first, t.second });
        req.typeIndex = job->typeIndex;

        PickReply reply;
        const bool ran = runPicker(hinst_, req, &reply);
        job->served = ran;
        job->cancelled = reply.cancelled;
        job->path = reply.path;
        logf("picker served=%d cancelled=%d path=%ls", ran ? 1 : 0,
             reply.cancelled ? 1 : 0, reply.path.c_str());
        SetEvent(job->done);          // the server thread owns the reply from here
        return 0;
    }

    case WM_ORIEL_THUMB_READY:
        onThumbReady(reinterpret_cast<ThumbBits*>(lp));
        return 0;

    case WM_ORIEL_PREP_MENU: {
        const std::wstring path = pendingMenuPath_;
        if (path.empty() || shellMenu_.readyFor(path)) return 0;
        double ms = 0;
        if (shellMenu_.prepare(hwnd, path, &ms))
            logf("menu prebuilt in %.2f ms for %ls", ms, path.c_str());
        else
            logf("menu prebuild FAILED for %ls", path.c_str());
        return 0;
    }

    // Shell extensions with submenus own their own drawing.
    case WM_INITMENUPOPUP:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_MENUCHAR: {
        LRESULT r = 0;
        if (shellMenu_.handleMenuMessage(msg, wp, lp, &r)) return r;
        break;
    }

    // Betting on the hovered row, not just the selected one: people right-click
    // rows they have not selected, and that is exactly the case a
    // selection-only prebuild would miss.
    case WM_LBUTTONUP:
        if (inspDragSlider_ >= 0) { inspDragSlider_ = -1; ReleaseCapture(); return 0; }
        if (dragDivider_ >= 0)    { dragDivider_ = -1;    ReleaseCapture(); return 0; }
        break;

    case WM_MOUSEMOVE: {
        const float x = dips(GET_X_LPARAM(lp)), y = dips(GET_Y_LPARAM(lp));
        if (inspDragSlider_ >= 0) { inspectorHit(x, y, true); return 0; }
        if (dragDivider_ >= 0) {
            // Uniform, like the bench: one divider sets the width for all of
            // them, which is the value that actually gets remembered.
            setColumnW_ = std::clamp(dragStartW_ + (x - dragStartX_) / (dragDivider_ + 1),
                                     150.0f, 300.0f);
            settingsChanged();
            render();
            return 0;
        }
        // Arm the leave notification once, so chrome lets go when the pointer
        // exits the window instead of staying lit.
        if (!tracking_) {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            tracking_ = true;
        }

        // hover feedback across every surface that has rows
        {
            const int newChrome = chromeHit(x, y);
            if (newChrome != hotChrome_) { hotChrome_ = newChrome; tick(); }

            int newCrumb = -1;
            {
                RECT rcc{}; GetClientRect(hwnd, &rcc);
                const float bot = dips(rcc.bottom - rcc.top) - M::kPathBar
                                - (picker_ ? actionBarHeight() : 0.0f);
                if (y >= bot && y <= bot + M::kPathBar)
                    for (size_t k = 0; k < crumbs_.size(); ++k)
                        if (x >= crumbs_[k].x && x <= crumbs_[k].x + crumbs_[k].w) {
                            newCrumb = static_cast<int>(k); break;
                        }
            }
            if (newCrumb != hotCrumb_) { hotCrumb_ = newCrumb; tick(); }

            int newTab = -1;
            if (!picker_ && y < M::kTitleBar)
                for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
                    const auto b = tabRect(i);
                    if (x >= b.left && x <= b.right && y >= b.top) { newTab = i; break; }
                }
            if (newTab != hotTab_) { hotTab_ = newTab; tick(); }

            int newClose = -1;
            if (!picker_ && y < M::kTitleBar && tabs_.size() > 1)
                for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
                    if (i != activeTab_ && i != hotTab_) continue;
                    const auto cb = tabClose(i);
                    if (x >= cb.left && x <= cb.right && y >= cb.top && y <= cb.bottom) {
                        newClose = i; break;
                    }
                }
            if (newClose != hotTabClose_) { hotTabClose_ = newClose; tick(); }

            int newSide = -1;
            if (x <= sideWidth() && y > M::kTitleBar) {
                float ry = M::kTitleBar + M::kToolBar;
                int flat = 0;
                for (int s = 0; s < 3 && newSide < 0; ++s) {
                    ry += 22;
                    for (int i = 0; i < kSideCounts[s]; ++i, ++flat) {
                        if (y >= ry && y < ry + setRowHeight_) { newSide = flat; break; }
                        ry += setRowHeight_;
                    }
                    ry += 11;
                }
            }
            if (newSide != sideHover_) { sideHover_ = newSide; tick(); }

            size_t hd = 0; int hi = -1;
            if (view_ == View::Column) hitTestRow(x, y, &hd, &hi);
            setHover(hd, hi);
        }

        if (view_ == View::Column && !picker_) {
            const auto area = contentArea();
            bool onEdge = false;
            for (const auto& cr : columnLayout(area)) {
                if (cr.depth >= model().columns().size()) break;
                if (std::fabs(x - (cr.x + cr.w)) <= M::kGripW * 0.5f &&
                    y > area.top + M::kToolBar) { onEdge = true; break; }
            }
            SetCursor(LoadCursorW(nullptr, onEdge ? IDC_SIZEWE : IDC_ARROW));
        }
        if (picker_) {
            int hot = -1;
            for (int i = 0; i < 2; ++i) {
                const auto b = actionButton(i);
                if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) { hot = i; break; }
            }
            if (hot != hotAction_) { hotAction_ = hot; tick(); }
        }
        size_t depth = 0; int index = 0;
        if (hitTestRow(x, y, &depth, &index))
            scheduleMenuPrepare(model().pathAt(depth, index));
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        const float x = dips(pt.x), y = dips(pt.y);
        const auto area = contentArea();
        const float delta = -static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA
                          * setRowHeight_ * 3.0f;

        if (view_ != View::Column) {
            const auto* c = currentFolder();
            if (!c) return 0;
            const int n = static_cast<int>(c->listing.entries.size());
            const float viewH = (area.bottom - area.top) - M::kToolBar - 24;
            float total;
            if (view_ == View::List) {
                total = n * setRowHeight_;
            } else {
                const int perRow = std::max(1, static_cast<int>((area.right - area.left - 14) / 112.0f));
                total = ((n + perRow - 1) / perRow) * 96.0f + 20.0f;
            }
            flatScrollY_ = std::clamp(flatScrollY_ + delta, 0.0f, std::max(0.0f, total - viewH));
            render();
            return 0;
        }

        auto& cols = model().columnsMut();
        for (const auto& cr : columnLayout(area)) {
            if (cr.depth >= cols.size()) break;
            if (x < cr.x || x >= cr.x + cr.w) continue;
            auto& c = cols[cr.depth];
            const float viewH = (area.bottom - area.top) - M::kToolBar - 12;
            const float maxScroll =
                std::max(0.0f, c.listing.entries.size() * setRowHeight_ - viewH);
            c.scrollY = std::clamp(c.scrollY + delta, 0.0f, maxScroll);
            render();
            break;
        }
        (void)y;
        return 0;
    }

    case WM_CHAR: {
        // A field takes printable input before anything else does.
        TextField* f = focusedField();
        if (f && f->ch(static_cast<wchar_t>(wp))) { tick(); return 0; }
        return 0;
    }

    // Anything held with Alt arrives here, not as WM_KEYDOWN. Handling the Alt
    // combinations in WM_KEYDOWN meant back, forward, parent and properties
    // were never reached at all.
    case WM_SYSKEYDOWN:
        if (!picker_ && !focusedField()) {
            switch (wp) {
            case VK_LEFT:   goBack();    return 0;
            case VK_RIGHT:  goForward(); return 0;
            case VK_UP:     model().collapseOne(hwnd); tick(); return 0;
            case VK_RETURN: runCommand(cmd::Properties); return 0;
            default: break;
            }
        }
        break;

    case WM_KEYDOWN: {
        const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

        if (TextField* f = focusedField()) {
            if (wp == VK_ESCAPE) {
                if (renaming_)          cancelRename();
                else if (f == &search_) setSearch(false);
                else { f->focused = false; tick(); }
                return 0;
            }
            if (wp == VK_RETURN) {
                if (renaming_) { commitRename(); return 0; }
                if (picker_ && !pickerResult().empty()) { finishPicker(false); return 0; }
                f->focused = false; tick(); return 0;
            }
            if (wp == VK_TAB) { f->focused = false; tick(); return 0; }
            if (f->key(wp, shift, ctrl)) { tick(); return 0; }
            // Arrows fall through to the list so it can still be driven while
            // a name is half-typed; everything else the field did not want is
            // dropped rather than firing a shortcut mid-word.
            if (wp != VK_UP && wp != VK_DOWN) return 0;
        }

        if (ctrl && (wp == 'F')) {
            setSearch(true); search_.focused = true; search_.selectAll();
            tick();
            return 0;
        }
        // The command vocabulary, on the keys the habits expect.
        if (!picker_) {
            if (wp == VK_F5)            { runCommand(cmd::Refresh);   return 0; }
            if (wp == VK_DELETE)        { runCommand(cmd::Trash);     return 0; }
            if (wp == VK_SPACE)         { setPreview_ = !setPreview_; settingsChanged(); tick(); return 0; }
            if (ctrl && shift && wp == 'C') { runCommand(cmd::CopyPath);  return 0; }
            if (ctrl && shift && wp == 'N') { runCommand(cmd::NewFolder); return 0; }
            if (ctrl && wp == 'T') { addTab(model().deepestListed()
                                        ? model().deepestListed()->place : Place::thisPC());
                                     return 0; }
            if (ctrl && wp == 'W') { closeTab(activeTab_); return 0; }
            if (ctrl && !shift) {
                switch (wp) {
                case 'C': runCommand(cmd::CopyItem);  return 0;
                case 'X': runCommand(cmd::CutItem);   return 0;
                case 'V': runCommand(cmd::Paste);     return 0;
                case 'D': runCommand(cmd::Duplicate); return 0;
                case 'I': runCommand(cmd::Info);      return 0;
                case 'R': runCommand(cmd::Refresh);   return 0;
                case 'H': runCommand(cmd::ToggleHidden); return 0;
                default: break;
                }
            }
        }
        if (picker_) {
            if (wp == VK_ESCAPE) { finishPicker(true); return 0; }
            if (wp == VK_RETURN && !pickerResult().empty()) {
                finishPicker(false);
                return 0;
            }
        }
        if (wp == VK_OEM_COMMA && ctrl) {
            toggleInspector();
            return 0;
        }
        // The two schemes differ only here, which is the whole reason the choice
        // exists: Enter means "rename" to one set of habits and "open" to the
        // other, and guessing wrong destroys work.
        if (!picker_) {
            if (wp == VK_F2) { beginRename(); return 0; }
            if (wp == VK_RETURN) {
                if (setOrielKeys_) beginRename();
                else               openSelected();
                return 0;
            }
            if (wp == VK_DOWN && ctrl && setOrielKeys_) { openSelected(); return 0; }
        }
        // Tagging: Ctrl+1..4 toggles a tag on the selected item.
        if (ctrl && wp >= '1' && wp <= '0' + kTagCount) {
            const std::wstring sel = model().selectedFullPath();
            if (!sel.empty()) { tags_.toggle(sel, static_cast<int>(wp - '1')); tick(); }
            return 0;
        }
        if (ctrl && wp == 'A') { runCommand(cmd::SelectAll); return 0; }

        bool handled = true;
        switch (wp) {
        case VK_UP:    moveSelectionVisible(-1, shift); break;
        case VK_DOWN:  moveSelectionVisible(+1, shift); break;
        case VK_LEFT:  model().collapseOne(hwnd);       break;
        case VK_RIGHT: model().expandSelected(hwnd);    break;
        default: handled = false;
        }
        if (handled) {
            scrollColumnsToEnd(contentArea().right - contentArea().left);
            scrollSelectionIntoView();
            tick();
            scheduleMenuPrepare(model().selectedFullPath());
            return 0;
        }
        break;
    }

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            resizeSwapChain(LOWORD(lp), HIWORD(lp));
            scrollColumnsToEnd(contentArea().right - contentArea().left);
            render();
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
        render();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (lp && !lstrcmpiW(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet")) {
            // Only the parts still set to follow the system move. Rebuilding
            // from the system unconditionally is how an explicit choice used to
            // get quietly overruled the next time Windows changed its mind.
            applyTheme();
            render();
        }
        break;

    case WM_ERASEBKGND:
        return 1;                       // never let GDI flash a background in

    case WM_CLOSE:
        if (picker_) { finishPicker(true); return 0; }
        break;

    case WM_DESTROY:
        // Written unconditionally, not only when a setting changed: the window
        // placement and the folder in view are part of the session and neither
        // goes through settingsChanged().
        if (!picker_) { KillTimer(hwnd, 4); saveSettings(); }
        thumbs_.stop();          // join before the D2D device goes away
        // A picker runs in a nested loop inside the resident app; quitting here
        // would take the whole process down with it.
        if (!picker_) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace oriel
