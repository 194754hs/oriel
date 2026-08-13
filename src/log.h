#pragma once

namespace oriel {

// Appends a line to oriel.log next to the exe. A windowed app has nowhere else
// to say what went wrong, and "it silently did nothing" is the worst bug class.
void logf(const char* fmt, ...);

} // namespace oriel
