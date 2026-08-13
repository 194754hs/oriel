#pragma once
#include <windows.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <string>

namespace oriel {

// The shell context menu for one item, built ahead of the click.
//
// Deliberately single-threaded: QueryContextMenu loads third-party shell
// extension DLLs, and those are written against the caller's apartment. Moving
// them off the UI thread is where file managers acquire their crash reports.
// Instead the expensive part runs while the user is still reading the row they
// just selected, so the right-click itself only has to show what already exists.
class ShellMenu {
public:
    // Our commands share the popup with the shell's. They are numbered below
    // the shell's range so the two can never be confused for one another.
    static constexpr int kOwnFirst = 0x3000;
    static constexpr int kOwnLast  = 0x3100;

    ~ShellMenu() { reset(); }

    // Inserts caller-owned items at the top, before the shell's own.
    //
    // The menu is built once and reused for as long as the path is unchanged,
    // so anything the caller added has to be removed before adding it again -
    // otherwise showing the same menu twice shows every custom item twice.
    HMENU menuHandle() const { return hmenu_; }
    void clearOwnItems() {
        if (!hmenu_) return;
        for (int id = kOwnFirst; id < kOwnLast; ++id) DeleteMenu(hmenu_, id, MF_BYCOMMAND);
        // Leading separators are left behind when the items above them go.
        while (GetMenuItemCount(hmenu_) > 0) {
            MENUITEMINFOW mi{ sizeof(mi) };
            mi.fMask = MIIM_FTYPE;
            if (!GetMenuItemInfoW(hmenu_, 0, TRUE, &mi)) break;
            if (!(mi.fType & MFT_SEPARATOR)) break;
            DeleteMenu(hmenu_, 0, MF_BYPOSITION);
        }
    }

    // Builds for `fullPath`. Returns false if the item cannot be bound.
    // `elapsedMs` reports how long the shell took, for the record.
    bool prepare(HWND owner, const std::wstring& fullPath, double* elapsedMs = nullptr);

    // True when a prepared menu matches this path and can be shown immediately.
    bool readyFor(const std::wstring& fullPath) const {
        return menu_ && hmenu_ && path_ == fullPath;
    }

    // Shows the menu and invokes whatever was chosen. Returns the chosen id.
    int show(HWND owner, POINT screenPt);

    // Shell extensions with submenus need these forwarded.
    bool handleMenuMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);

    void reset();

private:
    Microsoft::WRL::ComPtr<IContextMenu>  menu_;
    Microsoft::WRL::ComPtr<IContextMenu2> menu2_;
    Microsoft::WRL::ComPtr<IContextMenu3> menu3_;
    HMENU hmenu_ = nullptr;
    std::wstring path_;
};

} // namespace oriel
