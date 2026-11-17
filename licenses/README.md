# Licences of bundled components

The F1DockTimers source code is MIT licensed (see `../LICENSE`).

The Windows build also ships these third-party components:

| Component | Licence | File |
| --- | --- | --- |
| Qt 6 and Qt for Python (PySide6, Shiboken6) | LGPL-3.0 (which builds on GPL-3.0) | `LGPL-3.0.txt`, `GPL-3.0.txt` |
| Noto Sans typeface | SIL Open Font License 1.1 | `OFL-1.1-NotoSans.txt` |
| Noctalia F1 plugin, omarchy-f1, f1-circuits | MIT | `MIT-noctalia-f1-omarchy-f1-f1-circuits.txt` |
| Tabler Icons glyphs | MIT | `MIT-tabler-icons.txt` |
| keyring, tzdata and others | MIT / Apache-2.0 | see `THIRD_PARTY_NOTICES.md` and each package's `dist-info` |

## Qt / LGPL notice

The build links Qt dynamically. The Qt and PySide6 libraries are separate files in
`_internal/PySide6/` (for example `Qt6Core.dll`, `QtCore.pyd`), so you can replace them with
your own LGPL-compatible builds of the same version. Source code for Qt and Qt for Python is at
https://code.qt.io/cgit/ and https://download.qt.io/official_releases/QtForPython/ (PySide6 6.12.0).
