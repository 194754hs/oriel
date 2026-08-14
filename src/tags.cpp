#include "i18n.h"
#include "tags.h"
#include "utf8.h"
#include "log.h"

#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace oriel {

namespace {
// Comparison is case-insensitive because the file system is; storing the key
// folded keeps lookups honest without a custom hash.
std::wstring fold(const std::wstring& s) {
    std::wstring out = s;
    for (auto& c : out) c = static_cast<wchar_t>(towlower(c));
    return out;
}

} // namespace

const wchar_t* TagStore::name(int tag) {
    static const wchar_t* kNames[kTagCount] = { T(L"至急"), T(L"確認中"), T(L"納品済"), T(L"資料") };
    return (tag >= 0 && tag < kTagCount) ? kNames[tag] : L"";
}

std::wstring TagStore::storePath() {
    const std::wstring dir = appDataDir();
    return dir.empty() ? std::wstring{} : dir + L"\\tags.tsv";
}

void TagStore::load() {
    mask_.clear();
    order_.clear();
    const std::wstring file = storePath();
    if (file.empty()) return;
    std::ifstream in(file, std::ios::binary);
    if (!in) return;                          // no tags yet is the normal case

    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const std::wstring line = fromUtf8(raw);
        const size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        const std::wstring path = line.substr(0, tab);
        unsigned m = 0;
        try { m = static_cast<unsigned>(std::stoul(line.substr(tab + 1))); }
        catch (...) { continue; }
        if (path.empty() || !m) continue;
        const std::wstring key = fold(path);
        if (!mask_.count(key)) order_.push_back(path);
        mask_[key] = m;
    }
    logf("tags: loaded %zu entries", mask_.size());
}

void TagStore::save() const {
    const std::wstring file = storePath();
    if (file.empty()) return;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) { logf("tags: cannot write %ls", file.c_str()); return; }
    for (const auto& path : order_) {
        const auto it = mask_.find(fold(path));
        if (it == mask_.end() || !it->second) continue;
        out << toUtf8(path) << '\t' << it->second << '\n';
    }
    dirty_ = false;
}

unsigned TagStore::maskFor(const std::wstring& path) const {
    const auto it = mask_.find(fold(path));
    return it == mask_.end() ? 0u : it->second;
}

void TagStore::toggle(const std::wstring& path, int tag) {
    if (path.empty() || tag < 0 || tag >= kTagCount) return;
    const std::wstring key = fold(path);
    auto it = mask_.find(key);
    if (it == mask_.end()) {
        mask_[key] = 1u << tag;
        order_.push_back(path);
    } else {
        it->second ^= (1u << tag);
    }
    dirty_ = true;
    save();   // small file, and losing tags to a crash is not worth the risk
}

std::vector<std::wstring> TagStore::pathsWith(int tag) const {
    std::vector<std::wstring> out;
    if (tag < 0 || tag >= kTagCount) return out;
    const unsigned bit = 1u << tag;
    for (const auto& path : order_) {
        const auto it = mask_.find(fold(path));
        if (it == mask_.end() || !(it->second & bit)) continue;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        out.push_back(path);
    }
    return out;
}

} // namespace oriel
