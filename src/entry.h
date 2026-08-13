#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace oriel {

struct Entry {
    std::wstring name;
    // Set only when the row does not live inside its place's directory - a tag
    // view gathers files from everywhere, so the name alone cannot locate them.
    std::wstring source;
    bool      isDir  = false;
    uint64_t  size   = 0;
    FILETIME  written{};
    DWORD     attrs  = 0;

    bool hidden() const {
        return (attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
    }
};

// "—" for folders, otherwise the largest unit that keeps the number short.
std::wstring formatSize(const Entry&);
// yyyy/MM/dd in local time.
std::wstring formatDate(const FILETIME&);
// Kind, derived from the extension. Deliberately our own vocabulary.
std::wstring formatKind(const Entry&);

} // namespace oriel
