#pragma once
#include "i18n.h"
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <utility>

namespace oriel {

// Delivered to the UI thread; the server thread waits on `done` for the answer.
constexpr UINT WM_ORIEL_PICK = WM_APP + 4;

struct PickJob {
    bool save = true;
    HWND owner = nullptr;
    std::wstring folder;
    std::wstring fileName;
    // label, spec - e.g. {T(L"画像"), L"*.png;*.jpg"}
    std::vector<std::pair<std::wstring, std::wstring>> types;
    int typeIndex = 0;

    // filled in by the UI thread
    bool served = false;      // false means "we declined; use the genuine dialog"
    bool cancelled = true;
    std::wstring path;
    HANDLE done = nullptr;
};

// Listens for shims asking for a picker. One request at a time is deliberate:
// two pickers on screen at once would be worse than a queue.
class DialogServer {
public:
    ~DialogServer() { stop(); }
    bool start(HWND ui);
    void stop();

private:
    void serve();

    std::thread worker_;
    HWND ui_ = nullptr;
    std::atomic<bool> quit_{ false };
    HANDLE wake_ = nullptr;   // used to break a blocking ConnectNamedPipe
};

} // namespace oriel
