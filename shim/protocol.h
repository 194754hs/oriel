// Wire format between the in-proc shim and the resident Oriel process.
//
// Deliberately dumb: fixed header, length-prefixed UTF-16 strings, no
// versioning cleverness beyond a number to refuse on mismatch. Anything the
// shim cannot parse means "fall back to the genuine dialog", which is always a
// working outcome.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace oriel::proto {

inline constexpr uint32_t kMagic   = 0x4C45'4952;   // 'RIEL'
inline constexpr uint32_t kVersion = 1;

enum class Mode : uint32_t { Open = 0, Save = 1 };

enum class Status : uint32_t {
    Picked   = 0,   // the user chose something; `path` is filled in
    Cancelled= 1,   // the user dismissed the picker
    Decline  = 2,   // Oriel does not want this one - use the genuine dialog
};

struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t mode;        // Mode
    uint32_t reserved;
    uint64_t owner;       // HWND of the window the dialog belongs to
};

// The pipe is per-session so two logged-in users never cross.
inline std::wstring pipeName() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    wchar_t buf[64];
    swprintf_s(buf, L"\\\\.\\pipe\\oriel.dialog.%lu", session);
    return buf;
}

// ── framing ────────────────────────────────────────────────────────
inline void putString(std::vector<uint8_t>& out, const std::wstring& s) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    const auto* p = reinterpret_cast<const uint8_t*>(&n);
    out.insert(out.end(), p, p + sizeof(n));
    const auto* c = reinterpret_cast<const uint8_t*>(s.data());
    out.insert(out.end(), c, c + n * sizeof(wchar_t));
}

inline void putU32(std::vector<uint8_t>& out, uint32_t v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    out.insert(out.end(), p, p + sizeof(v));
}

inline bool getU32(const uint8_t*& p, const uint8_t* end, uint32_t* out) {
    if (static_cast<size_t>(end - p) < sizeof(uint32_t)) return false;
    memcpy(out, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    return true;
}

inline bool getString(const uint8_t*& p, const uint8_t* end, std::wstring* out) {
    if (static_cast<size_t>(end - p) < sizeof(uint32_t)) return false;
    uint32_t n = 0;
    memcpy(&n, p, sizeof(n));
    p += sizeof(n);
    if (n > (1u << 20)) return false;                       // refuse absurd lengths
    const size_t bytes = static_cast<size_t>(n) * sizeof(wchar_t);
    if (static_cast<size_t>(end - p) < bytes) return false;
    out->assign(reinterpret_cast<const wchar_t*>(p), n);
    p += bytes;
    return true;
}

} // namespace oriel::proto
