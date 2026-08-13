#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace oriel {

inline constexpr int kTagCount = 4;

// Tags, kept in our own store rather than in the file system.
//
// The alternative is an alternate data stream on each file, which survives a
// rename but dies the moment anything copies through a non-NTFS volume, a zip,
// or most sync clients. A side table loses the association when a file is moved
// by another application - a real cost, stated plainly rather than hidden - but
// it never silently destroys the data, and it can be repaired.
class TagStore {
public:
    // %LOCALAPPDATA%\Oriel\tags.tsv. Missing file is not an error.
    void load();
    void save() const;

    // Nothing tagged at all is the common case, and asking costs two string
    // allocations per row per frame. Let callers skip the question.
    bool empty() const { return mask_.empty(); }

    // Bit per tag index, 0 when untagged.
    unsigned maskFor(const std::wstring& path) const;
    void toggle(const std::wstring& path, int tag);
    bool has(const std::wstring& path, int tag) const {
        return (maskFor(path) & (1u << tag)) != 0;
    }

    // Every path carrying `tag`, in insertion order. Paths that no longer exist
    // are skipped rather than reported: a tag view listing ghosts is worse than
    // one that quietly shrinks.
    std::vector<std::wstring> pathsWith(int tag) const;

    static const wchar_t* name(int tag);

private:
    static std::wstring storePath();
    std::unordered_map<std::wstring, unsigned> mask_;
    std::vector<std::wstring> order_;
    mutable bool dirty_ = false;
};

} // namespace oriel
