#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

namespace oriel {

constexpr UINT WM_ORIEL_THUMB_READY = WM_APP + 3;

// Decoded pixels handed to the UI thread. Ownership transfers with the message.
struct ThumbBits {
    std::wstring key;                 // what the UI files this under
    std::wstring path;                // what was actually read
    int  w = 0, h = 0;
    bool isIcon = false;              // no real thumbnail existed; this is the type icon
    std::vector<uint8_t> bgra;        // premultiplied, top-down, stride = w * 4
};

// Runs shell thumbnail providers on a dedicated apartment.
//
// Unlike the context menu, this genuinely belongs off the UI thread: each
// request builds its own short-lived COM objects that the UI never touches, so
// there is nothing to marshal and nothing shared to corrupt. A provider that
// takes a second on a huge video then costs nothing but a late repaint.
class ThumbnailSource {
public:
    ~ThumbnailSource() { stop(); }

    bool start();
    void stop();

    // Newest request wins: the user is looking at what they asked for last.
    //
    // `key` is what the result gets filed under, which is not always the path:
    // a type icon is the same for every file with that extension, so it is asked
    // for once using any one of them as the sample and cached under ".blend".
    // `iconOnly` skips the thumbnail attempt entirely - at row size a document
    // preview is unreadable, and the application's own mark says more.
    void request(HWND owner, std::wstring key, std::wstring path, int pixels,
                 bool iconOnly);

    // Everything queued before this call becomes uninteresting.
    void discardPending();

private:
    struct Req { HWND owner; std::wstring key, path; int px; bool iconOnly; uint64_t gen; };

    void workerMain();
    ThumbBits* fetch(const Req&);

    std::thread worker_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Req> queue_;
    uint64_t generation_ = 1;
    bool quit_ = false;
};

} // namespace oriel
