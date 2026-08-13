#include "enumerate.h"

#include <shlwapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cwchar>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace oriel {

Place Place::directory(std::wstring p) {
    // trailing separator only survives on a drive root ("C:\"), where it matters
    if (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    Place pl;
    pl.kind = PlaceKind::Directory;
    pl.path = p;
    const size_t slash = p.find_last_of(L'\\');
    pl.label = (slash == std::wstring::npos) ? p : p.substr(slash + 1);
    if (pl.label.empty()) pl.label = p;     // drive root
    return pl;
}

Place Place::thisPC() {
    Place pl;
    pl.kind = PlaceKind::ThisPC;
    pl.label = L"この PC";
    return pl;
}

Place childOf(const Place& parent, const Entry& e) {
    if (!e.source.empty()) return Place::directory(e.source);
    if (parent.kind == PlaceKind::ThisPC) return Place::directory(e.name);
    std::wstring p = parent.path;
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    return Place::directory(p + e.name);
}

std::wstring knownFolder(const GUID& id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) {
        out = p;
        CoTaskMemFree(p);
    }
    return out;
}

std::wstring documentsFolder() { return knownFolder(FOLDERID_Documents); }
std::wstring profileFolder()   { return knownFolder(FOLDERID_Profile); }

// Folders first, then a natural-order compare so "10" sorts after "9".
static bool byFolderThenName(const Entry& a, const Entry& b) {
    if (a.isDir != b.isDir) return a.isDir;
    return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
}

static Listing listDrives() {
    Listing out;
    wchar_t buf[512]{};
    const DWORD n = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(buf)), buf);
    if (!n || n > std::size(buf)) { out.error = GetLastError(); return out; }
    for (const wchar_t* d = buf; *d; d += wcslen(d) + 1) {
        Entry e;
        e.name  = d;                       // "C:\"
        e.isDir = true;
        e.attrs = FILE_ATTRIBUTE_DIRECTORY;
        out.entries.push_back(std::move(e));
    }
    return out;
}

Listing listPlace(const Place& place, bool showHidden) {
    if (place.kind == PlaceKind::ThisPC) return listDrives();

    Listing out;
    if (place.path.empty()) { out.error = ERROR_PATH_NOT_FOUND; return out; }

    std::wstring pattern = place.path;
    if (pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    WIN32_FIND_DATAW fd{};
    // FindExInfoBasic skips the 8.3 alternate name and LARGE_FETCH batches the
    // reads — together they are the difference between this and the shell's
    // COM enumerator on directories with thousands of entries.
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        // An empty directory is not a failure.
        if (err != ERROR_FILE_NOT_FOUND) out.error = err;
        return out;
    }

    out.entries.reserve(128);
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == L'\0' ||
             (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0')))
            continue;

        Entry e;
        e.attrs = fd.dwFileAttributes;
        if (!showHidden && (e.attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
            continue;
        e.name    = fd.cFileName;
        e.isDir   = (e.attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.written = fd.ftLastWriteTime;
        e.size    = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        out.entries.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    std::sort(out.entries.begin(), out.entries.end(), byFolderThenName);
    return out;
}

// ── formatting ─────────────────────────────────────────────────────
std::wstring formatSize(const Entry& e) {
    if (e.isDir) return L"—";
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double v = static_cast<double>(e.size);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    wchar_t buf[64];
    if (u == 0)      swprintf_s(buf, L"%llu B", static_cast<unsigned long long>(e.size));
    else if (v < 10) swprintf_s(buf, L"%.1f %s", v, units[u]);
    else             swprintf_s(buf, L"%.0f %s", v, units[u]);
    return buf;
}

std::wstring formatDate(const FILETIME& ft) {
    if (!ft.dwLowDateTime && !ft.dwHighDateTime) return L"—";
    FILETIME local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st))
        return L"—";
    wchar_t buf[32];
    swprintf_s(buf, L"%04u/%02u/%02u", st.wYear, st.wMonth, st.wDay);
    return buf;
}

std::wstring formatKind(const Entry& e) {
    if (e.isDir) return L"フォルダ";
    const size_t dot = e.name.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"ファイル";
    std::wstring ext = e.name.substr(dot + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

    struct { const wchar_t* ext; const wchar_t* kind; } table[] = {
        { L"txt", L"テキスト" },   { L"md",  L"Markdown" },
        { L"pdf", L"PDF 書類" },   { L"doc", L"文書" },      { L"docx", L"文書" },
        { L"xls", L"表計算" },     { L"xlsx", L"表計算" },
        { L"ppt", L"プレゼン" },   { L"pptx", L"プレゼン" },
        { L"png", L"PNG 画像" },   { L"jpg", L"JPEG 画像" }, { L"jpeg", L"JPEG 画像" },
        { L"gif", L"GIF 画像" },   { L"svg", L"SVG 画像" },  { L"webp", L"WebP 画像" },
        { L"mp4", L"動画" },       { L"mov", L"動画" },      { L"mkv", L"動画" },
        { L"mp3", L"オーディオ" }, { L"m4a", L"オーディオ" }, { L"wav", L"オーディオ" },
        { L"zip", L"圧縮フォルダ" }, { L"7z", L"圧縮フォルダ" }, { L"rar", L"圧縮フォルダ" },
        { L"exe", L"アプリケーション" }, { L"dll", L"ライブラリ" },
        { L"cpp", L"ソースコード" }, { L"h", L"ソースコード" },
        { L"cs",  L"ソースコード" }, { L"ts", L"ソースコード" },
        { L"js",  L"ソースコード" }, { L"py", L"ソースコード" },
        { L"html", L"HTML 書類" },  { L"css", L"スタイルシート" },
        { L"json", L"JSON" },       { L"log", L"ログ" },
    };
    for (const auto& t : table)
        if (ext == t.ext) return t.kind;

    for (auto& c : ext) c = static_cast<wchar_t>(towupper(c));
    return ext + L" ファイル";
}

void sortEntries(std::vector<Entry>& v, SortKey key, bool descending) {
    auto byName = [](const Entry& a, const Entry& b) {
        return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
    };
    std::stable_sort(v.begin(), v.end(), [&](const Entry& a, const Entry& b) {
        // Folders lead in every order, and reversing the order must not put
        // them at the bottom: "sort by size, descending" still means folders
        // first, then the biggest file.
        if (a.isDir != b.isDir) return a.isDir;

        int cmp = 0;
        switch (key) {
        case SortKey::Modified:
            cmp = CompareFileTime(&a.written, &b.written);
            break;
        case SortKey::Size:
            cmp = (a.size < b.size) ? -1 : (a.size > b.size) ? 1 : 0;
            break;
        case SortKey::Kind: {
            const std::wstring ka = formatKind(a), kb = formatKind(b);
            cmp = StrCmpLogicalW(ka.c_str(), kb.c_str());
            break;
        }
        case SortKey::Name:
        default:
            cmp = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            break;
        }
        if (cmp == 0) return byName(a, b);          // stable, predictable ties
        return descending ? cmp > 0 : cmp < 0;
    });
}

} // namespace oriel
