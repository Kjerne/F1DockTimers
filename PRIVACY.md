# Privacy policy

F1DockTimers does not collect, store or transmit personal data, has no analytics or telemetry,
and has no servers of its own.

## Network connections

The app only sends plain HTTPS requests that download public Formula 1 data:

| Service | What for | When |
| --- | --- | --- |
| `api.jolpi.ca` (Jolpica-F1) | Calendar, standings, results, qualifying | Every few hours |
| `api.openf1.org` (OpenF1) | Session results; live timing on the Sponsor plan | After sessions; every 15 s near a session with Sponsor |
| `livetiming.formula1.com` | Revised session start times; the live stream only if you choose "OpenF1 Free + F1 live stream" | During race weekends |
| `en.wikipedia.org` | Opens in your browser only when you click a driver, team or race | On click |

These requests contain no personal data. As with any website, the services see your IP address and
the app's user agent (`f1docktimers/<version>`).

## OpenF1 account (optional)

Only if you choose the OpenF1 Sponsor plan and enter your OpenF1 username and password, the app sends
them to `api.openf1.org` to obtain an access token. The password is kept in Windows Credential
Manager on your PC. It is never written to settings files or logs, and never sent anywhere else.

## Data on your PC

Settings are stored in `%APPDATA%\F1DockTimers`. The data cache and the log are stored in
`%LOCALAPPDATA%\F1DockTimers`. The log contains no passwords or tokens. Uninstalling removes the
cache, and you can delete both folders at any time.

## System changes

The app changes your system only when you tell it to: "Start with Windows" adds an entry under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, which you can turn off in Settings, from the
tray menu, or by uninstalling. The installer installs per user, needs no admin rights, and includes
an uninstaller.
