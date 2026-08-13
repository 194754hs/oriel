#pragma once
#include "entry.h"

namespace oriel {

// A place the column view can show. Most are real directories; the roots are
// synthetic because there is no single path that lists the drives.
// Tag is synthetic in a second sense: its rows come from our own store, not from
// any one directory, so the listing is built on the UI thread and handed to the
// model rather than being read by a worker.
enum class PlaceKind { Directory, ThisPC, Tag };

struct Place {
    PlaceKind kind = PlaceKind::Directory;
    std::wstring path;          // empty for ThisPC and Tag
    std::wstring label;         // what the path bar and toolbar show
    int tag = -1;               // Tag only

    static Place directory(std::wstring p);
    static Place thisPC();
    static Place forTag(int index, std::wstring label) {
        Place p; p.kind = PlaceKind::Tag; p.tag = index; p.label = std::move(label);
        return p;
    }
    bool operator==(const Place& o) const {
        return kind == o.kind && path == o.path && tag == o.tag;
    }
};

// What the column is ordered by. Folders always lead regardless: a list where
// they are interleaved with files reads as noise.
enum class SortKey { Name, Modified, Size, Kind };

struct Listing {
    std::vector<Entry> entries;
    DWORD error = ERROR_SUCCESS;   // ERROR_ACCESS_DENIED and friends surface in the UI
};

// Stable, and always by name as the tie-break, so equal dates or sizes do not
// shuffle between refreshes.
void sortEntries(std::vector<Entry>&, SortKey, bool descending);

// Blocking. Runs on a worker, never on the UI thread.
Listing listPlace(const Place&, bool showHidden);

// The child a folder row navigates to.
Place childOf(const Place&, const Entry&);

std::wstring documentsFolder();
// Desktop / Downloads / Profile etc., by the shell's own ids.
std::wstring knownFolder(const GUID& id);
std::wstring profileFolder();

} // namespace oriel
