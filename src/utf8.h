#pragma once
#include <windows.h>
#include <string>

namespace oriel {

// Our own small files (tags, settings) are UTF-8 so they stay readable and
// repairable in any editor. std::codecvt_utf8 would do this but has been
// deprecated since C++17, and the Win32 pair is what it wrapped anyway.
inline std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

inline std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// %LOCALAPPDATA%\Oriel, created if missing. Empty on failure.
std::wstring appDataDir();

} // namespace oriel
