# Oriel

**A file manager for Windows that draws itself, and a file dialog that can become it.**

[![CI](https://github.com/194754hs/oriel/actions/workflows/ci.yml/badge.svg)](https://github.com/194754hs/oriel/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

日本語版: **[README.ja.md](README.ja.md)**

Oriel is a native Windows file manager written in C++20 against Direct3D 11,
Direct2D, DirectComposition and DirectWrite. There is no UI framework
underneath: the window is borderless and every pixel in it is drawn by the
application, on a composition surface, so parts of the frame can be genuinely
transparent and let the desktop backdrop through.

It navigates in **columns** — each folder you enter opens a new column beside
the last, so the path you took stays on screen and you can step back into any
level of it without losing your place.

Alongside the shell there is a second, much smaller piece: **an in-process COM
server that registers per-user over the system's file-dialog classes**, so that
the Open and Save dialogs applications ask for can eventually be Oriel's own
column view instead of a list they cannot navigate. That component is described
in full below, because a DLL that loads into other people's processes deserves
to be described in full.

> **Status: early.** The shell runs, navigates, renders and acts on files. The
> dialog shim is deliberately still a pass-through — it registers, it is
> reached, and it forwards everything to the genuine dialog. See
> [Current state](#current-state) before expecting this to replace anything.

---

## Building

Requires Visual Studio 2022 (MSVC v143), the Windows 11 SDK, and CMake 3.21+.
Windows only — this is Win32 and DirectX to the core.

```powershell
# The shell, the shim, and the shim's test harness (x64)
cmake -B build -A x64
cmake --build build --config Debug

# Release
cmake -B build-rel -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel --config Release

# The 32-bit shim. A 64-bit DLL cannot load into a 32-bit application, and
# plenty of applications that show a save dialog are still 32-bit.
cmake -B build-x86 -A Win32 -DORIEL_SHIM_ONLY=ON
cmake --build build-x86 --config Release
```

`oriel.exe [folder]` opens at a folder. Passing one is how the shell hands work
to it, so it is wired up from the start.

The build copies `THIRD-PARTY-NOTICES.txt` next to the executable on every
build. That is not a courtesy — the embedded icon outlines are ISC and MIT, and
both require their notice to travel with the binary. Copying it automatically is
the only way it cannot be forgotten at packaging time.

## Verifying

```powershell
# Loads the shim directly and asks it for a dialog, exactly the way COM would
# after the per-user override is in place. Touches no registry, changes no
# system state. 31 checks across the save and open dialogs, including that
# unknown interfaces fall through to the real dialog, that a save dialog
# refuses IFileOpenDialog, and that Show really opens one — proven by its own
# event sink firing rather than by the call returning.
build\Debug\shim_test.exe

# Registers / unregisters / inspects the per-user override
shim\shim-control.ps1
```

The `design\verify-*.ps1` harnesses drive the running window through real input
and screenshot the result, so a feature is **reported from the screen rather
than from the source**. They write into `artifacts\`. They are visual
harnesses, not an assertion suite — `shim_test` is the only thing here that
passes or fails on its own.

CI builds x64 and x86 on `windows-latest` and runs `shim_test`.

---

## The dialog shim

This is the part to read carefully before you install anything.

`oriel_dialog.dll` is an in-process COM server registered **per user** over the
system's file Open and Save dialog classes. Once registered, an application
asking the shell for a file dialog gets Oriel's object instead of the system's.
The intent is to eventually answer with Oriel's own column picker
(`runPicker`); today it answers by handing the request straight to the genuine
dialog, unchanged.

Four rules the implementation exists to enforce:

1. **It never overrides the machine registration.** The per-user key is written;
   the machine key is deliberately left alone, and is read at runtime to find
   the real implementation.
2. **It never hardcodes where the real dialog lives.** That comes from the
   registration it did not touch.
3. **Any failure on our side ends in the real dialog, never in no dialog.** An
   application that cannot save a file is a far worse outcome than one that
   shows a plain dialog.
4. **It links the static CRT and depends on nothing beyond the OS.** It is
   loaded into processes that have already chosen their own runtime; it must not
   require a redistributable to be present, and must not risk disagreeing with
   what the host has loaded.

It is **not code-signed**. An unsigned DLL appearing inside a signed
application is precisely what security tooling is built to object to, and you
should expect it to be objected to. Signing is wired into the build and waits
only on a certificate:

```powershell
cmake -B build-rel -DORIEL_SIGN_THUMBPRINT=<sha1 of the certificate>
```

If any of this makes you uncomfortable, build the shell alone. Nothing in
`oriel.exe` requires the shim, and the shim is not registered by building it.

---

## Architecture

```
src/
  main.cpp          per-monitor-v2 DPI, COM apartment, message loop
  app_window.*      the borderless window, composed on a DirectComposition surface
  column_model.*    the column stack; enumeration runs off-thread and results are
                    discarded by token if you have navigated on since
  enumerate.*       directory enumeration
  thumbnail.*       thumbnails
  shell_menu.*      the system context menu, hosted rather than reimplemented
  assoc.*           file associations
  actions.*         open / rename / delete and the rest
  tags.*            tags, stored as sorted UTF-8 TSV
  settings.*        preferences, same TSV format, unknown keys preserved on save
                    so an older build cannot eat a newer build's settings
  icons.*           Lucide outlines as D2D geometry
  svg_path.*        SVG path data -> geometry
  icon_data.inc     generated by design/gen-icons.ps1 — do not hand-edit
  theme.h           colour tokens, two appearances, resolved once
  anim.h fade.h     motion
  textfield.h       inline editing
  metrics.h         layout constants
shim/
  shim.cpp          the in-proc COM server described above
  shim.def          exports
  protocol.h        shell <-> shim protocol
  shim_test.cpp     loads the DLL directly; no registry involved
design/
  ATTRIBUTION.md    what is borrowed, under what licence, and the rules for it
  bench.html        the design bench
  gen-icons.ps1     Lucide SVG -> src/icon_data.inc + THIRD-PARTY-NOTICES.txt
  fetch-*.ps1       fetch the icon sets and fonts the bench needs (not committed)
  verify-*.ps1      screenshot-driven verification harnesses
```

## Current state

Working: the window, column navigation, off-thread enumeration with stale-result
rejection, thumbnails, the hosted system context menu, file associations,
actions including rename, tags, persisted settings, the icon pipeline, and the
motion layer.

Not done: the shim answers with the genuine dialog rather than Oriel's picker;
no code signing; no installer; no release binaries yet. Treat published
behaviour as provisional until there is a tagged release.

## Licensing and attribution

Oriel is MIT — see [LICENSE](LICENSE).

The embedded icon outlines are **not**. They are derived from Lucide (ISC, with
a subset derived from Feather under MIT), and `THIRD-PARTY-NOTICES.txt`
reproduces both notices in full. `design/gen-icons.ps1` generates that file from
the set's own `LICENSE`; do not hand-write it, and regenerate it if the set is
ever replaced.

[design/ATTRIBUTION.md](design/ATTRIBUTION.md) records what may be brought into
this project and what may not, which is a shorter list than people expect. The
icon packages themselves and the font-embedding design bench are not committed —
redistributing whole sets the product does not use would be a licence obligation
taken on for nothing. The fetch scripts rebuild them locally.

## Security

Oriel ships a DLL that loads into other applications' processes. If you find a
way to break one of the four rules above, please report it privately — see
[SECURITY.md](SECURITY.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
