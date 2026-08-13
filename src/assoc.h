#pragma once
#include <string>

namespace oriel {

// What should stand in for a file in a row.
//
// Our own line marks are one coherent family and should keep the rows calm, but
// they cannot say "this is a Blender scene" or "this is a Photoshop document".
// Where a real application owns the type, its own icon carries information ours
// never can, so it wins.
enum class Mark {
    Glyph,      // draw our own line mark
    TypeIcon,   // the application's icon for this type; one per extension
    FileIcon,   // the icon lives inside this particular file (.exe, .lnk)
};

// `ext` is the extension without the dot, already lowercased. Decided once per
// extension and remembered: the registry lookup is far too slow to do per row.
Mark markForExt(const std::wstring& ext);

// Drops the memo, so a newly installed application is picked up without a
// restart.
void forgetAssociations();

} // namespace oriel
