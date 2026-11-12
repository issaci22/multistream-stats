# Installing Multi-Stream Stats

> **Packages:** Windows x64 and Linux x86_64 (tested on Ubuntu 26.04). macOS support is planned for a future release. Development and compatibility testing are not yet complete. No release date is currently available. See [COMPATIBILITY.md](COMPATIBILITY.md).

- [Before you start](#before-you-start)
- [Windows: installer](#windows-installer) · [Windows: manual ZIP](#windows-manual-zip)
- [Linux: .deb (Ubuntu 26.04)](#linux-deb-ubuntu-2604) · [Linux: manual archive](#linux-manual-archive) · [Flatpak](#flatpak) · [macOS](#macos)
- [Check the installation](#check-the-installation) · [Upgrading](#upgrading) · [Uninstalling](#uninstalling) · [Troubleshooting](#troubleshooting)

## Before you start

- You need **OBS Studio 33** (33.x; Help → About shows your version). Builds for OBS 33 do not load in older versions.
- **Close OBS Studio** before installing, upgrading or uninstalling.
- Download from the project's **Releases** page. Optionally compare the file's SHA-256 with `SHA256SUMS.txt`:
  - Windows (PowerShell): `Get-FileHash <file>`
  - Linux: `sha256sum <file>`
- The plugin never changes your OBS settings, scenes, profiles or stream keys. It keeps its own small settings file (which sections are collapsed) in OBS's `plugin_config/obs-multistream-stats/` folder; installers and uninstallers never touch it.

### Where OBS Studio 33 loads plugins from

| OBS Studio installed as | Plugin folder |
|---|---|
| Windows, standard (installer or Steam) | `C:\ProgramData\obs-studio\plugins\obs-multistream-stats\` |
| Windows, portable (ZIP download) | `<OBS folder>\plugins\obs-multistream-stats\` |
| Linux, system package (.deb) | `/usr/lib/x86_64-linux-gnu/obs-modules/plugins/obs-multistream-stats.so` and `/usr/share/obs/obs-modules/plugins/obs-multistream-stats/` |
| Linux, current user | `~/.local/share/obs-studio/plugins/obs-multistream-stats/` |
| Any system, custom folder | the folder named by the `OBS_PLUGINS_PATH` environment variable (advanced) |

Older guides mention `obs-plugins\64bit` (Windows) or `~/.config/obs-studio/plugins` (Linux). Those are **legacy** locations: OBS 33 marks plugins there as "Legacy", and future versions will stop loading them. The packages here never use them.

## Windows: installer

File: `obs-multistream-stats-<version>-windows-x64-installer.exe`

1. Close OBS Studio.
2. Run the installer. Because it is not code-signed yet, Windows SmartScreen may say **"Windows protected your PC"**. Click **More info → Run anyway** (only if you downloaded it from this project's Releases page).
3. **How is OBS Studio installed?**
   - **With administrator rights** (recommended): for a **standard** OBS installation (installed with the OBS installer or from Steam). Plugins for a standard OBS live in `C:\ProgramData\obs-studio\plugins`, which needs administrator rights. Portable folders work in this mode too.
   - **Without administrator rights**: for a **portable** OBS folder only.
4. Read and accept the license (GNU GPL v2).
5. **Your OBS Studio**: choose **Standard OBS Studio** or **Portable OBS Studio folder**.
   - Standard: the installer shows which OBS it found and its version.
   - Portable: the installer suggests a folder it found (for example `Documents\OBS Studio`). Check it, or click **Browse** and choose the OBS folder that contains `bin`, `data` and `config`. Choosing its `bin\64bit` folder or `obs64.exe` also works.
   - The installer checks that the folder really is OBS Studio 33 or newer and the 64-bit x64 version. It warns if a folder is not set up as portable (no `portable_mode.txt` or `obs_portable_mode.txt`), because OBS then only loads plugins from it when started with `--portable`.
   - If OBS is running from that folder, the installer asks you to close it.
6. The summary shows the OBS folder, the plugin folder and what will happen (install, upgrade from an older version, or reinstall). Click **Install**.
7. Start OBS and open **Docks → Multi-Stream Statistics**.

Notes:
- Only the plugin's own files are written (`obs-multistream-stats.dll`, `data\manifest.json`, `data\locale\*.ini`). No OBS file is changed, and nothing is put into the legacy `obs-plugins` folder.
- If an old copy exists in a legacy folder, the installer mentions it and leaves it alone. OBS skips it once the new copy exists; you can delete it by hand.
- Several OBS copies: run the installer once per OBS folder. The uninstaller (Settings → Apps → Multi-Stream Stats) removes the plugin from every folder it installed into.

Silent installation (IT, scripts). The OBS folder must always be given; the installer never guesses:

```
obs-multistream-stats-<version>-windows-x64-installer.exe /VERYSILENT /SUPPRESSMSGBOXES /ALLUSERS /MODE=standard
obs-multistream-stats-<version>-windows-x64-installer.exe /VERYSILENT /SUPPRESSMSGBOXES /CURRENTUSER /MODE=portable /OBSDIR="D:\OBS Studio"
```

Add `/LOG="<file>"` for a log. Exit code 0 means installed. Downgrading over a newer version additionally needs `/ALLOWDOWNGRADE`.

## Windows: manual ZIP

File: `obs-multistream-stats-<version>-windows-x64-manual.zip`. It contains the folder `obs-multistream-stats` plus `INSTALL.txt` and `LICENSE.txt`.

1. Close OBS Studio and extract the ZIP.
2. Copy the whole **`obs-multistream-stats`** folder into the plugins folder of your OBS:
   - **Standard OBS** (installer or Steam): `C:\ProgramData\obs-studio\plugins\`
     (ProgramData is hidden: type `C:\ProgramData\obs-studio` into the Explorer address bar. Create `plugins` if it does not exist. Windows asks for administrator permission.)
   - **Portable OBS**: `<your OBS folder>\plugins\` (create `plugins` if it does not exist).
3. Check that this file exists: `...\plugins\obs-multistream-stats\obs-multistream-stats.dll`.
4. Start OBS and open **Docks → Multi-Stream Statistics**.

## Linux: .deb (Ubuntu 26.04)

File: `obs-multistream-stats_<version>_ubuntu-26.04_amd64.deb` (x86_64). Tested on Ubuntu 26.04.

**Requirements:** OBS Studio **33.x** from the official OBS PPA. If OBS is not installed yet:

```
sudo add-apt-repository ppa:obsproject/obs-studio
sudo apt install obs-studio
```

**Install** (close OBS first):

```
sudo apt install ./obs-multistream-stats_<version>_ubuntu-26.04_amd64.deb
```

- The package depends on `obs-studio (>= 33.0.0)` and `obs-studio (<< 34)`, so it cannot be installed next to an incompatible OBS. It also depends on the Qt 6 libraries (Core, Gui, Widgets) and the C/C++ runtime that the plugin links; apt installs any that are missing.
- It installs system-wide into OBS's own plugin folders: `/usr/lib/x86_64-linux-gnu/obs-modules/plugins/obs-multistream-stats.so` and `/usr/share/obs/obs-modules/plugins/obs-multistream-stats/`.
- Then start OBS and open **Docks → Multi-Stream Statistics** ([check the installation](#check-the-installation)).

**Remove:** `sudo apt remove obs-multistream-stats`. Your plugin settings (`~/.config/obs-studio/plugin_config/obs-multistream-stats/`) are kept.

## Linux: manual archive

File: `obs-multistream-stats-<version>-linux-x86_64-manual.tar.xz`. Installs for the current user, without root. The `INSTALL.txt` inside the archive has the same steps.

**Requirements:**
- a native OBS Studio **33.x** (not the Flatpak). The plugin is built against the OBS 33 package from the official OBS PPA, which provides `libobs.so.30`;
- Qt **6.10** or newer (Core, Gui, Widgets);
- glibc **2.14** or newer (the C library);
- libstdc++ with `GLIBCXX_3.4.29`, i.e. from GCC **11.1** or newer (the C++ runtime).

These are the minimum versions the plugin binary requires; Ubuntu 26.04 ships newer versions of all of them. The archive's `INSTALL.txt` lists the same requirements.

It is tested on Ubuntu 26.04 only; other distributions are not tested ([details](COMPATIBILITY.md#other-linux-distributions)).

**Install** (close OBS first):

```
tar -xJf obs-multistream-stats-<version>-linux-x86_64-manual.tar.xz
mkdir -p "${XDG_DATA_HOME:-$HOME/.local/share}/obs-studio/plugins"
cp -r obs-multistream-stats "${XDG_DATA_HOME:-$HOME/.local/share}/obs-studio/plugins/"
```

Result: `~/.local/share/obs-studio/plugins/obs-multistream-stats/obs-multistream-stats.so` plus its `data/` folder.

**Remove:** `rm -r ~/.local/share/obs-studio/plugins/obs-multistream-stats`. Your plugin settings are kept.

OBS started with `--portable` ignores `~/.local/share/obs-studio/plugins`. For a portable or self-built OBS, put the folder anywhere and start OBS with `OBS_PLUGINS_PATH=/that/folder obs`.

## Flatpak

OBS Studio installed from Flathub (`com.obsproject.Studio`) is **not supported** by these packages. Flatpak OBS runs in a sandbox with its own Qt and system libraries; a plugin built for the system OBS is not guaranteed to be compatible, and copying it into the sandbox's folders is not a supported installation. See [COMPATIBILITY.md](COMPATIBILITY.md#flatpak).

## macOS

No macOS package is available. macOS support is planned for a future release. Development and compatibility testing are not yet complete. No release date is currently available.

## Check the installation

1. **Docks** menu → **Multi-Stream Statistics** opens the dock. With nothing streaming it shows **OBS Stream · Not streaming** and "No active streams".
2. **Tools → Plugin Manager** lists **Multi-Stream Statistics**, without a "Legacy" flag.
3. **Help → Log Files → View Current Log** contains:
   ```
   [multistream-stats] loaded version <version> (libobs API 33…, running OBS 33…)
   ```

## Upgrading

- **Windows installer:** run the new installer and choose the same OBS. It shows "Upgrade <old> to <new>", replaces the plugin files (including a leftover `.pdb` from manual installs) and keeps your settings.
- **Ubuntu .deb:** `sudo apt install ./<new>.deb`.
- **Manual installs:** close OBS, delete the old `obs-multistream-stats` folder, copy the new one.

## Uninstalling

| Installed with | Remove with |
|---|---|
| Windows installer | **Settings → Apps → Installed apps → Multi-Stream Stats → Uninstall** (close OBS first) |
| Windows ZIP | delete `...\plugins\obs-multistream-stats\` |
| Ubuntu .deb | `sudo apt remove obs-multistream-stats` |
| Linux archive | `rm -r ~/.local/share/obs-studio/plugins/obs-multistream-stats` |

The plugin's own settings file is kept. To remove it too, delete the `plugin_config/obs-multistream-stats` folder:

| OBS | Settings folder |
|---|---|
| Windows, standard | `%APPDATA%\obs-studio\plugin_config\obs-multistream-stats\` |
| Windows, portable | `<OBS folder>\config\obs-studio\plugin_config\obs-multistream-stats\` |
| Linux | `~/.config/obs-studio/plugin_config/obs-multistream-stats/` |

## Troubleshooting

**The dock is not in the Docks menu.**
1. Check your OBS version (Help → About): it must be 33 or newer.
2. Check the file is where OBS looks (table in [Before you start](#where-obs-studio-33-loads-plugins-from)):
   - Windows standard OBS: `C:\ProgramData\obs-studio\plugins\obs-multistream-stats\obs-multistream-stats.dll`. If your OBS is **portable**, ProgramData is ignored; use `<OBS folder>\plugins\` instead.
   - A folder nested one level too deep is a common mistake (`plugins\obs-multistream-stats\obs-multistream-stats\...`).
3. Open the current log (Help → Log Files) and search for `multistream-stats`:
   - `loaded version …` appears: the plugin works; open it from the Docks menu. If the dock is off-screen, use **Docks → Reset Docks**.
   - `Module '…obs-multistream-stats…' not loaded`, preceded by `LoadLibrary failed for '…'` (Windows) or `os_dlopen(…)` (Linux): the plugin file was found but could not be loaded. Usually the build does not match your OBS: a different OBS major version, a Linux system without the required libraries, or on Windows an ARM64 OBS (this build is x64 only).
   - `Module '…obs-multistream-stats…' compiled with newer libobs …`: your OBS is older than the version the plugin was built for. Update OBS.
   - Nothing at all: OBS did not find the plugin; check the folder again.
4. Tools → Plugin Manager: if the plugin is listed but disabled, enable it and restart OBS.

**Windows: SmartScreen warning.** The installer is not code-signed yet; click **More info → Run anyway** if you downloaded it from this project's Releases page.

**Linux: the plugin does not load.** Run `ldd ~/.local/share/obs-studio/plugins/obs-multistream-stats/obs-multistream-stats.so` (or the `/usr/lib/...` path) and look for `not found`. A missing `libobs` library means your OBS differs from the OBS 33 package this build was made for (Ubuntu 26.04, official OBS PPA).

**Values show N/A.** That is deliberate when a value cannot be measured; hover it to see why (for example: not supported by this output type, or only available while streaming).

Still stuck? [Open an issue](../../issues/new/choose) with your OBS log.
