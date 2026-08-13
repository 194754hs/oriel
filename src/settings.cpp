#include "settings.h"
#include "utf8.h"
#include "log.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <cstdio>

namespace oriel {

std::wstring appDataDir() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        return {};
    std::wstring dir(local);
    CoTaskMemFree(local);
    dir += L"\\Oriel";
    CreateDirectoryW(dir.c_str(), nullptr);   // already-exists is fine
    return dir;
}

std::wstring Settings::storePath() {
    const std::wstring dir = appDataDir();
    return dir.empty() ? std::wstring{} : dir + L"\\settings.tsv";
}

void Settings::load() {
    kv_.clear();
    const std::wstring file = storePath();
    if (file.empty()) return;
    std::ifstream in(file, std::ios::binary);
    if (!in) return;                         // first run is not an error

    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const std::wstring line = fromUtf8(raw);
        const size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos || tab == 0) continue;
        kv_[line.substr(0, tab)] = line.substr(tab + 1);
    }
    logf("settings: loaded %zu keys", kv_.size());
}

void Settings::save() const {
    const std::wstring file = storePath();
    if (file.empty()) return;
    // Write beside, then swap: a crash mid-write must not leave a truncated
    // file that reads as "all defaults" on the next run.
    const std::wstring tmp = file + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { logf("settings: cannot write %ls", tmp.c_str()); return; }
        for (const auto& [k, v] : kv_) out << toUtf8(k) << '\t' << toUtf8(v) << '\n';
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING))
        logf("settings: replace failed %lu", GetLastError());
}

int Settings::getInt(const wchar_t* key, int def) const {
    const auto it = kv_.find(key);
    if (it == kv_.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}

float Settings::getFloat(const wchar_t* key, float def) const {
    const auto it = kv_.find(key);
    if (it == kv_.end()) return def;
    try { return std::stof(it->second); } catch (...) { return def; }
}

std::wstring Settings::getStr(const wchar_t* key, const std::wstring& def) const {
    const auto it = kv_.find(key);
    return it == kv_.end() ? def : it->second;
}

void Settings::set(const wchar_t* key, int v) {
    kv_[key] = std::to_wstring(v);
}

void Settings::set(const wchar_t* key, float v) {
    wchar_t buf[32];
    swprintf_s(buf, L"%.3f", static_cast<double>(v));
    kv_[key] = buf;
}

void Settings::set(const wchar_t* key, const std::wstring& v) {
    // A tab or newline would break the format; drop the pair rather than write
    // a file that will not parse back.
    if (v.find_first_of(L"\t\r\n") != std::wstring::npos) return;
    kv_[key] = v;
}

} // namespace oriel
