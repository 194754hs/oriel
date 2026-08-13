#pragma once
#include <d2d1_1.h>

namespace oriel {

// Appends one SVG path `d` string to an already-open geometry sink.
//
// Covers what an icon set emits and nothing more: M L H V C S Q T A Z, absolute
// and relative, with implicit repeats. The caller owns Open()/Close(); this
// opens and ends its own figures, so several strings can be appended into one
// geometry.
bool appendSvgPath(ID2D1GeometrySink* sink, const char* d);

} // namespace oriel
