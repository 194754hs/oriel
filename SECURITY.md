# Security Policy

Oriel ships `oriel_dialog.dll`, an in-process COM server that registers per-user
over the system's file dialog classes and is therefore **loaded into other
applications' processes**. A defect there is not a rendering glitch; it is code
running inside someone else's signed program, between a user and their files.
Security reports are the highest-priority class of issue here.

## Reporting a vulnerability

**Do not open a public issue for a security problem.**

Use GitHub's private vulnerability reporting:
**Security → Report a vulnerability** on this repository.

Please include:

- The commit you built, and whether it was the x64 or Win32 (`ORIEL_SHIM_ONLY`)
  build
- Windows build (`winver`)
- Whether the shim was registered (`shim\shim-control.ps1`) and into which host
  application the DLL was loaded
- Reproduction steps — through `shim_test.exe` wherever possible, since it
  exercises the shim without touching the registry
- `oriel.log` / `oriel-shim.log` from next to the binary, with paths redacted if
  they contain anything you would rather not share

You will get an acknowledgement of receipt. Reporters are credited in the
release notes unless they ask otherwise.

## In scope

Anything that breaks one of the rules the shim exists to enforce:

1. **The machine registration is never overridden.** Any route by which Oriel
   writes, redirects or damages the machine-wide dialog registration rather than
   the per-user one.
2. **The real dialog's location is never hardcoded.** Any way to make the shim
   load an implementation other than the one the untouched registration names —
   a planted DLL, a search-order weakness, a writable path in the resolution
   chain.
3. **Failure ends in the real dialog, never in no dialog.** Any input or state
   that makes an application lose its file dialog entirely, hang waiting for
   one, or receive a path the user did not choose.
4. **The DLL stays self-contained.** Any dependency that could be satisfied from
   outside the OS, or any interaction with a host process's already-loaded
   runtime. It links the static CRT specifically to avoid this.

Also in scope, in the shell itself: a path that escapes the folder being
enumerated, anything that executes a file where it should only reveal or open
it through the registered association, corruption of the tags or settings TSV
that a later build acts on, and any elevation of privilege.

## Out of scope

- The DLL being unsigned, and the security warnings that follow from it. This is
  known, documented in the README, and waiting on a certificate; the build
  already supports signing via `ORIEL_SIGN_THUMBPRINT`.
- The shim currently forwarding everything to the genuine dialog. That is the
  intended behaviour of this build, not a defect.
- Crashes reached only by pointing the shell at a pathological directory tree,
  unless they corrupt data or escape the tree.
- Vulnerabilities in Windows itself — report those upstream; open a normal issue
  here if Oriel needs to stop relying on the affected behaviour.

## Design notes relevant to security

- The shell process is a **standard user process**. There is no elevation path
  and no "run as administrator" affordance.
- The system context menu is **hosted, not reimplemented**, so verb execution
  stays with the shell rather than being re-derived here.
- Directory enumeration runs off the UI thread and results carry a token;
  anything that arrives after you have navigated on is discarded rather than
  applied to whatever column now occupies that slot.
- Settings and tags are stored as sorted UTF-8 TSV under `%LOCALAPPDATA%\Oriel`,
  readable and repairable by hand. Unknown keys are preserved on save so an
  older build cannot silently drop a newer build's data.

## Supported versions

Fixes land on `main`. There are no tagged releases yet, so there is no older
version to patch.
