# Port notes

Status of the C++/Qt 6 port against the Python app (v1.0.8, the spec in `reference-python/`).

- Phase 1: a foundation that builds with one command, the pure logic ported with tests, a running
  tray icon + taskbar countdown on real data.
- Phase 2: the whole poller (standings, results, grids, OpenF1 free/sponsor, weather, measured
  weather) with tests, and the panel: popup, shared widgets, Schedule and Weather tabs.
- Phase 3: all seven panel tabs (Grid, Live, Last, Drivers, Teams added) and the spoiler-free
  cover, the settings dialog with all six pages, and settings writes into the shared
  `settings.json` (Save, pin, countdown drag, grid switch, "Show results", first run, exit).
- Phase 4: the experimental F1 live-timing stream client (SignalR over server-sent events).
- Phase 5: the embedded taskbar countdown (a layered child window of the taskbar) and the
  Windows 11 Widgets-button lookup (UI Automation on a worker thread).
- Phase 6: the desktop widget, Windows notifications (session reminders and the top 3 after
  qualifying, sprint and race) and the tray menu toggles.
- Phase 7: the GitHub Releases updater. The C++ build is now version 1.1.0, the planned first
  release of the rework.
- Phase 8: release-ready. Start with Windows, the Inno Setup installer that replaces the Python
  build in place, the release build script and CI workflow, the translation-template generator,
  licence files, and the end of the side-by-side workarounds (shared cache, log and
  single-instance name).

## Release plan (decided with the user)

- The first C++ release is **v1.1.0**, in the same GitHub repository with the same asset names
  (`F1DockTimers-X.Y.Z-setup.exe`), so the Python 1.0.x updater upgrades straight into it.
- It replaces the Python build **in its original location**: installed to
  `%LOCALAPPDATA%\Programs\F1DockTimers`, same AppData settings and cache. Done in phase 8:
  `packaging/installer.iss` has the Python installer's AppId, folder, Start-menu entry and Run
  value; the app now uses `cache.json`, `f1.log` and the Python build's single-instance name.
- The Python build's settings are **imported automatically**: both read and write the same
  `settings.json` (the C++ settings tests check the Python loader reads it back), and the
  pre-rename `NoctaliaF1` folder migration is ported. Nothing to import by hand.
- The installer offers to **delete the old Python files** (the PyInstaller `_internal` folder):
  a "Remove the files of the previous version (your settings are kept)" task, shown only when
  they are there, **checked by default**, and also applied by the silent install the in-app
  updater runs (tested). It also removes `cache-cpp.json` and `f1-cpp.log` left by the port.

## Measurements

Measured 2026-11-22 on this PC (Windows 11 Pro 26200, 16 logical cores, 1920x1080) with
`tools/measure.ps1`: each build in its own `NOCTALIA_F1_HOME` with a copy of the same
`settings.json` (taskbar style, Copenhagen time), warm cache, one warm-up run first, then 240 s of
samples every 30 s with the panel closed. Python = the v1.0.8 PyInstaller onedir build
(`dist\F1DockTimers\`). Two Python runs were made; both are shown.

| | C++ phase 2 | C++ phase 1 | Python 1.0.8 |
|---|---|---|---|
| Download | **12.1 MB** zip (27.1 MB unpacked: Qt 23 MB, font 2 MB, exe 0.9 MB); phase 3: 12.3 MB zip, exe 1.1 MB, plus Qt's small SVG image plugin for the dialog's arrows | 11.9 MB zip | 42.0 MB portable exe / 44.0 MB MSIX (103 MB unpacked) |
| Start to countdown on screen (both wait 800 ms on purpose) | **1.04-1.09 s** | 0.93-1.09 s | 1.45-2.22 s |
| Memory before the first trim (working set) | 68 MB | 52-60 MB | 115-117 MB |
| Idle private working set (Task Manager "Memory"), 30-240 s | **2.7-5.5 MB** | 1.5-2.6 MB | 7.4-12.2 MB (22 MB in one run after a background fetch) |
| Idle working set incl. shared pages | 6.2-15 MB | 3.9-9.3 MB | 12.8-48 MB |
| Commit size | **37 MB** | 27 MB | 70-75 MB |
| Idle CPU (210 s window, % of all cores) | 0.019 % | 0.008 % | 0.017-0.042 % |

Phase 3 adds UI that only runs while the panel or the dialog is open, so the idle rows were not
measured again.

Phase 2 makes the comparison fair for the background work: both builds now fetch the same data
(schedule, standings, results, forecast, measured weather, OpenF1 last session). With that, the C++
build idles at roughly **half the memory** of the Python build and about half the commit size; idle
CPU is now about the same (it is dominated by the once-a-second tick and the timers, not by the
language). The remaining features add little to the binary (no new Qt modules are needed). The VC++
runtime is not bundled (present on nearly every PC; app-local it adds about 1 MB).

## What works

- **Build**: `cmake --workflow --preset release` configures, builds and runs all tests (MSVC 2022 +
  Qt 6.8.3, see [BUILD.md](BUILD.md)). A GitHub Actions draft (`.github/workflows/windows-build.yml`)
  does the same on `windows-2022` and uploads a zip.
- **Core logic, fully ported** (`src/core`, Qt Core only): time parsing/formatting with zones and
  DST, sessions, every Jolpica and OpenF1 parser, the F1 live-timing stream parser, weather, spoiler
  rules, team colours and flags, `T()` translations with the same language-file format, the taskbar
  text templates, the schedule countdown text and the circuit outlines (`tracks.txt`). Python
  semantics that matter are kept on purpose: banker's rounding, `str()`/`int()`/`float()` on loose
  JSON, truthiness, `{token}` formatting.
- **Poller, complete** (`src/services/poller.cpp`), same timings and
  rules as `services/poller.py`:
  - schedule (Jolpica `current/next`, season-transition fallback), driver and constructor
    standings, last results, refresh intervals from settings, exponential backoff that keeps the
    previous rows, post-race refresh once per round, sleep/resume detection;
  - provisional grid from Jolpica qualifying, official grid from OpenF1 `starting_grid` (every
    30 min after qualifying, then 60/20/5 min before the race, only for this meeting);
  - F1 timing `SessionInfo` for revised start times; manual offsets per round;
  - OpenF1 Free plan: quiet from 15 min before a session until 20 min after it, the last session's
    results after it ends, the "free" message instead of live polling, never any credentials;
  - OpenF1 Sponsor plan: token login (form POST), bearer header on OpenF1 requests only, token
    reuse until 2 min before expiry, 401/403/429 handling with the same back-offs and messages;
  - Open-Meteo forecast for the weekend (16 days ahead, refresh by the weather setting, "off"
    respected), measured session weather from OpenF1 once per finished session.
- **F1 live-timing stream** (`src/services/f1stream.cpp`, phase 4), same protocol and rules as
  `services/f1stream.py`: negotiate, event stream with the load balancer's cookie kept by the
  shared network manager, JSON handshake after 1 s and Subscribe to the 8 feeds after 2 s,
  updates coalesced to at most one every 500 ms, 4 MB buffer cap, token characters checked,
  reconnect 15 s after a normal close and 60 s after a failure ("net" shown on the Live tab),
  started only near a session (or with the debug option) and stopped when switching back to
  OpenF1. Checked against the real feed 50 minutes before the Las Vegas race: running order
  (official grid order, ANT 6th after his penalty), pit-lane state, "2:00:00 left", yellow flag.
- **Credentials**: Windows Credential Manager via `CredReadW`/`CredWriteW`, in the exact entry
  Python's `keyring` uses (`F1DockTimers/OpenF1`, compound `<user>@...` target, UTF-16 blob,
  legacy `NoctaliaF1/OpenF1` migrated once), or `OPENF1_USERNAME`/`OPENF1_PASSWORD`. The password
  never reaches settings, cache or log; the request headers are never logged; tests inject fake
  credentials and never touch the real store.
- **Panel**: frameless popup anchored to the tray icon or the countdown (any taskbar edge, any
  monitor, clamped to the work area), slide/fade animation, hides on an outside click unless
  pinned, Esc / arrows / R / Ctrl+, keys, header with round, pin and gear, the pill tab bar with the
  same tab list and default-tab choice as Python, footer with "Updated" stamp and offline/error
  note, refresh button.
- **Schedule tab**: circuit name and place, start-light gantry with the lights-out animation,
  countdown, circuit outline with the draw-in animation, sessions table with local start times
  (revised/manual marked), forecast or measured sky/temperature/rain per session, hover details.
- **Weather tab**: live track conditions during a session, the outlook for the running/next
  session with the hour-by-hour strip (hours outside the session greyed), all sessions with
  forecast or measured weather, refresh note and the Open-Meteo CC BY 4.0 credit.
- Visual check: `F1DockTimers.exe --screenshots <dir>` on live data gives Schedule and Weather tabs
  that match `reference-python/docs/screenshots/panel-schedule.png` and `panel-weather.png` in
  layout, fonts, colours and values.
- **Grid tab**: provisional order from qualifying (never shown as final), the official grid with
  penalties when OpenF1 has it, the Official / Provisional switch (saved), "+N places" marks and
  the penalty count. Checked on live data: the Las Vegas official grid with ANT +5 places.
- **Live tab**: running order with team colours, pit stops, gaps, knocked-out state, flag and
  session clock during a session; outside one, the reason (free plan, login, rate limit...) and
  the last session's final classification from OpenF1.
- **Last, Drivers, Teams tabs**: results with fastest lap and laps down, standings with wins, gap
  and points, favourites in the accent colour, Wikipedia links on rows.
- **Spoiler-free cover** for Grid, Live, Last, Drivers and Teams with "Show results", swapped in
  and out as competitive sessions start.
- **Settings dialog**: General, Appearance, Taskbar (live preview, presets, tokens), Formula 1
  (favourites from the standings, weather refresh, spoiler mode, notifications, session-time
  corrections), Live data (Free / stream / Sponsor, username, password to Credential Manager,
  forget), About; same stylesheet, hover tips and wheel guard; export / import of settings with
  the same rules (no machine-specific keys, no passwords). Applying rebuilds the panel and
  restyles, shows or hides the countdown.
- **Settings writes**: the C++ build now writes the shared `settings.json` (atomic replace),
  like the Python app: Save in the dialog, pin, countdown drag/resize, grid switch, "Show
  results", first run and exit. Checked: the Python loader reads a C++-written file with no
  difference in any of the 71 fields.
- **Tray**: icon with live dot, tooltip, click opens/closes the panel, menu (Open panel, Refresh
  data, Settings, Exit); clicking the taskbar countdown opens the panel too.
- **Taskbar countdown, embedded mode** (phase 5, `src/services/taskbar_embed.cpp`): a layered
  Win32 child window of `Shell_TrayWnd`, painted by the same Qt painter into an image and handed
  over with `UpdateLayeredWindow`; click, drag, resize, double-click, right-click menu, hover
  highlight and tooltip forwarded from the window procedure; survives Explorer restarts (the
  guard recreates it), stays above the taskbar's own content, keeps the app buttons of a classic
  taskbar from sliding under it and gives the room back when it goes, falls back to floating if
  the taskbar refuses the window. Checked on this PC: the countdown sits inside the taskbar and
  disappears cleanly on exit.
- **Windows 11 Widgets button**: its position comes from UI Automation on a worker thread every
  5 s (never blocks Explorer, which waits on our UI thread in embedded mode), so the countdown
  docks beside it on a left-aligned taskbar.
- **App manifest** (`src/app.manifest`): declares Windows 8/10/11 compatibility. Without it
  Windows refuses layered child windows, which is why the first embedded attempt failed here while
  the Python app (whose `python.exe` carries such a manifest) worked.
- **Desktop widget** (phase 6, `src/ui/desktop_widget.cpp`): the same panel on the desktop,
  frameless, drag to move, edges to resize (native move/resize), remembers its place per monitor
  (falls back to the top right when the saved monitor is gone), opacity, always on top,
  click-through, lock, double-click action and the right-click menu. `--widget` shows it.
- **Notifications** (phase 6, `src/core/notifications.cpp`, `src/services/notifier.cpp`): reminders
  before race, qualifying and sprint (lead time 0-120 min, no late toast after 5 min) and the top 3
  of a fresh qualifying or race classification (never in spoiler-free mode), as native toasts via
  the tray icon. Sent keys live in the same `notified.json` as the Python app, so the two builds
  never announce one session twice.
- **Tray menu**: Desktop widget, Taskbar countdown, Widget click-through and Widget always on top
  toggles, kept in sync with the settings; middle-click refreshes; the tooltip follows the Python
  app (LIVE with the leader, or the countdown target and race date).
- **Taskbar countdown, floating mode** and **single instance** (`--quit`, a second start opens the
  panel) as in phase 1.
- **Updater** (phase 7, `src/core/update_rules.cpp`, `src/services/updater.cpp`,
  `src/services/wintrust.cpp`): daily check of the latest GitHub release (30 s after start, then
  hourly wake-ups that check once a day, plus "Check for updates" in the tray menu and the
  settings), toast once per version, "Update X available" link in the panel footers, Install /
  Release page / Skip this version / Later. Install downloads the setup asset (GitHub hosts only,
  300 MB cap, progress with Cancel), checks the SHA-256 GitHub publishes and the Authenticode rule
  (signed app needs the same publisher) on a worker thread, then runs the setup silently and
  exits. The signature comes from `WinVerifyTrust` instead of PowerShell. A copy that was not
  installed (a build folder) opens the release page: the C++ build is a folder, so there is no
  self-replacing portable exe. The Store copy never asks GitHub.
- **Start with Windows** (phase 8, `src/services/autostart.cpp`): the Run value `F1DockTimers`
  (the installer's too), in Settings and the tray menu; the old `NoctaliaF1` value is removed. As
  in the Python app the installed copy re-points the entry at itself at start-up (for example
  after the Python build's entry); a build folder never changes it on its own.
- **Release packaging** (phase 8, `packaging/`): `build.ps1` builds, tests and stages
  `dist\F1DockTimers` (exe, Qt, the Visual C++ runtime app-local, assets, licences; no test
  programs), zips it as `F1DockTimers-<version>-portable.zip` and compiles
  `F1DockTimers-<version>-setup.exe` (11.5 MB). `test-installer.ps1` checks a separate test build
  of the installer on this PC: silent upgrade over a Python-style folder, cleanup, Run value,
  Start-menu entry, uninstall, and that the real install is untouched (20 checks pass).
  `.github/workflows/windows-build.yml` builds, tests, smoke-tests and attaches the setup, zip,
  checksums and build attestation to a release, like the Python workflow.
- **Translation template** (phase 8, `tools/make_lang_template.py`): reads the C++ sources for
  `T(QStringLiteral(...))`, the dialog's `tr(...)` and `F1_TR_NOOP(...)` (texts in arrays); the
  template has the Python app's 482 texts minus two that only the Python code has. `ctest` checks
  it is up to date.
- **Live names from known drivers** (after phase 8, mirroring the Python fix of 2026-11-22): when
  the F1 stream's driver list has no three-letter code, rows showing "#6" get the code, colour and
  team from the last session, the official grid and OpenF1's driver list; a name the feed has is
  never overwritten (`fillDriverNames` in `src/services/poller.cpp`, tests in `tst_stream`).
- **Release-key signatures** (after phase 8): updates install only if the release's
  `SHA256SUMS.txt` is signed with the maintainer's ECDSA P-256 release key (checked with Windows
  BCrypt against `src/core/release_key.h`) and the signed list agrees with GitHub's digest; every
  download redirect is checked. Otherwise the update is shown and opens the release page. Key and
  signing scripts in `tools/`, process in `docs/RELEASING.md`. This goes beyond the Python app,
  which trusts GitHub's digest alone.
- **Session pills in Live and Last** (new in 1.1.0, not in the Python app): every session of the
  latest weekend in Live and of the last race weekend in Last, picked from a row of pills. OpenF1
  session lists and classifications are cached per weekend (`src/core/weekend.cpp`, one polite request
  at a time, final after 3 h, a 404 counts as "no classification"); Last takes Race, Qualifying and
  Sprint from Jolpica and adds the starting grid from the race result. Race and sprint rows show
  their points in the last column.
- **Tests**: 145 Qt Test cases in 14 suites plus the template check (tst_updater: a fixture signed by
  the real scripts, valid, missing, other-key, tampered and mismatched signatures). Phase 8 added the
  `NoctaliaF1` migration and the autostart command. Phase 7 had 126 and added `tst_updater`: the Python rules
  (versions, release and asset trust, signature policy, install kind), the check against a local
  server (newer, up to date, no release, daily rule, Store copy), and the full download, verify
  and launch flow with a wrong checksum, an unsigned update for a signed app and a foreign host
  all refused. Phase 6 had 114 cases in 13 suites and added the results notification, reminders
  once, the notifier's `notified.json` across restarts, the widget's saved-place rule and its
  window flags. Phase 5 added `tst_embed`, the port of
  `test_taskbar_embed.py` on the real desktop: the child window lives in the taskbar, is recreated
  after it vanishes, switches to floating and back, docking beside other widgets, the Widgets
  lookup, the memory trim and the negative-offset rule. Phase 4 had 102 cases in 12 suites. Phase 4 added `tst_stream` (a local fake SignalR
  server: negotiate, stream, handshake and Subscribe bodies, parsing, missing or unsafe token,
  back-off, quiet reconnect, and the poller running and stopping the stream). It found a real bug
  before it shipped: the Subscribe call sent the feed list without the outer list, which F1
  would not have accepted. Phase 3 had 98 cases in 11 suites (phase 3 added the table, live and spoiler tabs, the
  grid switch, the settings dialog's live-mode mapping, tooltips and password handling with a fake
  credential store, pill and spoiler saves, and settings export/import). Phase 2 had 89. New in phase 2: `tst_poller` (the poller against a
  fake network: free-plan quiet rules, no credentials on Free, sponsor login and bearer header,
  login failure, 401/429, failed fetch keeps rows, schedule/standings, season transition, backoff,
  forecast window and "off", measured weather once per session, official grid timing and meeting
  check, last-session results, manual offsets, credential target names) and `tst_ui` (popup
  placement on every taskbar edge and scale, tab list and default tab, every tab renders at 100 %
  and 150 % without horizontal overflow, Schedule/Weather content, popup dismiss/pin, circuit
  mapping for the whole season).

## Checklist

| Feature | Python source | Status |
|---|---|---|
| Pure logic: time, sessions, parsers, F1 feed parser, weather, spoilers, teams, i18n `T()` | `core/*`, `i18n.py` | Done, tested |
| Circuit outlines (`tracks.txt` loader and drawing) | `core/tracks.py` | Done, tested |
| Settings file: load/validate/clamp | `settings.py` | Done |
| Settings: save, export/import, machine-specific keys | `settings.py` | Done, tested (shared file, read back by the Python loader) |
| Cache (DataStore) | `services/store.py` | Done |
| HTTP client + rate limiter | `services/http.py` | Done (GET and form POST) |
| Poller: schedule, standings, results, grids, SessionInfo, post-race refresh | `services/poller.py` | Done, tested |
| Poller: OpenF1 Free / Sponsor (results, live, token) | `services/poller.py`, `services/credentials.py` | Done, tested |
| Poller: forecast + measured weather | `services/poller.py` | Done, tested |
| Credentials in Windows Credential Manager (read, migrate; write/delete for the dialog) | `services/credentials.py` | Done |
| Live: F1 timing stream (experimental) | `services/f1stream.py` | Done, tested against a local fake SignalR server and checked on the real feed |
| Tray icon + menu | `app.py` | Done |
| Popup panel (header, tab bar, footer, keys, pin, animation) | `ui/panel.py`, `ui/popup.py` | Done |
| Shared widgets (text, pill tabs, icons, gantry, track view, rows, chips, help marks, tooltip wrapping) | `ui/widgets.py` | Done |
| Schedule tab | `ui/tabs/schedule.py` | Done |
| Weather tab | `ui/tabs/weather.py` | Done |
| Grid, Live, Last, Drivers, Teams tabs | `ui/tabs/tables.py`, `ui/tabs/live.py`, `ui/tabs/common.py` | Done, tested |
| Spoiler-free mode | `core/spoilers.py`, `ui/tabs/tables.py` | Done, tested (cover tab, "Show results", saved reveal time) |
| Settings dialog | `ui/settings_dialog.py` | Done, tested |
| Desktop widget | `ui/desktop_widget.py` | Done |
| Taskbar countdown, floating | `ui/taskbar_pill.py`, `services/winshell.py` | Done, including Widgets-button docking (`services/widgets_button.py`) |
| Taskbar countdown, embedded in the taskbar | `services/taskbar_embed.py` | Done, tested on the real taskbar |
| Notifications (session reminders, results top 3) | `services/notifier.py` | Done (shares `notified.json`) |
| Updater (GitHub releases, download, verify, silent setup) | `services/updater.py` | Done for installed copies; build folders open the release page |
| i18n: language files, `lang/_template.txt` | `i18n.py`, `tools/make_lang_template.py` | Done (generator reads the C++ sources) |
| Start with Windows | `services/autostart.py` | Done (Run key); the Store copy's StartupTask comes with MSIX |
| First-run behaviour (open the panel once), `--show`, `--minimized`, start minimized | `app.py` | Done |
| Idle memory trim | `services/memory.py` | Done |
| MSIX / Microsoft Store package | `build/build-msix.ps1`, `build/store/` | Not started |
| Inno Setup installer | `build/installer.iss` | Done (`packaging/installer.iss`), tested on this PC with a separate test build |
| Portable build | `build/F1DockTimers-portable.spec` | `F1DockTimers-<version>-portable.zip` (a folder, not a single exe) |

## Differences from the Python app (deliberate, for now)

- Portable copies are a zip of the app folder instead of one self-extracting exe, so the updater
  cannot swap them in place; it opens the release page for them (the Python build's portable exe
  replaces itself). Python 1.0.x portable users get the same: their updater finds no
  `-portable.exe` in 1.1.0 and opens the page.
- The Store copy's StartupTask and the MSIX package are not ported yet.
- Phase 3 left some non-ASCII text in `app.cpp` and the settings dialog double-encoded (for
  example `Â·` instead of `·` in the window title and hints); phase 6 fixed it.
- Python's dialog creates the "Team colour chips" box but never puts it on the page; the port keeps
  that (probably a bug in the Python app worth fixing there).
- Since phase 8 the cache (`cache.json`), log (`f1.log`, rotated at 512 kB like Python's) and
  single-instance name are the Python build's, so the two no longer run side by side: a second
  start of either opens the panel of the one already running. Use `NOCTALIA_F1_HOME` for a
  separate test home.
- The live track weather from the F1 stream reaches the Live/Weather tabs and the `{temp}`
  tokens. The Python app parses it but never copies it into its live data, so its "TRACK NOW"
  section never appears; that looks like a small bug in the Python app worth fixing there.

## What is left (phase 3 and later)

About 9,400 lines of C++ and 1,700 lines of tests so far. Remaining, in a sensible order:

| Work | Python lines | Rough size | Risk |
|---|---|---|---|
| MSIX / Microsoft Store package (StartupTask, Store-only update text) | ~150 + manifest | 0.5 session | Store certification |
| First real release: run the workflow, try the setup on a PC with the Python build installed | n/a | small | none expected |

Phase 4 removed the F1 stream client from this list, phase 5 the embedded mode, phase 6 the
desktop widget, notifications and tray menu, phase 7 the updater, phase 8 the installer,
autostart, template generator and the side-by-side workarounds.

Found in the Python app while comparing (worth fixing there): with the PySide6 it ships,
`services/http.py` crashes on a 429 answer (`reply.rawHeader(b"Retry-After")` no longer accepts
bytes), so OpenF1's rate limit is not honoured; the "Team colour chips" box is never on the
settings page; and the F1 stream's live track weather is never copied into the live data.

Honest total: feature parity for the GitHub release is reached; the Store package is the last
piece. Before publishing 1.1.0, run it as the daily app for a while to catch behaviour
differences. The trade-off is now clearer: a quarter
of the download, about half the idle memory, a faster start, idle CPU about the same.
