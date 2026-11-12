# Compatibility

## OBS Studio

| | |
|---|---|
| Built against | OBS Studio **33.0.1** (libobs API 33) |
| Supported | OBS Studio **33.x** |
| Older OBS (32 and earlier) | Not supported: OBS refuses to load a plugin built for a newer libobs ("compiled with newer libobs"). |
| Newer OBS (34+) | Not verified. Windows/macOS builds may load in a later OBS if its API stays compatible; the Linux build cannot (it links `libobs.so.33`). A new build will be published after testing with each new OBS major version. |
| Qt | Windows/macOS: the Qt bundled with OBS (6.11 in OBS 33.0.1). Linux: the system Qt, 6.10 or newer. |
| Optional | Aitum Multistream (tested with 1.0.9); obs-websocket (bundled with OBS) to read Aitum's destination list. |

## Platforms and packages

| Platform | Architecture | Packages | Installs into |
|---|---|---|---|
| Windows 10 / 11 | x64 | Installer (.exe), manual ZIP | standard OBS: `C:\ProgramData\obs-studio\plugins\`; portable OBS: `<OBS folder>\plugins\` |
| macOS 13 or newer | Universal (Apple Silicon arm64 + Intel x86_64 in one binary) | DMG with PKG installer, manual ZIP (.plugin bundle) | `~/Library/Application Support/obs-studio/plugins/` (per user) |
| Ubuntu 26.04 | x86_64 | .deb | `/usr/lib/x86_64-linux-gnu/obs-modules/plugins/`, `/usr/share/obs/obs-modules/plugins/` |
| Other Linux distributions | x86_64 | manual .tar.xz | `~/.local/share/obs-studio/plugins/` (per user) |

Not available: Windows on ARM (OBS's native ARM64 build cannot load an x64 plugin), Linux ARM64, Flatpak (see below), Ubuntu 24.04 (OBS Studio 33 is not available for it from the OBS PPA).

### Install types

| Platform | Standard OBS | Portable or custom OBS | Manual install |
|---|---|---|---|
| Windows | Installer ("Standard OBS Studio"), ZIP | Installer ("Portable OBS Studio folder", detected or browsed, no admin rights), ZIP | ZIP |
| macOS | DMG/PKG (current user) | No portable mechanism on macOS. Advanced: `OBS_PLUGINS_PATH` when OBS is started from Terminal | ZIP |
| Linux | .deb (system), .tar.xz (user) | `OBS_PLUGINS_PATH` (OBS started with `--portable` ignores `~/.local/share`) | .tar.xz |

## Verification status of 1.1.0

What has actually been checked for each platform. "CI VM" means a GitHub Actions virtual machine, which has no GPU and no real streaming. This table is updated before each release; a platform is never described as tested beyond what is listed here.

| | Windows x64 | macOS universal | Ubuntu 26.04 x86_64 |
|---|---|---|---|
| Compiled | Yes (MSVC 14.51, OBS 33.0.1 SDK) | pending CI | pending CI |
| Unit tests | Pass | pending CI | pending CI |
| Packages built and verified (contents, checksums) | Yes (installer and ZIP) | pending CI | pending CI |
| Installer tested (install, upgrade, uninstall, settings kept) | Portable OBS: yes (install, reinstall, upgrade from 1.0.1, downgrade guard, refusal while OBS runs, uninstall). Standard OBS: pending CI | pending CI | pending CI |
| Full isolated regression (mock outputs, DPI scales, layouts, lag window, real Aitum to a loopback server) | Pass | – | – |
| Plugin loads in OBS 33.0.1, dock registers | Yes (isolated OBS copies) | pending CI (VM) | pending CI (VM) |
| Loads on a real computer | Yes¹ (maintainer's PC, OBS 33.0.1 portable) | not yet | not yet |
| Real-world streaming | Yes¹ (OBS 33.0.1 + Aitum Multistream 1.0.9, live destination) | not yet | not yet |

The Windows DLL in the 1.1.0 packages (SHA-256 `a4db862a…8e680c60da`, listed in `packaging/windows/known-good-payloads.json`) passed every Windows check above that has no footnote. The packages contain exactly this file.

¹ This was verified with the maintainer's pre-release build of 1.1.0 (SHA-256 `0bc2d220…428b2a07`). It has the same source code, compiler and data files as the published DLL. The published DLL was linked so that it names only its debug-symbol file instead of the folder it was built in; that record is shorter, so some read-only data sits a few bytes earlier. Apart from those addresses, code, imports, exports and resources are identical.

## Other Linux distributions

The manual archive contains the same binary as the Ubuntu package. It may work on another x86_64 distribution if all of these hold:
- OBS Studio **33.x** installed natively (provides `libobs.so.33` and `libobs-frontend-api`),
- Qt **6.10** or newer (`libQt6Core.so.6`, `libQt6Gui.so.6`, `libQt6Widgets.so.6`),
- a C/C++ runtime at least as new as Ubuntu 26.04's (the exact minimum `GLIBC_` version is listed in each release's `linux-dependency-audit.txt` from CI).

This is not tested. Reports (working or not) are welcome as issues; please include your distribution, OBS version and log.

## Flatpak

**Not supported in this release.**

OBS Studio from Flathub (`com.obsproject.Studio`) runs in a sandbox. It loads plugins from:
- the Flatpak extension point `/app/plugins` (plugins published on Flathub as `com.obsproject.Studio.Plugin.<Name>` extensions and built against the OBS Flatpak's runtime), or
- `~/.var/app/com.obsproject.Studio/data/obs-studio/plugins/` inside the sandbox.

The sandbox uses its own Qt and system libraries (freedesktop runtime 26.08 plus OBS's bundled Qt), not the host's. Copying a plugin built for a system OBS into the sandbox folder may or may not work, and would break silently with a runtime update, so it is not offered as an installation method.

The proper way is a Flathub extension (`com.obsproject.Studio.Plugin.MultiStreamStats`) built with the OBS Flatpak SDK and reviewed by Flathub. That is planned for a later release; it needs Flathub's review, so no date is promised.

## Code signing

| Platform | 1.1.0 |
|---|---|
| Windows | Installer and DLL are **not** Authenticode-signed. SmartScreen shows "Windows protected your PC"; choose More info → Run anyway. |
| macOS | Bundle has an ad-hoc signature only; PKG and DMG are **not** signed with an Apple Developer ID and **not** notarized. Gatekeeper asks for "Open Anyway" (see INSTALL.md). |
| Linux | Packages are not GPG-signed; verify them with `SHA256SUMS.txt`. |
