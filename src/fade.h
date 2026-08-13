#pragma once
#include "anim.h"
#include <unordered_map>
#include <cstdint>

namespace oriel {

// A fade per interactive element, keyed by a stable id.
//
// One shared "is something hovered" tween is not enough: when the pointer moves
// from one row to the next, the row being left has to fade out while the new one
// fades in. With a single tween the old one cuts to nothing, and a cut is
// exactly what reads as "no animation" however smooth the incoming half is.
//
// Paint asks for a value and says what the element's state should be; the set
// works out whether that is a change and how far along the fade is. Elements
// that stop being painted (scrolled out of view) are dropped, because nobody
// can see them cut.
class FadeSet {
public:
    void beginFrame() { ++gen_; }

    // 0..1 for `key`, heading toward `on` over `ms`.
    float at(uint64_t key, bool on, int ms) {
        auto it = m_.find(key);
        if (it == m_.end()) {
            // First sight: start settled, so a rebuilt frame does not flash.
            F f; f.on = on; f.seen = gen_; f.t.set(on ? 1.0f : 0.0f);
            m_.emplace(key, f);
            return on ? 1.0f : 0.0f;
        }
        F& f = it->second;
        f.seen = gen_;
        if (f.on != on) { f.on = on; f.t.to(on ? 1.0f : 0.0f, ms); }
        return f.t.value();
    }

    bool active() const {
        for (const auto& kv : m_)
            if (kv.second.t.active()) return true;
        return false;
    }

    // Called after a frame: forget whatever was not painted. Anything still on
    // screen is kept even when it is settled dark, because that settled-dark
    // entry is the only record that the element was ever off. Dropping it makes
    // the next frame treat the element as new, and a new element starts settled
    // at whatever state it is asked for - which is a snap, not a fade.
    void gc() {
        for (auto it = m_.begin(); it != m_.end(); )
            if (it->second.seen != gen_) it = m_.erase(it);
            else ++it;
    }

private:
    struct F { Tween t; bool on = false; uint32_t seen = 0; };
    std::unordered_map<uint64_t, F> m_;
    uint32_t gen_ = 0;
};

// Surfaces get their own high bits so two of them can never collide on an index.
enum class Fx : uint32_t {
    Caption = 1, Tab, NewTab, SideBtn, SideRow, SideSel, Capsule, Search, Gear,
    Switch, RowHover, RowSel, InspTab, Action, Crumb, TagRow, Field, TabClose,
};
inline uint64_t fkey(Fx s, int index) {
    return (static_cast<uint64_t>(s) << 32) | static_cast<uint32_t>(index);
}

// Every chrome control the pointer can light up. Ids are spaced so a control
// group can carry its member index in the low digits.
enum HotId : int {
    HotNone     = 0,
    HotSideBtn  = 100,   // +0..2   collapse / back / forward
    HotCapsule  = 200,   // +0..2   sort / share / more
    HotSearch   = 300,
    HotGear     = 400,
    HotSwitch   = 500,   // +0..2   column / list / icon
    HotNewTab   = 600,
};

} // namespace oriel
