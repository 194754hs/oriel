#pragma once
#include "enumerate.h"
#include <memory>
#include <algorithm>

namespace oriel {

// Posted to the owning window when a worker finishes a directory.
constexpr UINT WM_ORIEL_ENUM_DONE = WM_APP + 1;

struct EnumResult {
    uint64_t token = 0;
    size_t   depth = 0;
    Place    place;
    Listing  listing;
};

// The stack of columns the view walks. Enumeration always happens on a thread
// pool worker; the UI thread only ever reads what has already arrived.
class ColumnModel {
public:
    struct Column {
        Place    place;
        Listing  listing;
        // `selected` is the lead: the row the preview follows and the one a
        // deeper column opens from. `sel` is every selected row, sorted, and
        // always contains the lead. One selection behaves exactly as before.
        int      selected = -1;
        std::vector<int> sel;
        int      anchor   = -1;  // where a shift-range measures from
        bool     loading  = true;
        uint64_t token    = 0;
        float    scrollY  = 0;   // view state, but it belongs to this column
        ULONGLONG appeared = 0;  // when it was pushed, for the arrival animation

        bool isSelected(int i) const {
            return std::binary_search(sel.begin(), sel.end(), i);
        }
    };

    void setRoot(HWND owner, Place place);
    // A root whose rows the caller already has. Used by the tag views, whose
    // contents come from our own store rather than from a directory.
    void setRootListing(Place place, Listing listing);
    // Choose row `index` in column `depth`; deeper columns are discarded and,
    // for a folder, a new one is requested.
    void select(HWND owner, size_t depth, int index);
    // Ctrl-click: add or remove one row without disturbing the rest.
    void toggleSelect(HWND owner, size_t depth, int index);
    // Shift-click: everything between the anchor and here, among `visible` so a
    // filtered-out row is never silently swept into the selection.
    void extendSelect(HWND owner, size_t depth, int index, const std::vector<int>& visible);
    void setSelection(HWND owner, size_t depth, std::vector<int> rows, int lead);

    // Every selected item, deepest column first. Empty when nothing is chosen.
    std::vector<std::wstring> selectedPaths() const;
    size_t selectionCount() const;
    void moveSelection(HWND owner, int delta);
    void collapseOne(HWND owner);       // left arrow / back
    void expandSelected(HWND owner);    // right arrow

    // Takes ownership of the pointer posted by the worker.
    bool onEnumDone(std::unique_ptr<EnumResult>);

    const std::vector<Column>& columns() const { return cols_; }
    std::vector<Column>& columnsMut() { return cols_; }
    size_t activeDepth() const { return cols_.empty() ? 0 : cols_.size() - 1; }

    // Non-null when the deepest selection is a file, i.e. the preview has content.
    const Entry* previewEntry() const;
    const Column* deepestListed() const;
    // Full path of the deepest selected row, for the shell to act on.
    std::wstring selectedFullPath() const;
    std::wstring pathAt(size_t depth, int index) const;
    std::vector<std::wstring> breadcrumb() const;

    // Re-reads one column in place, keeping the columns to its right.
    void refresh(HWND owner, size_t depth) { request(owner, depth); }
    void refreshAll(HWND owner) { for (size_t i = 0; i < cols_.size(); ++i) request(owner, i); }

    // Re-orders what is already loaded. Selection follows the item, not the
    // index, or re-sorting would silently select something else.
    void setSort(SortKey key, bool descending);
    SortKey sortKey() const { return sortKey_; }
    bool sortDescending() const { return sortDesc_; }

    void setShowHidden(HWND owner, bool v);
    bool showHidden() const { return showHidden_; }

private:
    void request(HWND owner, size_t depth);

    std::vector<Column> cols_;
    uint64_t nextToken_ = 1;
    bool showHidden_ = false;
    SortKey sortKey_ = SortKey::Name;
    bool sortDesc_ = false;
};

} // namespace oriel
