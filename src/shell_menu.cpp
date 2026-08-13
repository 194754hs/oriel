#include "shell_menu.h"
#include <shellapi.h>   // CMIC_MASK_UNICODE expands to SEE_MASK_UNICODE

namespace oriel {

namespace {
// Room for the shell and its extensions to number their verbs.
constexpr UINT kFirstCmd = 0x0100;
constexpr UINT kLastCmd  = 0x7000;
}

void ShellMenu::reset() {
    if (hmenu_) { DestroyMenu(hmenu_); hmenu_ = nullptr; }
    menu3_.Reset();
    menu2_.Reset();
    menu_.Reset();
    path_.clear();
}

bool ShellMenu::prepare(HWND owner, const std::wstring& fullPath, double* elapsedMs) {
    if (readyFor(fullPath)) return true;
    reset();
    if (fullPath.empty()) return false;

    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(fullPath.c_str(), nullptr, &pidl, 0, nullptr)))
        return false;

    Microsoft::WRL::ComPtr<IShellFolder> parent;
    PCUITEMID_CHILD child = nullptr;
    HRESULT hr = SHBindToParent(pidl, IID_PPV_ARGS(&parent), &child);
    if (SUCCEEDED(hr))
        hr = parent->GetUIObjectOf(owner, 1, &child, IID_IContextMenu, nullptr,
                                   reinterpret_cast<void**>(menu_.GetAddressOf()));
    CoTaskMemFree(pidl);
    if (FAILED(hr) || !menu_) { reset(); return false; }

    hmenu_ = CreatePopupMenu();
    if (!hmenu_) { reset(); return false; }

    // CMF_EXTENDEDVERBS would add the shift-only entries; keep the normal set.
    hr = menu_->QueryContextMenu(hmenu_, 0, kFirstCmd, kLastCmd, CMF_NORMAL);
    if (FAILED(hr)) { reset(); return false; }

    // Optional, but without them any extension with a submenu draws nothing.
    menu_.As(&menu2_);
    menu_.As(&menu3_);

    path_ = fullPath;
    QueryPerformanceCounter(&t1);
    if (elapsedMs && freq.QuadPart)
        *elapsedMs = double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart);
    return true;
}

int ShellMenu::show(HWND owner, POINT pt) {
    if (!menu_ || !hmenu_) return 0;

    const int cmd = TrackPopupMenuEx(
        hmenu_, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, owner, nullptr);
    // Our own commands are inserted above the shell's and numbered outside its
    // range, so they come back here to be handed to the caller rather than
    // being invoked as a shell verb that does not exist.
    if (cmd >= kOwnFirst && cmd < kOwnLast) return cmd;
    if (cmd < static_cast<int>(kFirstCmd)) return 0;

    CMINVOKECOMMANDINFOEX info{};
    info.cbSize  = sizeof(info);
    info.fMask   = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
    info.hwnd    = owner;
    info.lpVerb  = MAKEINTRESOURCEA(cmd - kFirstCmd);
    info.lpVerbW = MAKEINTRESOURCEW(cmd - kFirstCmd);
    info.nShow   = SW_SHOWNORMAL;
    info.ptInvoke = pt;
    menu_->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
    return cmd;
}

bool ShellMenu::handleMenuMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
    if (menu3_) {
        LRESULT r = 0;
        if (SUCCEEDED(menu3_->HandleMenuMsg2(msg, wp, lp, &r))) {
            if (result) *result = r;
            return true;
        }
    }
    if (menu2_) {
        if (SUCCEEDED(menu2_->HandleMenuMsg(msg, wp, lp))) {
            if (result) *result = 0;
            return true;
        }
    }
    return false;
}

} // namespace oriel
