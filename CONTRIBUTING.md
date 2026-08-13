# Contributing to Oriel

## Setup

Visual Studio 2022 (MSVC v143), Windows 11 SDK, CMake 3.21+. Windows only.

```powershell
cmake -B build -A x64
cmake --build build --config Debug
build\Debug\shim_test.exe
```

There is no package manager and no vendored dependency tree. The shell links
`d3d11 dxgi d2d1 dcomp dwrite dwmapi user32 gdi32 shlwapi shell32 ole32`; the
shim links `ole32 shlwapi user32 advapi32` and nothing else. Keep it that way —
see the rules below for why the shim in particular cannot grow dependencies.

## The rules a pull request must not break

These are the reasons the shim can be installed at all. If a change needs one
relaxed, say so in the pull request rather than working around it quietly.

1. **Never override the machine registration.** Per-user only. The machine key
   is read at runtime to find the genuine implementation, and is never written.
2. **Never hardcode where the real dialog lives.**
3. **Every failure path ends in the genuine dialog.** An application that cannot
   save a file is worse than one showing a plain dialog. `reply->cancelled` is
   left alone on failure precisely so the shim reads it as "fall back".
4. **The shim keeps the static CRT and no dependency beyond the OS.** It is
   loaded into processes that already chose a runtime.
5. **`icon_data.inc` is generated.** Edit `design/gen-icons.ps1` or the source
   set, never the `.inc`. `THIRD-PARTY-NOTICES.txt` is generated from the icon
   set's own `LICENSE` by the same script — do not hand-write it.
6. **Stale enumeration results are discarded, not applied.** Columns carry a
   token; a worker result whose token no longer matches belongs to a folder the
   user has already left.
7. **Unknown settings and tag keys survive a save.** An older build must not eat
   a newer build's data.
8. **The system context menu stays hosted, not reimplemented.**

## Materials

Read [design/ATTRIBUTION.md](design/ATTRIBUTION.md) before adding any asset. The
short version:

- Only self-made material, or material under a licence that explicitly permits
  use, modification and redistribution (MIT / ISC / OFL and the like) with its
  notice included.
- Do not trace another product's visual design, and do not name another
  company's products, brands or feature names in the UI, the code, comments,
  commit messages or anything published. What may be referenced is *published
  design practice* — placement conventions, spacing rhythm, interaction idioms —
  not material and not names.
- Avoid bespoke licences. A term that can change is the one dependency you
  cannot plan around; if an MIT or ISC equivalent exists, take it.
- Fonts must permit embedding and redistribution.

The icon packages under `design/icon-sets/` and the generated
`design/bench.dist.html` / `design/icon-compare.html` are **not committed**:
they carry other people's work that the product does not ship, and committing
them would take on a redistribution obligation for nothing. `fetch-icons.ps1`,
`fetch-fonts.ps1`, `build.ps1` and `build-icon-compare.ps1` rebuild them
locally.

## Verifying a change

`shim_test.exe` is the only harness that passes or fails on its own. Everything
under `design/verify-*.ps1` drives the real window through real input and
screenshots the result into `artifacts\` — a feature is reported **from the
screen, not from the source**. If you change behaviour the screenshots cover,
run the relevant harness and say what you saw.

Scripts derive their paths from `$PSScriptRoot`. Do not reintroduce absolute
paths; they were removed so the harnesses work from any checkout.

Keep `.ps1` files pure ASCII and CRLF — PowerShell 5.1 reads a BOM-less script
as ANSI, and non-ASCII characters will be mangled.

## Pull requests

- One concern per pull request.
- Report what you measured, not what you expect. "shim_test clean, verify-rename
  screenshots attached" beats "should be fine".
- If you changed behaviour the README describes, update both `README.md` and
  `README.ja.md`.
- New warnings are failures: the build runs `/W4 /permissive-`.

## Security

Do not open a public issue for a vulnerability in the shim or the shell. See
[SECURITY.md](SECURITY.md).
