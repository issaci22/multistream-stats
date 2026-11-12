# Third-party notices

Multi-Stream Stats is licensed under the GNU General Public License, version 2 or (at your option) any later version (GPL-2.0-or-later); see [LICENSE](LICENSE).

## Linked at run time (not included in the packages)

| Component | License | How it is used |
|---|---|---|
| libobs and obs-frontend-api (OBS Studio) | GPL-2.0-or-later | The plugin links against OBS Studio's libraries, which OBS Studio provides at run time. This is why the plugin is GPL-licensed. Source: https://github.com/obsproject/obs-studio |
| Qt 6 (Core, Gui, Widgets) | LGPL-3.0 (also available under GPL and commercial licenses) | Dynamically linked; provided by OBS Studio (Windows, macOS) or the Linux distribution. Not redistributed by this project. Source: https://www.qt.io/ |
| C/C++ runtime libraries of the platform | platform licenses | Provided by the operating system or OBS Studio. |

## Included in the source code

| Component | License | Location |
|---|---|---|
| obs-websocket plugin API header (`obs-websocket-api.h`) | GPL-2.0-or-later | `src/third_party/` (unchanged; used to call Aitum Multistream's documented vendor request through obs-websocket) |
| doctest | MIT | `tests/third_party/doctest.h` (unit tests only; not part of the plugin binaries) |
| CMake modules from the OBS plugin template (obs-plugintemplate) | GPL-2.0 | `cmake/` (adapted; each file says what was changed) |

## Used to build the packages

| Tool | License | Note |
|---|---|---|
| Inno Setup | Inno Setup License | Creates the Windows installer. The installer contains Inno Setup's runtime with its copyright notices. https://jrsoftware.org/isinfo.php |
| Apple pkgbuild / productbuild / hdiutil, dpkg-deb | platform tools | Used to create the macOS and Linux packages; nothing of them is redistributed. |

## Names

"OBS" and "OBS Studio" are names of the OBS Project; "Aitum" and "Aitum Multistream" belong to Aitum. They are used only to say what this plugin works with. This project is not affiliated with or endorsed by either.
