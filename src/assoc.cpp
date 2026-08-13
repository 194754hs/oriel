#include "assoc.h"

#include <windows.h>
#include <shlwapi.h>
#include <unordered_map>
#include <cwctype>

namespace oriel {
namespace {

// Types whose icon is inside the file rather than registered for the type, so
// two executables in the same folder show their own faces.
bool iconLivesInTheFile(const std::wstring& ext) {
    static const wchar_t* kSelf[] = { L"exe", L"lnk", L"scr", L"cpl", L"msc", L"ico", L"url" };
    for (const wchar_t* e : kSelf) if (ext == e) return true;
    return false;
}

// Windows registers icons for plenty of types it handles itself - text, zip,
// generic documents. Those are the ones our own family says better, and mixing
// two icon languages for the same idea is what makes a file list look assembled.
// So the test is not "is an icon registered" but "does something other than
// Windows own this type".
bool underWindowsDir(const std::wstring& path) {
    wchar_t win[MAX_PATH]{};
    const UINT n = GetWindowsDirectoryW(win, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    std::wstring w(win, n);
    if (path.size() < w.size()) return false;
    return CompareStringOrdinal(path.c_str(), static_cast<int>(w.size()),
                                w.c_str(), static_cast<int>(w.size()),
                                TRUE) == CSTR_EQUAL;
}

Mark decide(const std::wstring& ext) {
    if (ext.empty()) return Mark::Glyph;
    if (iconLivesInTheFile(ext)) return Mark::FileIcon;

    const std::wstring dotted = L"." + ext;
    DWORD cch = 0;
    if (FAILED(AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_DEFAULTICON,
                                 dotted.c_str(), nullptr, nullptr, &cch)) || !cch)
        return Mark::Glyph;

    std::wstring raw(cch, L'\0');
    if (FAILED(AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_DEFAULTICON,
                                 dotted.c_str(), nullptr, raw.data(), &cch)))
        return Mark::Glyph;
    raw.resize(wcsnlen(raw.c_str(), raw.size()));
    if (raw.empty()) return Mark::Glyph;

    // "module,index" - the index is irrelevant here, only who owns the module.
    std::wstring module = raw;
    if (const size_t comma = module.find_last_of(L','); comma != std::wstring::npos)
        module.erase(comma);
    if (!module.empty() && module.front() == L'"') module.erase(0, 1);
    if (!module.empty() && module.back() == L'"') module.pop_back();
    if (module.empty()) return Mark::Glyph;

    // "%1" means the icon is inside whichever file is being shown.
    if (module == L"%1") return Mark::FileIcon;

    wchar_t expanded[MAX_PATH * 2]{};
    if (ExpandEnvironmentStringsW(module.c_str(), expanded,
                                  static_cast<DWORD>(std::size(expanded))))
        module = expanded;

    return underWindowsDir(module) ? Mark::Glyph : Mark::TypeIcon;
}

std::unordered_map<std::wstring, Mark>& memo() {
    // UI thread only, so no lock. Bounded by the number of distinct extensions
    // the user actually looks at.
    static std::unordered_map<std::wstring, Mark> m;
    return m;
}

} // namespace

Mark markForExt(const std::wstring& ext) {
    auto& m = memo();
    if (const auto it = m.find(ext); it != m.end()) return it->second;
    const Mark v = decide(ext);
    m.emplace(ext, v);
    return v;
}

void forgetAssociations() { memo().clear(); }

} // namespace oriel
