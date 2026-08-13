#include "column_model.h"
#include <algorithm>

namespace oriel {

namespace {

struct Job {
    HWND     owner;
    uint64_t token;
    size_t   depth;
    Place    place;
    bool     showHidden;
};

// A private pool with a hard ceiling.
//
// The default process pool grows a thread for every callback that blocks, and
// clicking quickly through folders blocks a great many: measured 234 threads
// after two minutes of it, with the stacks showing up as steadily rising
// memory. Directory reads are I/O bound, so a handful of them in flight is
// already as fast as this gets; the rest were pure overhead.
struct EnumPool {
    PTP_POOL pool = nullptr;
    TP_CALLBACK_ENVIRON env{};
    EnumPool() {
        InitializeThreadpoolEnvironment(&env);
        pool = CreateThreadpool(nullptr);
        if (!pool) return;                  // fall back to the default pool
        SetThreadpoolThreadMinimum(pool, 1);
        SetThreadpoolThreadMaximum(pool, 4);
        SetThreadpoolCallbackPool(&env, pool);
    }
};
// Deliberately never destroyed: closing a pool while callbacks are still in
// flight is a race, and the process is going away regardless.
EnumPool& enumPool() {
    static EnumPool* p = new EnumPool();
    return *p;
}

VOID CALLBACK runJob(PTP_CALLBACK_INSTANCE, PVOID ctx) {
    std::unique_ptr<Job> job(static_cast<Job*>(ctx));
    auto* result = new EnumResult{};
    result->token = job->token;
    result->depth = job->depth;
    result->place = job->place;
    result->listing = listPlace(job->place, job->showHidden);
    // The window takes ownership; if the post fails nobody will, so clean up.
    if (!PostMessageW(job->owner, WM_ORIEL_ENUM_DONE, 0,
                      reinterpret_cast<LPARAM>(result)))
        delete result;
}

} // namespace

void ColumnModel::request(HWND owner, size_t depth) {
    if (depth >= cols_.size()) return;
    Column& c = cols_[depth];
    // A tag view has no directory to read: its rows were assembled by the
    // window from our own store. Re-reading it would blank it, which is what
    // used to happen on every refresh, rename or hidden-files toggle.
    if (c.place.kind == PlaceKind::Tag) return;
    c.loading = true;
    c.token = nextToken_++;
    c.listing = Listing{};

    auto* job = new Job{ owner, c.token, depth, c.place, showHidden_ };
    EnumPool& ep = enumPool();
    if (!TrySubmitThreadpoolCallback(runJob, job, ep.pool ? &ep.env : nullptr)) {
        // Falling back to the calling thread is worse than a stutter but far
        // better than a column that never loads.
        delete job;
        c.listing = listPlace(c.place, showHidden_);
        c.loading = false;
    }
}

void ColumnModel::setRoot(HWND owner, Place place) {
    cols_.clear();
    cols_.push_back(Column{ std::move(place) });
    request(owner, 0);
}

void ColumnModel::setRootListing(Place place, Listing listing) {
    cols_.clear();
    Column c{ std::move(place) };
    c.listing = std::move(listing);
    c.loading = false;
    c.token = nextToken_++;      // so any in-flight worker result is discarded
    c.appeared = GetTickCount64();
    cols_.push_back(std::move(c));
}

void ColumnModel::setSelection(HWND owner, size_t depth, std::vector<int> rows, int lead) {
    if (depth >= cols_.size()) return;
    Column& c = cols_[depth];
    const int count = static_cast<int>(c.listing.entries.size());

    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [&](int i) { return i < 0 || i >= count; }),
               rows.end());
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    c.sel = std::move(rows);
    if (c.sel.empty()) { c.selected = -1; }
    else if (std::binary_search(c.sel.begin(), c.sel.end(), lead)) c.selected = lead;
    else c.selected = c.sel.front();

    cols_.resize(depth + 1);                       // drop everything to the right

    // A deeper column only makes sense for exactly one folder. With several
    // rows chosen there is no single "next", and pushing one would contradict
    // the selection the user is looking at.
    if (c.sel.size() == 1 && c.selected >= 0) {
        const Entry& e = c.listing.entries[c.selected];
        if (e.isDir) {
            Column next{ childOf(c.place, e) };
            next.appeared = GetTickCount64();   // the view slides it in from here
            cols_.push_back(std::move(next));
            request(owner, depth + 1);
        }
    }
}

void ColumnModel::select(HWND owner, size_t depth, int index) {
    setSelection(owner, depth, { index }, index);
    if (depth < cols_.size()) cols_[depth].anchor = index;
}

void ColumnModel::toggleSelect(HWND owner, size_t depth, int index) {
    if (depth >= cols_.size()) return;
    std::vector<int> rows = cols_[depth].sel;
    const auto it = std::find(rows.begin(), rows.end(), index);
    int lead = index;
    if (it != rows.end()) {
        rows.erase(it);
        // Removing the lead hands the role to whatever is left.
        lead = rows.empty() ? -1 : rows.front();
    } else {
        rows.push_back(index);
    }
    setSelection(owner, depth, std::move(rows), lead);
    if (depth < cols_.size()) cols_[depth].anchor = index;
}

void ColumnModel::extendSelect(HWND owner, size_t depth, int index,
                               const std::vector<int>& visible) {
    if (depth >= cols_.size()) return;
    Column& c = cols_[depth];
    const int from = (c.anchor >= 0) ? c.anchor : index;

    // Walk the visible order, not the raw indices: with a filter on, the rows
    // between two clicks are the ones on screen, not the ones in the listing.
    int a = -1, b = -1;
    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i] == from)  a = static_cast<int>(i);
        if (visible[i] == index) b = static_cast<int>(i);
    }
    std::vector<int> rows;
    if (a < 0 || b < 0) {
        rows.push_back(index);
    } else {
        if (a > b) std::swap(a, b);
        for (int i = a; i <= b; ++i) rows.push_back(visible[static_cast<size_t>(i)]);
    }
    const int keepAnchor = c.anchor;
    setSelection(owner, depth, std::move(rows), index);
    if (depth < cols_.size()) cols_[depth].anchor = keepAnchor;   // shift keeps it
}

std::vector<std::wstring> ColumnModel::selectedPaths() const {
    for (size_t i = cols_.size(); i-- > 0; ) {
        const Column& c = cols_[i];
        if (c.sel.empty()) continue;
        std::vector<std::wstring> out;
        out.reserve(c.sel.size());
        for (int idx : c.sel)
            if (idx >= 0 && idx < static_cast<int>(c.listing.entries.size()))
                out.push_back(childOf(c.place, c.listing.entries[idx]).path);
        return out;
    }
    return {};
}

size_t ColumnModel::selectionCount() const {
    for (size_t i = cols_.size(); i-- > 0; )
        if (!cols_[i].sel.empty()) return cols_[i].sel.size();
    return 0;
}

void ColumnModel::moveSelection(HWND owner, int delta) {
    if (cols_.empty()) return;
    // Move within the deepest column that actually has rows.
    size_t depth = cols_.size() - 1;
    while (depth > 0 && cols_[depth].listing.entries.empty()) --depth;
    Column& c = cols_[depth];
    if (c.listing.entries.empty()) return;

    const int count = static_cast<int>(c.listing.entries.size());
    const int next = (c.selected < 0)
        ? (delta > 0 ? 0 : count - 1)
        : std::clamp(c.selected + delta, 0, count - 1);
    select(owner, depth, next);
}

void ColumnModel::collapseOne(HWND owner) {
    if (cols_.size() <= 1) return;
    cols_.pop_back();
    // The parent keeps its selection; nothing needs re-reading.
    (void)owner;
}

void ColumnModel::expandSelected(HWND owner) {
    if (cols_.empty()) return;
    const size_t depth = cols_.size() - 1;
    const Column& c = cols_[depth];
    if (c.listing.entries.empty()) return;
    if (c.selected < 0) { select(owner, depth, 0); return; }
    const Entry& e = c.listing.entries[c.selected];
    if (e.isDir && depth + 1 < cols_.size()) select(owner, depth + 1, 0);
}

bool ColumnModel::onEnumDone(std::unique_ptr<EnumResult> r) {
    if (!r || r->depth >= cols_.size()) return false;
    Column& c = cols_[r->depth];
    // Stale: the user moved on while this was reading.
    if (c.token != r->token) return false;
    c.listing = std::move(r->listing);
    sortEntries(c.listing.entries, sortKey_, sortDesc_);
    c.loading = false;
    return true;
}

void ColumnModel::setSort(SortKey key, bool descending) {
    if (sortKey_ == key && sortDesc_ == descending) return;
    sortKey_ = key;
    sortDesc_ = descending;
    for (auto& c : cols_) {
        // Hold on to what was selected by name, because its index is about to
        // mean something else entirely.
        // Every selected row, by name: after the sort those indices point at
        // different files, so keeping the numbers would select strangers.
        std::vector<std::wstring> chosen;
        for (int idx : c.sel)
            if (idx >= 0 && idx < static_cast<int>(c.listing.entries.size()))
                chosen.push_back(c.listing.entries[idx].name);
        std::wstring lead;
        if (c.selected >= 0 && c.selected < static_cast<int>(c.listing.entries.size()))
            lead = c.listing.entries[c.selected].name;

        sortEntries(c.listing.entries, sortKey_, sortDesc_);

        c.sel.clear();
        c.selected = -1;
        c.anchor = -1;
        for (size_t i = 0; i < c.listing.entries.size(); ++i) {
            const std::wstring& n = c.listing.entries[i].name;
            if (std::find(chosen.begin(), chosen.end(), n) != chosen.end())
                c.sel.push_back(static_cast<int>(i));
            if (!lead.empty() && n == lead) c.selected = static_cast<int>(i);
        }
        if (c.selected < 0 && !c.sel.empty()) c.selected = c.sel.front();
        c.anchor = c.selected;
    }
}

const ColumnModel::Column* ColumnModel::deepestListed() const {
    for (size_t i = cols_.size(); i-- > 0; )
        if (!cols_[i].loading) return &cols_[i];
    return cols_.empty() ? nullptr : &cols_.front();
}

const Entry* ColumnModel::previewEntry() const {
    if (cols_.empty()) return nullptr;
    const Column& c = cols_.back();
    // A preview of one of several chosen files would be arbitrary. The count
    // is what matters once there is more than one, and the path bar says it.
    if (c.sel.size() > 1) return nullptr;
    if (c.selected < 0 || c.selected >= static_cast<int>(c.listing.entries.size()))
        return nullptr;
    const Entry& e = c.listing.entries[c.selected];
    return e.isDir ? nullptr : &e;
}

std::wstring ColumnModel::selectedFullPath() const {
    for (size_t i = cols_.size(); i-- > 0; ) {
        const Column& c = cols_[i];
        if (c.selected >= 0 && c.selected < static_cast<int>(c.listing.entries.size()))
            return childOf(c.place, c.listing.entries[c.selected]).path;
    }
    return {};
}

std::wstring ColumnModel::pathAt(size_t depth, int index) const {
    if (depth >= cols_.size()) return {};
    const Column& c = cols_[depth];
    if (index < 0 || index >= static_cast<int>(c.listing.entries.size())) return {};
    return childOf(c.place, c.listing.entries[index]).path;
}

std::vector<std::wstring> ColumnModel::breadcrumb() const {
    std::vector<std::wstring> out;
    if (cols_.empty()) return out;
    out.push_back(cols_.front().place.label);
    for (const Column& c : cols_) {
        if (c.selected < 0) break;
        if (c.selected >= static_cast<int>(c.listing.entries.size())) break;
        out.push_back(c.listing.entries[c.selected].name);
    }
    return out;
}

void ColumnModel::setShowHidden(HWND owner, bool v) {
    if (showHidden_ == v) return;
    showHidden_ = v;
    for (size_t i = 0; i < cols_.size(); ++i) request(owner, i);
}

} // namespace oriel
