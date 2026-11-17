# Phase 1: findings from the original Noctalia F1 plugin

Inspected on 2026-11-17 from `noctalia-dev/community-plugins` (`main`, folder `f1`, plugin version 0.1.2).

## What was read

| File | Lines | Role |
| --- | --- | --- |
| `plugin.toml` | 149 | Manifest, 11 settings, panel 500x640, capture keys Esc/Left/Right/R |
| `shared.luau` | 318 | Time parsing, countdown formatting, session list, live/grid windows, team colours, flag names |
| `service.luau` | 833 | Poller: Jolpica, SessionInfo.json, OpenF1, backoff, notification, manual delay |
| `panel.luau` | 696 | Six tabs, circuit SVG, start-lights gantry, intro animation, keys |
| `bar.luau` | 94 | Bar pill: short countdown, LIVE state, tooltip |
| `f1feed.luau` | 265 | Parser for F1 SignalR live-timing stream (experimental) |
| `f1-live.sh` | 59 | curl wrapper for the SignalR stream |
| `tracks.txt` | 42 | 40 circuit outlines (unit box) keyed by Ergast circuitId |
| `tools/build-tracks.py` | 86 | Builds tracks.txt from bacinger/f1-circuits |
| `translations/en.json` | 158 | Every UI string |
| `screenshots/*.webp` | 4 | schedule, grid, drivers, constructors |
| `LICENSE` | 31 | MIT (gcap0n1, leafbox, Tomislav Bacinger) |

Important correction to the brief: the plugin is **Luau on Noctalia's native UI API**, not QML.
There is no QML to reuse. The UI primitives (`ui.row`, `ui.label`, `ui.button` ...) were
recreated as Qt Widgets with custom painting.

## Visual spec (measured)

Reference screenshots are 598x770 px for a 500x640 panel, so the shot is at ~1.2x scale.

| Token | Value | Source |
| --- | --- | --- |
| surface | `#1B1B29` (blurred, translucent in Noctalia) | sampled |
| on_surface (text) | `#E4E1EC` | M3 tonal match of sampled `#CFCED3` AA text |
| on_surface_variant (muted) | `#C6C5D0` | sampled |
| primary (accent) | `#BCC2FF` | sampled tab fill |
| on_primary | `#262C67` | sampled tab text |
| secondary (delay note) | `#C4C4DD` | sampled |
| outline_variant (divider) | `#46464F` | sampled `#434351` |
| light on / go / off | `#E10600` / `#1FBF5B` / `#3A3F44` @0.55 | panel.luau |

Font sizes (logical px, panel.luau): title 20 bold, circuit 14, location 12, countdown 22 bold accent,
section header 11 bold muted, table 13, gap 12, tab buttons 12. Panel gap 10, rows paddingV 3 / paddingH 4,
team chip 3x16 radius 1, lamps 12 px with 7 px column gap and 3 px row gap, track image 170x150 at 0.85 opacity,
stroke = 2.2% of span, pad 8%, start/finish tick 3.5% half-length and 1.8x stroke.
Typeface appears to be Noto Sans; bundled (OFL).

## Behaviour

* Tabs: Schedule, Grid (only from quali+1h until race start), Live (label `● Live` when live), Last, Drivers, Teams.
* Opening tab: Live if live, Grid if applicable, else configured default (schedule/drivers/constructors).
* Keys: Left/Right cycle tabs, R refresh, Esc close.
* Countdown: `in 4d 21h 36m`, `live now` at zero; bar short form `4d`/`3h`/`12m`/`<1m`.
* Start lights: 5 columns x 2 lamps; intro lights one column per 620 ms, holds 1100 ms; red until race start, green after; circuit fades in over 280 ms.
* Session rows: next session bold accent, past sessions muted, `+30 min vs. published time` note.
* Session times: Jolpica planned times, overridden by F1 `SessionInfo.json` (polled each minute from 3 h before first session to 4 h after race, ignored if > 12 h off), overridden again by manual delay.
* Live window: 15 min before a session until 3 h after; OpenF1 polled every 15 s; live only if newest position < 15 min old.
* Retry: 1, 2, 4 ... min capped at 15 min; failed datasets keep the previous rows.
* Schedule every 6 h, standings and last result every 3 h, extra refresh once per round 3 h 15 min after race start.
* Notification: one per round, `notify_lead_min` (default 10) before the race.
* Remote text sanitised (control and bidi characters stripped, length clamped). Links only to `*.wikipedia.org` over https.

## Linux dependency map

| Linux / Noctalia | Windows implementation |
| --- | --- |
| Noctalia bar pill + attached panel | `QSystemTrayIcon` + frameless popup anchored to the tray icon geometry |
| `noctalia.state` shared store | In-process `DataStore` (Qt signals) + JSON disk cache |
| `noctalia.http` | `QNetworkAccessManager` (async, no thread blocking) |
| `noctalia.notify` | `QSystemTrayIcon.showMessage` (native Windows toast) |
| `xdg-open` | `QDesktopServices.openUrl` (same Wikipedia allow-list) |
| `OPENF1_USERNAME/PASSWORD` env | Windows Credential Manager via `keyring` (env vars still honoured) |
| `f1-live.sh` + curl + pkill | Native `QNetworkAccessManager` SSE client with cookie jar, no subprocess |
| `noctalia.pluginDataDir()` | `%LOCALAPPDATA%\NoctaliaF1` |
| `noctalia msg ... delay` IPC | Settings dialog: per-session minute corrections |
| Theme colours from Noctalia | Fixed palette above + accent override setting |

## Licensing

Plugin MIT (gcap0n1; ported from Snackwrap/omarchy-f1, MIT, Robert (leafbox)); circuit data MIT (Tomislav Bacinger).
Jolpica data CC BY-NC-SA 4.0 non-commercial. F1 live-timing stream is unofficial and undocumented; kept
opt-in and off by default exactly like the original, with the same warning. OpenF1 live data requires a paid
account; no bypass.

## Parity checklist

- [x] Header: flag glyph, `FORMULA 1`, `Round N · YYYY`, divider
- [x] Title, pill tabs, circuit, locality, lights gantry, countdown, sessions, delay note
- [x] Circuit outline from tracks.txt with start/finish tick, fade-in
- [x] Grid tab window and provisional label
- [x] Live tab: idle/auth/rate/login messages, badge, flag, clock, rows, Q-part elimination
- [x] Last, Drivers (gap to leader), Teams
- [x] Team chips, favourite highlight
- [x] Keyboard: Left/Right/R/Esc
- [x] SessionInfo.json delay tracking + manual corrections
- [x] Backoff, keep-last-data on failure, sanitisation, link allow-list
- [x] Race notification with lead time (extended: quali and sprint too)
- [x] Optional experimental F1 stream (off by default)
