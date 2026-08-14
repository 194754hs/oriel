#include "i18n.h"
#include "actions.h"
#include "log.h"

#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wrl/client.h>

namespace oriel::act {

using Microsoft::WRL::ComPtr;

namespace {

// The shell wants one buffer of paths, each null-terminated, the whole thing
// terminated by a second null.
std::vector<wchar_t> doubleNullList(const std::vector<std::wstring>& paths) {
    std::vector<wchar_t> buf;
    for (const auto& p : paths) {
        buf.insert(buf.end(), p.begin(), p.end());
        buf.push_back(L'\0');
    }
    buf.push_back(L'\0');
    if (buf.size() == 1) buf.push_back(L'\0');   // empty list still needs the pair
    return buf;
}

bool setClipboardData(HWND owner, UINT format, const void* data, size_t bytes) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) return false;
    if (void* dst = GlobalLock(h)) {
        memcpy(dst, data, bytes);
        GlobalUnlock(h);
    } else {
        GlobalFree(h);
        return false;
    }
    if (!SetClipboardData(format, h)) { GlobalFree(h); return false; }
    (void)owner;
    return true;   // the clipboard owns it now
}

UINT dropEffectFormat() {
    static const UINT f = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    return f;
}

// One place that runs a shell file operation, so the flags stay consistent.
bool runOperation(HWND owner, UINT op, const std::vector<std::wstring>& from,
                  const std::wstring& to, FILEOP_FLAGS flags) {
    auto src = doubleNullList(from);
    auto dst = doubleNullList(to.empty() ? std::vector<std::wstring>{}
                                         : std::vector<std::wstring>{ to });
    SHFILEOPSTRUCTW fo{};
    fo.hwnd   = owner;
    fo.wFunc  = op;
    fo.pFrom  = src.data();
    fo.pTo    = to.empty() ? nullptr : dst.data();
    fo.fFlags = flags;
    const int r = SHFileOperationW(&fo);
    if (r != 0) logf("file operation %u failed: %d", op, r);
    return r == 0 && !fo.fAnyOperationsAborted;
}

} // namespace

bool copyToClipboard(HWND owner, const std::vector<std::wstring>& paths, bool cut) {
    if (paths.empty() || !OpenClipboard(owner)) return false;
    EmptyClipboard();

    auto list = doubleNullList(paths);
    const size_t bytes = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
    std::vector<uint8_t> blob(bytes, 0);
    auto* df = reinterpret_cast<DROPFILES*>(blob.data());
    df->pFiles = sizeof(DROPFILES);
    df->fWide  = TRUE;
    memcpy(blob.data() + sizeof(DROPFILES), list.data(), list.size() * sizeof(wchar_t));

    bool ok = setClipboardData(owner, CF_HDROP, blob.data(), blob.size());
    // Cut is not a separate clipboard format: it is a copy plus this hint, and
    // it is what tells the destination to remove the original.
    const DWORD effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    if (ok) ok = setClipboardData(owner, dropEffectFormat(), &effect, sizeof(effect));

    // The names as text too, so pasting into a terminal or an editor works.
    std::wstring text;
    for (const auto& p : paths) { if (!text.empty()) text += L"\r\n"; text += p; }
    if (!text.empty())
        setClipboardData(owner, CF_UNICODETEXT, text.c_str(),
                         (text.size() + 1) * sizeof(wchar_t));

    CloseClipboard();
    return ok;
}

bool copyTextToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(owner)) return false;
    EmptyClipboard();
    const bool ok = setClipboardData(owner, CF_UNICODETEXT, text.c_str(),
                                     (text.size() + 1) * sizeof(wchar_t));
    CloseClipboard();
    return ok;
}

std::vector<std::wstring> clipboardPaths(HWND owner, bool* wasCut) {
    std::vector<std::wstring> out;
    if (wasCut) *wasCut = false;
    if (!OpenClipboard(owner)) return out;

    if (HANDLE h = GetClipboardData(CF_HDROP)) {
        auto drop = static_cast<HDROP>(h);
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            const UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring p(len, L'\0');
            if (DragQueryFileW(drop, i, p.data(), len + 1)) out.push_back(p);
        }
    }
    if (wasCut) {
        if (HANDLE h = GetClipboardData(dropEffectFormat())) {
            if (const auto* e = static_cast<const DWORD*>(GlobalLock(h))) {
                *wasCut = (*e & DROPEFFECT_MOVE) != 0;
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    return out;
}

bool pasteInto(HWND owner, const std::wstring& into) {
    bool cut = false;
    const auto paths = clipboardPaths(owner, &cut);
    if (paths.empty() || into.empty()) return false;
    // Pasting into the folder the files already live in should duplicate, not
    // fail; the shell handles that by renaming, which is the behaviour to keep.
    return runOperation(owner, cut ? FO_MOVE : FO_COPY, paths, into,
                        FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR);
}

bool moveToRecycleBin(HWND owner, const std::vector<std::wstring>& paths, bool confirm) {
    if (paths.empty()) return false;
    FILEOP_FLAGS flags = FOF_ALLOWUNDO;      // ALLOWUNDO is what means "recycle"
    if (!confirm) flags |= FOF_NOCONFIRMATION;
    return runOperation(owner, FO_DELETE, paths, {}, flags);
}

bool duplicate(HWND owner, const std::wstring& path) {
    if (path.empty()) return false;
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return false;
    const std::wstring dir  = path.substr(0, slash);
    const std::wstring name = path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    const bool isDir = (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const std::wstring stem = (isDir || dot == std::wstring::npos || dot == 0)
                            ? name : name.substr(0, dot);
    const std::wstring ext  = (isDir || dot == std::wstring::npos || dot == 0)
                            ? L"" : name.substr(dot);

    for (int i = 2; i < 1000; ++i) {
        std::wstring candidate = dir + L"\\" + stem +
            (i == 2 ? T(L" のコピー") : (T(L" のコピー ") + std::to_wstring(i))) + ext;
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        return runOperation(owner, FO_COPY, { path }, candidate,
                            FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR);
    }
    return false;
}

std::wstring newFolder(HWND owner, const std::wstring& parent) {
    if (parent.empty()) return {};
    for (int i = 1; i < 1000; ++i) {
        const std::wstring name = (i == 1) ? T(L"新規フォルダ")
                                           : (T(L"新規フォルダ ") + std::to_wstring(i));
        const std::wstring full = parent + L"\\" + name;
        if (CreateDirectoryW(full.c_str(), nullptr)) return name;
        if (GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    logf("newFolder failed in %ls: %lu", parent.c_str(), GetLastError());
    (void)owner;
    return {};
}

bool createShortcut(HWND owner, const std::wstring& path) {
    if (path.empty()) return false;
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link))))
        return false;
    link->SetPath(path.c_str());
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return false;
    const std::wstring dir = path.substr(0, slash);
    link->SetWorkingDirectory(dir.c_str());

    ComPtr<IPersistFile> file;
    if (FAILED(link.As(&file))) return false;
    const std::wstring stem = path.substr(slash + 1);
    for (int i = 1; i < 1000; ++i) {
        std::wstring dest = dir + L"\\" + stem +
            (i == 1 ? T(L" - ショートカット") : (T(L" - ショートカット (") + std::to_wstring(i) + L")")) + L".lnk";
        if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        const bool ok = SUCCEEDED(file->Save(dest.c_str(), TRUE));
        if (!ok) logf("createShortcut failed for %ls", path.c_str());
        return ok;
    }
    (void)owner;
    return false;
}

bool showProperties(HWND owner, const std::wstring& path) {
    if (path.empty()) return false;
    SHELLEXECUTEINFOW ei{};
    ei.cbSize = sizeof(ei);
    ei.fMask  = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    ei.hwnd   = owner;
    ei.lpVerb = L"properties";
    ei.lpFile = path.c_str();
    ei.nShow  = SW_SHOW;
    return ShellExecuteExW(&ei) != FALSE;
}

bool share(HWND owner, const std::wstring& path) {
    if (path.empty()) return false;
    // The system share sheet is reachable as a verb on the item, so there is no
    // need to take a WinRT dependency for one button. Types without a share
    // handler simply fail, which is the caller's cue to say so.
    SHELLEXECUTEINFOW ei{};
    ei.cbSize = sizeof(ei);
    ei.fMask  = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    ei.hwnd   = owner;
    ei.lpVerb = L"Windows.ModernShare";
    ei.lpFile = path.c_str();
    ei.nShow  = SW_SHOW;
    if (ShellExecuteExW(&ei)) return true;
    logf("share verb unavailable for %ls (%lu)", path.c_str(), GetLastError());
    return false;
}

bool openWith(HWND owner, const std::wstring& path) {
    if (path.empty()) return false;
    OPENASINFO info{};
    info.pcszFile = path.c_str();
    info.oaifInFlags = OAIF_EXEC | OAIF_ALLOW_REGISTRATION;
    (void)owner;
    return SUCCEEDED(SHOpenWithDialog(owner, &info));
}

bool revealInExplorer(HWND owner, const std::wstring& path) {
    if (path.empty()) return false;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
    if (!pidl) return false;
    const HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ILFree(pidl);
    (void)owner;
    return SUCCEEDED(hr);
}

} // namespace oriel::act
