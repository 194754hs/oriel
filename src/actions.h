#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace oriel {

// File operations, kept out of the window so the window stays about drawing.
//
// Everything destructive goes through the shell's own operation engine rather
// than through DeleteFile and friends: that is what puts things in the recycle
// bin, shows the progress and conflict UI the user already knows, and lets
// Ctrl+Z work afterwards. Rolling our own would be less code and worse.
namespace act {

// Clipboard. The list is CF_HDROP; cut and copy differ only by the preferred
// drop effect, which is the convention every other file manager reads.
bool copyToClipboard(HWND owner, const std::vector<std::wstring>& paths, bool cut);
bool copyTextToClipboard(HWND owner, const std::wstring& text);
// Returns what was on the clipboard, and whether it was cut.
std::vector<std::wstring> clipboardPaths(HWND owner, bool* wasCut);

// `into` must be a directory. Honours the cut flag by moving instead of copying.
bool pasteInto(HWND owner, const std::wstring& into);

bool moveToRecycleBin(HWND owner, const std::vector<std::wstring>& paths, bool confirm);
bool duplicate(HWND owner, const std::wstring& path);
// Creates "新規フォルダ", or the next free numbering. Returns the name made.
std::wstring newFolder(HWND owner, const std::wstring& parent);

bool createShortcut(HWND owner, const std::wstring& path);
bool showProperties(HWND owner, const std::wstring& path);
// The system share sheet, via the shell's own verb. False when the type has none.
bool share(HWND owner, const std::wstring& path);
bool openWith(HWND owner, const std::wstring& path);
bool revealInExplorer(HWND owner, const std::wstring& path);

} // namespace act
} // namespace oriel
