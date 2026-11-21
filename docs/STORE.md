# Publishing F1DockTimers in the Microsoft Store

The Store version is signed by Microsoft, so Windows shows **no SmartScreen warning**, and the
Store keeps it updated. The app notices it runs from the Store: its own update check switches
off, and "Start with Windows" opens Windows' Settings > Apps > Startup (the Store version starts
through its package's start-up task).

## One-time setup (Partner Center)

1. Sign up as an **individual developer** at <https://storedeveloper.microsoft.com> (check the
   current fee and the identity verification steps there).
2. In Partner Center: **Apps and games > New product > MSIX or PWA app**, and reserve the name
   **F1DockTimers**.
3. Open the app's **Product management > Product identity** page and copy:
   - `Package/Identity/Name` (looks like `12345Kjerne.F1DockTimers`)
   - `Package/Identity/Publisher` (looks like `CN=ABCD1234-...`)
   - `Package/Properties/PublisherDisplayName`

## Each release

1. Build the app: `.\build\build.ps1`
2. Build the Store package (the identity from step 3 is already filled in as the default:
   `Kjerne.F1DockTimers`, Store ID `9PD3M7J1SF52`):

   ```powershell
   .\build\build-msix.ps1
   ```

   This writes `dist\F1DockTimers-<version>.msix`. It is unsigned on purpose; the Store signs it.
3. In Partner Center: **Start a submission**, upload the `.msix` under **Packages**, and submit.
   The version must be higher than the last submitted one.

## Store listing (first submission)

- **Pricing:** Free (the F1 data from Jolpica may only be used non-commercially).
- **Category:** Sports. **Age rating:** fill in the questionnaire (no user content, no purchases).
- **Privacy policy URL:** `https://github.com/Kjerne/F1DockTimers/blob/main/PRIVACY.md`
- **Description:** Formula 1 schedule, standings, live timing and a session countdown in your
  Windows taskbar. Free, open source, no ads, no tracking.
- **Screenshots:** the images in `docs/screenshots/`.
- **Restricted capability `runFullTrust`:** Partner Center asks why. Answer: *F1DockTimers is a
  classic Win32 desktop app (Python/Qt) packaged as MSIX; runFullTrust is required for desktop apps.*

## What differs in the Store version

| | GitHub download | Microsoft Store |
| --- | --- | --- |
| SmartScreen warning | Yes (unsigned) | No (signed by Microsoft) |
| Updates | Built-in updater (GitHub) | Microsoft Store |
| Start with Windows | Registry Run entry, switch in the app | Windows Settings > Apps > Startup |
| Settings and cache | `%APPDATA%` / `%LOCALAPPDATA%` | Windows keeps them in the package's own folder |
