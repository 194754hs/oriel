#include "app_window.h"
#include <string>

// `oriel.exe [folder]` — opening at a given folder is how the shell will hand
// work to us later, so it is wired up from the start.
static std::wstring startFolderFrom(LPWSTR cmdLine) {
    if (!cmdLine || !*cmdLine) return {};
    std::wstring s = cmdLine;
    while (!s.empty() && (s.front() == L' ' || s.front() == L'"')) s.erase(s.begin());
    while (!s.empty() && (s.back()  == L' ' || s.back()  == L'"')) s.pop_back();
    if (s.empty()) return {};
    const DWORD attrs = GetFileAttributesW(s.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        return {};
    return s;
}

int APIENTRY wWinMain(HINSTANCE hinst, HINSTANCE, LPWSTR cmdLine, int nCmdShow) {
    // Per-monitor v2 so the frame, the caption buttons and the text all rescale
    // together when the window crosses to a display at a different scale.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    oriel::AppWindow app;
    if (!app.create(hinst, nCmdShow, startFolderFrom(cmdLine))) { CoUninitialize(); return 2; }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
