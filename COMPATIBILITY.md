# Compatibility

## OBS Studio

| | |
|---|---|
| Built against | Windows: OBS Studio **33.0.1** SDK (libobs API 33). Linux: the `obs-studio` **33.0.0** package for Ubuntu 26.04 from the official OBS PPA. |
| Supported | OBS Studio **33.x** |
| Older OBS (32 and earlier) | Not supported: OBS refuses to load a plugin built for a newer libobs ("compiled with newer libobs"). |
| Newer OBS (34+) | Not verified. The Windows build may load in a later OBS if its API stays compatible; the Linux build is tied to the OBS 33 package it was built against (the .deb requires `obs-studio` 33.x). A new build will be published after testing with each new OBS major version. |
| Qt | Windows: the Qt bundled with OBS (6.11 in OBS 33.0.1). Linux: the system Qt, 6.10 or newer. |
| Optional | Aitum Multistream (tested with 1.0.9); obs-websocket (bundled with OBS) to read Aitum's destination list. |

## Platforms and packages

Version 1.1.0 is provided for **Windows x64** and **Linux x86_64**. Linux is tested on Ubuntu 26.04 only. macOS support is planned for a future release. Development and compatibility testing are not yet complete. No release date is currently available.

| Platform | Architecture | Packages | Installs into |
|---|---|---|---|
| Windows 10 / 11 | x64 | Installer (.exe), manual ZIP | standard OBS: `C:\ProgramData\obs-studio\plugins\`; portable OBS: `<OBS folder>\plugins\` |
| Ubuntu 26.04 | x86_64 | .deb | `/usr/lib/x86_64-linux-gnu/obs-modules/plugins/`, `/usr/share/obs/obs-modules/plugins/` |
| Other Linux distributions (not tested) | x86_64 | manual .tar.xz | `~/.local/share/obs-studio/plugins/` (per user) |

Not available: macOS ([see below](#macos)), Windows on ARM (OBS's native ARM64 build cannot load an x64 plugin), Linux ARM64, Flatpak (see below), Ubuntu 24.04 (OBS Studio 33 is not available for it from the OBS PPA).

### Install types

| Platform | Standard OBS | Portable or custom OBS | Manual install |
|---|---|---|---|
| Windows | Installer ("Standard OBS Studio"), ZIP | Installer ("Portable OBS Studio folder", detected or browsed, no admin rights), ZIP | ZIP |
| Linux | .deb (system), .tar.xz (user) | `OBS_PLUGINS_PATH` (OBS started with `--portable` ignores `~/.local/share`) | .tar.xz |

## Verification status of 1.1.0

What has actually been checked for each platform. "CI VM" means a GitHub Actions virtual machine, which has no GPU and no real streaming; on Linux, OBS runs there on a virtual display (Xvfb). This table is updated before each release; a platform is never described as tested beyond what is listed here.

| | Windows x64 | Ubuntu 26.04 x86_64 |
|---|---|---|
| Compiled | Yes (MSVC 14.51, OBS 33.0.1 SDK) | Yes (CI VM: GCC, `obs-studio` 33.0.0 from the official OBS PPA, system Qt 6) |
| Unit tests | Pass | Pass (CI VM) |
| Packages built and verified (contents, checksums) | Yes (installer and ZIP) | Yes (CI VM: .deb and .tar.xz; files compared with the build, no debug symbols inside, checksums) |
| Installer or package tested (install, upgrade, uninstall, settings kept) | Portable OBS: yes (install, reinstall, upgrade from 1.0.1, downgrade guard, refusal while OBS runs, uninstall). Standard OBS: yes on a CI VM² (official OBS 33.0.1 installer; silent install, plugin loads, uninstall, settings kept) | .deb: install, reinstall, removal, settings kept (CI VM). .tar.xz: per-user install (CI VM) |
| Full isolated regression (mock outputs, DPI scales, layouts, lag window, real Aitum to a loopback server) | Pass | – |
| Plugin loads in OBS, dock registers | Yes (isolated OBS 33.0.1 copies; CI VMs² with portable and standard OBS 33.0.1) | Yes (CI VM: OBS 33.0.0, installed from the .deb and from the .tar.xz) |
| Loads on a real computer | Yes¹ (maintainer's PC, OBS 33.0.1 portable) | not yet |
| Real-world streaming | Yes¹ (OBS 33.0.1 + Aitum Multistream 1.0.9, live destination) | not yet |

The Windows DLL in the 1.1.0 packages (SHA-256 `a4db862a…8e680c60da`, listed in `packaging/windows/known-good-payloads.json`) passed every Windows check above that has no footnote. The packages contain exactly this file.

¹ This was verified with the maintainer's pre-release build of 1.1.0 (SHA-256 `0bc2d220…428b2a07`). It has the same source code, compiler and data files as the published DLL. The published DLL was linked so that it names only its debug-symbol file instead of the folder it was built in; that record is shorter, so some read-only data sits a few bytes earlier. Apart from those addresses, code, imports, exports and resources are identical.

² CI builds the plugin itself and packages that build with the same installer script as the release installer. The release installer contains the DLL listed above instead.

## Other Linux distributions

The manual archive contains the same binary as the Ubuntu package. It may work on another x86_64 distribution if all of these hold:
- OBS Studio **33.x** installed natively, providing `libobs.so.30` and `libobs-frontend-api.so.30` like the official OBS PPA package (check with `ldd`),
- Qt **6.10** or newer (`libQt6Core.so.6`, `libQt6Gui.so.6`, `libQt6Widgets.so.6`),
- a C/C++ runtime at least as new as Ubuntu 26.04's (the minimum glibc version is printed in the archive's `INSTALL.txt`).

This is not tested. Reports (working or not) are welcome as issues; please include your distribution, OBS version and log.

## macOS

macOS support is planned for a future release. Development and compatibility testing are not yet complete. No release date is currently available.

Version 1.1.0 has no macOS package. The macOS build configuration stays in the source tree for that work ([BUILDING.md](BUILDING.md#macos-future-development-not-released)).

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
| Linux | Packages are not GPG-signed; verify them with `SHA256SUMS.txt`. |
