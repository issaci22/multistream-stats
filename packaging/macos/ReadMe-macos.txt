Multi-Stream Stats {VERSION} for macOS
==========================================

Requirements: macOS {MIN_MACOS} or newer, OBS Studio 33 or newer.
One download for every Mac: the plugin is a universal binary for both
Apple Silicon (M-series) and Intel Macs.

Install
-------
1. Quit OBS Studio.
2. Double-click "Install Multi-Stream Stats.pkg" and follow the steps.
   It installs for your user account only; no administrator password is needed.
3. Start OBS Studio and open Docks > Multi-Stream Statistics.
{SIGNING_NOTE}
Check that it works
-------------------
- Tools > Plugin Manager lists "Multi-Stream Statistics".
- Help > Log Files > View Current Log contains
    [multistream-stats] loaded version {VERSION}

Where the plugin goes
---------------------
  ~/Library/Application Support/obs-studio/plugins/obs-multistream-stats.plugin
(In Finder: Go > Go to Folder..., then paste the path.)
OBS Studio 33 only loads plugins from this folder in your user Library, so the
installer offers no other location.

Uninstall
---------
Quit OBS Studio and move obs-multistream-stats.plugin from the folder above to the Trash.
The dock's own settings are in
  ~/Library/Application Support/obs-studio/plugin_config/obs-multistream-stats/
Delete that folder as well if you want to remove them.

Manual installation (without the installer)
-------------------------------------------
Download obs-multistream-stats-{VERSION}-macos-universal-manual.zip and follow its INSTALL.txt.

Project page, source code and bug reports:
  {PROJECT_URL}
