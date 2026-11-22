# Building the C++ port

One toolchain, chosen for the smallest and most ordinary Windows binary:

| Part | Version | Where |
|------|---------|-------|
| Compiler | MSVC 2022 (v143, 14.44) + Windows SDK 10.0.26100 | Visual Studio 2022 Community, "Desktop development with C++" |
| Qt | 6.8.3 LTS, `msvc2022_64` kit: qtbase + qtsvg (+ qttranslations, unused) | `C:\Qt\6.8.3\msvc2022_64` |
| Build | CMake 3.25+ (tested with 4.4), Visual Studio 17 2022 generator | `C:\Program Files\CMake\bin` |
| Tests | Qt Test, run by CTest | `tests/` |

Why MSVC and not the installed MinGW (WinLibs g++ 16): Qt's prebuilt MinGW kit is built with
its own MinGW 13, so mixing it with g++ 16 risks ABI trouble, and a MinGW build has to ship
`libstdc++`/`libgcc`/`libwinpthread` DLLs. MSVC binaries only need the Visual C++ runtime that
almost every Windows PC already has, and they are the most common shape antivirus engines see.

## One command

From the repository root, in any shell (no developer prompt needed, the Visual Studio generator
finds MSVC itself):

```powershell
cmake --workflow --preset release
```

That configures `build/msvc`, builds Release, and runs every test. The app and the tests land in
`build/msvc/Release/`; `windeployqt` copies the Qt DLLs and plugins next to `F1DockTimers.exe`,
so it runs from there.

Other presets: `cmake --build --preset debug`, `ctest --preset release`.

## Release files

```powershell
powershell -ExecutionPolicy Bypass -File packaging\build.ps1
```

Builds and tests, then writes `dist\F1DockTimers\` (the app folder with Qt and the Visual C++
runtime, no test programs), `dist\F1DockTimers-<version>-portable.zip` and, with Inno Setup 6
installed (`winget install JRSoftware.InnoSetup`), `dist\F1DockTimers-<version>-setup.exe`. The
version comes from `project(... VERSION ...)` in `CMakeLists.txt`.

`packaging\build.ps1 -TestInstaller` also builds a test installer under another app id and name;
`packaging\test-installer.ps1` then installs, upgrades over a Python-style folder and uninstalls
it on this PC without touching a real install. After adding a text to the app, run
`python tools\make_lang_template.py` (ctest fails while `lang\_template.txt` is out of date).

The CI workflow (`.github/workflows/windows-build.yml`) does the same on GitHub and attaches the
files to a release.

## Getting Qt without the Qt installer

The kit was fetched straight from the official Qt mirror, no account and no installer:

```powershell
$base = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.win64_msvc2022_64/"
$pre  = "6.8.3-0-202503201308"
$suf  = "-Windows-Windows_11_23H2-MSVC2022-Windows-Windows_11_23H2-X86_64.7z"
foreach ($m in "qtbase", "qtsvg") { curl.exe -L -o "$m.7z" "$base$pre$m$suf" }
# extract each archive into C:\Qt\6.8.3\msvc2022_64 (Windows tar.exe reads .7z)
```

About 40 MB to download, about 250 MB on disk. `aqtinstall` (`aqt install-qt windows desktop
6.8.3 win64_msvc2022_64 --archives qtbase qtsvg`) gives the same result, and CI uses
`jurplel/install-qt-action`, which wraps it.

Qt somewhere else? Pass `-DCMAKE_PREFIX_PATH=<qt>/msvc2022_64` to the configure step, or set
`QT_ROOT_DIR`.

## Running against real data

Settings live in `%APPDATA%\F1DockTimers\settings.json` and the cache in
`%LOCALAPPDATA%\F1DockTimers\cache.json`, the same files as the Python app's: the C++ build
replaces it and takes over its settings and cache as they are. It also shares the Python build's
single-instance name, so only one of the two runs at a time.

For a separate home (tests, side-by-side runs, or a sandboxed shell whose `%APPDATA%` is
virtualised), set `NOCTALIA_F1_HOME`, exactly as with the Python app:

```powershell
$env:NOCTALIA_F1_HOME = "$env:TEMP\f1home"   # uses <home>\config\settings.json and <home>\cache
.\build\msvc\Release\F1DockTimers.exe
```

`F1DockTimers.exe --quit` asks a running instance (same home) to exit; starting it a second time
opens the panel. `F1DockTimers.exe --screenshots <dir> [--tabs schedule,weather]` waits for data,
saves each panel tab as a PNG and exits (the same dev aid as the Python app; `settings:N` captures settings page N without saving). The log is
`f1.log` in the cache folder; `F1DT_DEBUG=1` adds debug lines.

OpenF1 Sponsor credentials are read from Windows Credential Manager, from the same entry the Python
app writes (`F1DockTimers/OpenF1`), or from `OPENF1_USERNAME` / `OPENF1_PASSWORD`. They are never
logged and never stored in the settings or cache. The tests never touch the real credential store.

## Layout

```
src/core/       pure logic, Qt Core only (port of f1win/core + the pure parts of the pill/schedule)
src/services/   network, cache, Windows helpers (port of f1win/services)
src/ui/         widgets, panel, popup, desktop widget, tabs, taskbar countdown (port of f1win/ui)
src/settings.*  the settings file, src/paths.* the folders
tests/          Qt Test suites, one per reference-python/tests/test_*.py area
tools/          measure.ps1 (startup, memory, CPU)
```
