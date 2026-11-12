# Building Multi-Stream Stats

The plugin is C++17 with Qt 6 Widgets and CMake. Each platform is built natively on that platform; there is no cross-compilation. The same `CMakeLists.txt` and presets are used locally and in CI.

The release targets are Windows x64 and Ubuntu 26.04 x86_64. macOS is kept as [future development](#macos-future-development-not-released).

| | Windows x64 | Ubuntu 26.04 x86_64 |
|---|---|---|
| Toolchain | Visual Studio 2026 (or Build Tools) with C++ workload, Windows SDK 10.0.26100, "C++ CMake tools" | GCC, CMake 3.28+, Ninja, `jq` |
| OBS SDK | Downloaded by CMake: OBS 33.0.1 sources (libobs + frontend API built locally), obs-deps and Qt 6 from obs-deps 2026-10-23, all SHA-256 checked against `buildspec.json` into `.deps/` | System packages: `obs-studio` 33.x from `ppa:obsproject/obs-studio`, `qt6-base-dev` |
| Configure preset | `windows-x64` (or `windows-x64-dev` with the test plugin) | `ubuntu-x86_64` (`ubuntu-ci-x86_64` with the test plugin) |
| Output | `build_x64/rundir/RelWithDebInfo/obs-multistream-stats/` | `build_x86_64/rundir/RelWithDebInfo/obs-multistream-stats/` |

Everything stays inside the source tree: `.deps/`, `build_*/`, `artifacts/`. Nothing is installed into an OBS folder by the build.

## Windows

```powershell
tools\build.ps1                               # configure + build (preset windows-x64)
tools\build.ps1 -Preset windows-x64-dev -Test # + test-only mock plugin + unit tests
```

`tools\build.ps1` finds Visual Studio 2026 with `vswhere` and uses its bundled CMake. Git is not required for building.

## Ubuntu 26.04

```bash
sudo add-apt-repository ppa:obsproject/obs-studio
sudo apt install obs-studio qt6-base-dev qt6-base-private-dev libgles2-mesa-dev libsimde-dev \
                 build-essential cmake ninja-build pkg-config jq
cmake --preset ubuntu-ci-x86_64
cmake --build --preset ubuntu-ci-x86_64
ctest --preset ubuntu-ci-x86_64
```

The OBS package must be 33.x. The official OBS PPA's OBS 33 package provides `libobs.so.30`; packaging checks that the plugin links the libobs of the installed obs-studio 33.x package.

## macOS (future development, not released)

macOS support is planned for a future release. Development and compatibility testing are not yet complete. No release date is currently available.

The macOS build configuration is kept so that this work can resume without starting over: `cmake/macos/`, the `macos`, `macos-ci` and `macos-ci-strict` presets, `.github/scripts/package-macos.sh`, `packaging/macos/`, and the `macos` / `macos-smoke` CI jobs.

- Toolchain: Xcode 16 or newer (CI: Xcode 26.5), CMake 3.28+, `jq`. CMake downloads the OBS SDK, obs-deps and Qt and checks their SHA-256 as on Windows.
- The current configuration builds for Apple Silicon (arm64) only, deployment target macOS 13.0 (the same as OBS Studio 33). A universal build (arm64 + x86_64) was explored earlier; see the Git history.
- Output: `build_macos/RelWithDebInfo/obs-multistream-stats.plugin` (ad-hoc signed).

```bash
cmake --preset macos-ci            # downloads the OBS SDK, builds libobs the first time
cmake --build --preset macos-ci
ctest --preset macos-ci
```

To run the macOS CI jobs, start **Actions → Build and test → Run workflow** with **include_macos**. Push and pull-request runs skip them. A release with macOS packages also needs **include_macos** in the release workflow (see [Releasing](#releasing)).

## Tests

| Test | Where | How |
|---|---|---|
| Unit tests (statistics core, no OBS needed) | Windows, Linux (macOS when enabled) | `ctest --preset <preset>` |
| Strict build (warnings as errors) | CI, Linux report only (macOS too when enabled) | `*-strict` presets |
| Isolated OBS scenarios: mock outputs, layout at several DPI scales, real Aitum Multistream with a loopback-only RTMP server | Windows, local | `tools\setup-test-env.ps1`, `tools\run-scenario.ps1`, `tools\run-all-tests.ps1` |
| Installed-package smoke test: OBS starts with the plugin installed the way users install it, the dock registers, values are N/A when idle, clean exit, no added memory leaks | CI: Windows and Linux (macOS when enabled) | `tools/ci-smoke-test.ps1` (PowerShell 7) |

The test-only module `msd-mock-outputs` (`tests/mock-outputs`) creates simulated outputs and drives the scripted scenarios in `tests/scenarios/`. It refuses to run unless it is loaded from a folder whose path contains `/test-env/`, and it is never packaged.

## Packaging

| Platform | Command | Produces |
|---|---|---|
| Windows | `tools\package-windows.ps1 [-Payload <plugin folder>]` | installer (Inno Setup 6.3+, `packaging/windows/installer.iss`), manual ZIP, `SHA256SUMS-windows.txt` |
| Linux | `bash .github/scripts/package-linux.sh build_x86_64 <out>` | .deb, manual .tar.xz, debug-symbols archive, `SHA256SUMS-linux.txt` |
| macOS (future) | `bash .github/scripts/package-macos.sh build_macos <out>` | DMG containing the PKG, manual ZIP, dSYMs ZIP, `SHA256SUMS-macos.txt` |

All three scripts verify their output after packaging (extract again, compare every plugin file with the build, check that no debug symbols are inside, re-check the checksums).

Release assets are the Windows installer and manual ZIP, the Linux .deb and manual .tar.xz, a source archive and `SHA256SUMS.txt`. Debug-symbol archives stay CI artifacts.

**Windows release payloads are pinned.** `tools\package-windows.ps1` refuses to package a version unless its DLL and data files match `packaging/windows/known-good-payloads.json`, the hashes of the build that was tested end to end. CI packages its own build with `-AllowUnpinned` to test the pipeline; those packages are never released. For 1.1.0 the Windows release assets are built locally from the pinned DLL, the exact file that passed the full isolated regression and the installer tests.

## Continuous integration

`.github/workflows/build.yaml` (every push and pull request; never publishes):
- `windows`, `linux`: build, unit tests, dependency audit, packages, logs (kept 30 days);
- `windows-smoke`: the installer against the official OBS 33.0.1 portable ZIP and standard installer (silent install/refusal/reinstall/uninstall, settings kept, plugin loads);
- `linux-smoke`: the .deb and the .tar.xz against OBS 33 from the PPA under Xvfb;
- `checksums`: verifies the Windows and Linux checksum lists (and macOS when enabled) and writes a combined list;
- `macos`, `macos-smoke` (postponed): run only when the workflow is started by hand with **include_macos**. Push and pull-request runs skip them without requesting a macOS runner. With include_macos, `macos_smoke` can switch off the runtime tests (DMG/PKG and ZIP against the official OBS 33.0.1 DMG on an Apple Silicon runner).

A run succeeds only if every job that runs passes. The Windows and Linux jobs never wait for macOS.

The official OBS downloads and Inno Setup used by CI are pinned by SHA-256 (Inno Setup's Authenticode signer is checked too). Third-party actions are pinned by commit SHA; Dependabot proposes updates.

## Releasing

`.github/workflows/release.yaml` runs only when started by hand and waits for approval of the **release** environment (Settings → Environments → release → required reviewers). It never publishes; it creates a **draft**.

1. Update `buildspec.json` and `data/manifest.json` (version), `CHANGELOG.md`, `COMPATIBILITY.md` (verification status) and `docs/release-notes/<version>.md`.
2. Push, let **Build and test** pass, and review its artifacts and smoke results.
3. Tag the reviewed commit `v<version>` and push the tag.
4. Run **Release (draft only)** with the tag and the reviewed run's ID. It checks that the run built the tag's commit and succeeded, refuses placeholder identifiers, attaches the Linux .deb and manual .tar.xz from that run and a source archive, checks that only allowed files are in the draft, writes `SHA256SUMS.txt` and the notes, and creates the draft. macOS packages are attached only with **include_macos** (not for 1.1.0).
5. For 1.1.0: upload the locally built Windows assets to the draft, then run the workflow again with `mode=refresh-checksums`. It verifies that the Windows ZIP contains the pinned, tested DLL and rewrites the checksums.
6. Review the draft and publish it by hand.

## Code signing (not set up yet)

The packaging scripts already support signing; what is missing are certificates and the CI step that installs them.

- **macOS** needs an Apple Developer Program membership and:
  - a *Developer ID Application* certificate (signs the bundle and the DMG),
  - a *Developer ID Installer* certificate (signs the PKG),
  - an App Store Connect API key for `notarytool`.
  `package-macos.sh` signs with hardened runtime and a timestamp when `CODESIGN_IDENT` / `CODESIGN_IDENT_INSTALLER` are set, and notarizes and staples when `NOTARY_KEYCHAIN_PROFILE` names a stored `notarytool` profile. A signing job would import the certificates from GitHub secrets into a temporary keychain on the runner; it has not been written yet because it cannot be tested without the credentials.
- **Windows** options: SignPath Foundation (free for qualifying open-source projects), Azure Artifact Signing (paid, identity validation), or an OV/EV certificate. Signing would apply to the installer (and optionally the DLL) before checksums are computed. Note that signing the DLL changes its bytes, so a signed DLL is a new payload that must be pinned after testing.

Credentials belong only in GitHub secrets, never in the repository.

## Versioning

Semantic versioning. The version lives in `buildspec.json` and `data/manifest.json` and must match the tag (`v1.1.0`). A change to `src/` or `data/` always gets a new version number; a platform's binary is never replaced under an existing version.
