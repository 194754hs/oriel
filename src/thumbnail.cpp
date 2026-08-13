#include "thumbnail.h"
#include "log.h"

#include <shlobj.h>
#include <wrl/client.h>

namespace oriel {

using Microsoft::WRL::ComPtr;

bool ThumbnailSource::start() {
    if (worker_.joinable()) return true;
    worker_ = std::thread(&ThumbnailSource::workerMain, this);
    return true;
}

void ThumbnailSource::stop() {
    if (!worker_.joinable()) return;
    { std::lock_guard<std::mutex> lk(m_); quit_ = true; queue_.clear(); }
    cv_.notify_all();
    worker_.join();
}

void ThumbnailSource::request(HWND owner, std::wstring key, std::wstring path,
                              int pixels, bool iconOnly) {
    if (path.empty() || pixels <= 0) return;
    {
        std::lock_guard<std::mutex> lk(m_);
        queue_.push_back(Req{ owner, std::move(key), std::move(path), pixels,
                              iconOnly, generation_ });
        // A backlog only ever holds stale work; the newest few are what matter.
        while (queue_.size() > 32) queue_.pop_front();
    }
    cv_.notify_one();
}

void ThumbnailSource::discardPending() {
    std::lock_guard<std::mutex> lk(m_);
    ++generation_;
    queue_.clear();
}

void ThumbnailSource::workerMain() {
    // Thumbnail providers are third-party COM written against an STA.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return;

    for (;;) {
        Req req;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return quit_ || !queue_.empty(); });
            if (quit_) break;
            // LIFO: serve what the user asked for most recently first.
            req = std::move(queue_.back());
            queue_.pop_back();
            if (req.gen != generation_) continue;      // superseded while waiting
        }
        if (ThumbBits* bits = fetch(req)) {
            if (!PostMessageW(req.owner, WM_ORIEL_THUMB_READY, 0,
                              reinterpret_cast<LPARAM>(bits)))
                delete bits;
        }
    }
    CoUninitialize();
}

ThumbBits* ThumbnailSource::fetch(const Req& req) {
    ComPtr<IShellItem> item;
    HRESULT hrItem = SHCreateItemFromParsingName(req.path.c_str(), nullptr,
                                                 IID_PPV_ARGS(&item));
    if (FAILED(hrItem)) {
        logf("thumb: SHCreateItemFromParsingName 0x%08X for %ls", hrItem, req.path.c_str());
        return nullptr;
    }
    ComPtr<IShellItemImageFactory> factory;
    HRESULT hrFac = item.As(&factory);
    if (FAILED(hrFac)) { logf("thumb: no IShellItemImageFactory 0x%08X", hrFac); return nullptr; }

    const SIZE size{ req.px, req.px };
    HBITMAP hbm = nullptr;
    bool isIcon = false;

    // Ask for a real thumbnail first; fall back to the registered type icon so
    // that documents and folders still get something to show. Row-sized requests
    // skip straight to the icon: a 16px document preview is a grey smudge.
    HRESULT hr = E_FAIL;
    if (!req.iconOnly)
        hr = factory->GetImage(size, SIIGBF_RESIZETOFIT | SIIGBF_THUMBNAILONLY, &hbm);
    if (FAILED(hr) || !hbm) {
        hr = factory->GetImage(size, SIIGBF_RESIZETOFIT | SIIGBF_ICONONLY, &hbm);
        isIcon = true;
    }
    if (FAILED(hr) || !hbm) {
        logf("thumb: GetImage failed 0x%08X for %ls", hr, req.path.c_str());
        return nullptr;
    }

    DIBSECTION ds{};
    const int got = GetObjectW(hbm, sizeof(ds), &ds);
    if (got != sizeof(ds) || !ds.dsBm.bmBits) {
        logf("thumb: not a DIB section (GetObject returned %d, want %d, bits=%p)",
             got, static_cast<int>(sizeof(ds)), ds.dsBm.bmBits);
        DeleteObject(hbm);
        return nullptr;
    }

    const int w = ds.dsBm.bmWidth;
    const int h = ds.dsBm.bmHeight;
    const int srcStride = ds.dsBm.bmWidthBytes;
    // A negative biHeight means the rows are already top-down.
    const bool topDown = ds.dsBmih.biHeight < 0;
    if (w <= 0 || h <= 0 || ds.dsBm.bmBitsPixel != 32) {
        logf("thumb: unexpected format %dx%d bpp=%d", w, h, ds.dsBm.bmBitsPixel);
        DeleteObject(hbm);
        return nullptr;
    }
    logf("thumb: %dx%d %s%s for %ls", w, h, topDown ? "top-down" : "bottom-up",
         isIcon ? " (icon)" : "", req.path.c_str());

    auto* out = new ThumbBits{};
    out->key = req.key;
    out->path = req.path;
    out->w = w;
    out->h = h;
    out->isIcon = isIcon;
    out->bgra.resize(static_cast<size_t>(w) * h * 4);

    const auto* src = static_cast<const uint8_t*>(ds.dsBm.bmBits);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = topDown ? src + static_cast<size_t>(y) * srcStride
                                     : src + static_cast<size_t>(h - 1 - y) * srcStride;
        memcpy(out->bgra.data() + static_cast<size_t>(y) * w * 4, row,
               static_cast<size_t>(w) * 4);
    }
    DeleteObject(hbm);
    return out;
}

} // namespace oriel
