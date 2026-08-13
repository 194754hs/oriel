#pragma once
#include <windows.h>
#include <string>
#include <algorithm>

namespace oriel {

// A single-line editable string.
//
// Deliberately small: no undo stack, no IME composition, no rich selection
// gestures. A file name box and a search box are all the shell needs, and a
// half-built text engine is worse than an obviously simple one.
struct TextField {
    std::wstring text;
    size_t caret  = 0;
    size_t anchor = 0;          // the selection runs between anchor and caret
    bool   focused = false;

    bool   hasSelection() const { return anchor != caret; }
    size_t selLo() const { return anchor < caret ? anchor : caret; }
    size_t selHi() const { return anchor < caret ? caret : anchor; }

    void set(std::wstring s) {
        text = std::move(s);
        caret = text.size();
        anchor = caret;
    }
    void selectAll() { anchor = 0; caret = text.size(); }
    void clear() { text.clear(); caret = anchor = 0; }

    void eraseSelection() {
        if (!hasSelection()) return;
        const size_t lo = selLo();
        text.erase(lo, selHi() - lo);
        caret = anchor = lo;
    }

    // Printable input. Control characters are left to key().
    bool ch(wchar_t c) {
        if (c < 0x20 || c == 0x7F) return false;
        eraseSelection();
        text.insert(text.begin() + static_cast<ptrdiff_t>(caret), c);
        ++caret;
        anchor = caret;
        return true;
    }

    // Returns true when the field consumed the key.
    bool key(WPARAM vk, bool shift, bool ctrl) {
        auto moved = [&](size_t to) {
            caret = to;
            if (!shift) anchor = caret;
            return true;
        };
        switch (vk) {
        case VK_LEFT:
            if (!shift && hasSelection()) return moved(selLo());
            return moved(caret ? caret - 1 : 0);
        case VK_RIGHT:
            if (!shift && hasSelection()) return moved(selHi());
            return moved(caret < text.size() ? caret + 1 : caret);
        case VK_HOME: return moved(0);
        case VK_END:  return moved(text.size());
        case VK_BACK:
            if (hasSelection()) { eraseSelection(); return true; }
            if (caret == 0) return true;
            text.erase(text.begin() + static_cast<ptrdiff_t>(caret - 1));
            --caret; anchor = caret;
            return true;
        case VK_DELETE:
            if (hasSelection()) { eraseSelection(); return true; }
            if (caret >= text.size()) return true;
            text.erase(text.begin() + static_cast<ptrdiff_t>(caret));
            return true;
        case 'A':
            if (ctrl) { selectAll(); return true; }
            return false;
        default:
            return false;
        }
    }
};

} // namespace oriel
