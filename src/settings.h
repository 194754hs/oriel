#pragma once
#include <string>
#include <map>

namespace oriel {

// Settings, as a flat key/value file.
//
// A settings panel that reverts everything on exit is a demo, not a product.
// The format is the same TSV the tags use: sorted, UTF-8, one pair per line, so
// it can be read, diffed and repaired by hand. Unknown keys are preserved on
// save rather than dropped, so an older build cannot silently eat a newer
// build's preferences.
class Settings {
public:
    void load();
    void save() const;

    int          getInt  (const wchar_t* key, int def) const;
    float        getFloat(const wchar_t* key, float def) const;
    bool         getBool (const wchar_t* key, bool def) const { return getInt(key, def ? 1 : 0) != 0; }
    std::wstring getStr  (const wchar_t* key, const std::wstring& def = {}) const;

    void set(const wchar_t* key, int v);
    void set(const wchar_t* key, float v);
    void set(const wchar_t* key, bool v) { set(key, v ? 1 : 0); }
    void set(const wchar_t* key, const std::wstring& v);

private:
    static std::wstring storePath();
    std::map<std::wstring, std::wstring> kv_;   // ordered, so the file is stable
};

} // namespace oriel
