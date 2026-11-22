# Licences of bundled components

The F1DockTimers source code is MIT licensed (see `../LICENSE`).

The Windows build also ships these third-party components:

| Component | Licence | File |
| --- | --- | --- |
| Qt 6 (Core, Gui, Widgets, Network, Svg and their plugins) | LGPL-3.0 (which builds on GPL-3.0) | `LGPL-3.0.txt`, `GPL-3.0.txt` |
| Noto Sans typeface | SIL Open Font License 1.1 | `OFL-1.1-NotoSans.txt` |
| Noctalia F1 plugin, omarchy-f1, f1-circuits | MIT | `MIT-noctalia-f1-omarchy-f1-f1-circuits.txt` |
| Tabler Icons glyphs | MIT | `MIT-tabler-icons.txt` |
| Microsoft Visual C++ runtime (`vcruntime140*.dll`, `msvcp140*.dll`) | Redistributable files under the Visual Studio licence terms | |

## Qt / LGPL notice

The build links Qt dynamically. The Qt libraries are separate files next to `F1DockTimers.exe`
(`Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, `Qt6Network.dll`, `Qt6Svg.dll` and the plugin
folders `platforms`, `imageformats`, `tls`), so you can replace them with your own LGPL-compatible
builds of the same version (Qt 6.8.3, MSVC 2022, 64-bit). Source code for Qt is at
https://code.qt.io/cgit/ and https://download.qt.io/official_releases/qt/6.8/6.8.3/. Qt itself
contains third-party code under its own permissive licences, listed at
https://doc.qt.io/qt-6/licenses-used-in-qt.html.
