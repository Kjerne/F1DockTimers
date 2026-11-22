# Third-party notices

| Component | Use | License |
| --- | --- | --- |
| [Noctalia Formula 1 plugin](https://github.com/noctalia-dev/community-plugins/tree/main/f1) by gcap0n1 | Design, behaviour and logic ported to Windows | MIT (full text in `docs/NOCTALIA-F1-LICENSE.txt`) |
| [omarchy-f1](https://github.com/Snackwrap/omarchy-f1) by Robert (leafbox) | Origin of the plugin | MIT |
| [f1-circuits](https://github.com/bacinger/f1-circuits) by Tomislav Bacinger | Circuit outlines (`f1win/assets/tracks.txt`, unchanged from the plugin) | MIT |
| [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic) | Bundled typeface | SIL Open Font License 1.1 (`f1win/assets/fonts/OFL.txt`) |
| [Tabler Icons](https://tabler.io/icons) | Flag, pin, gear, refresh glyph paths | MIT |
| [Qt 6 / PySide6](https://www.qt.io/qt-for-python) | UI toolkit | LGPL-3.0 (dynamically linked; the bundled Qt DLLs can be replaced; texts in `licenses/`) |
| [keyring](https://github.com/jaraco/keyring) | Windows Credential Manager access | MIT |
| [tzdata](https://github.com/python/tzdata) | IANA time zone database | Apache-2.0 |

## Data

* **Jolpica-F1** (`api.jolpi.ca`): schedule, standings, results, qualifying. Data under
  [CC BY-NC-SA 4.0](https://github.com/jolpica/jolpica-f1/blob/main/TERMS.md), non-commercial use.
* **OpenF1** (`api.openf1.org`): live and historical session data, and the official starting grid
  after penalties. Live data during sessions requires a paid OpenF1 account; this app never bypasses that.
* **Open-Meteo** (`api.open-meteo.com`): weather forecast for the circuit. Weather data by
  [Open-Meteo.com](https://open-meteo.com/), licensed under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/); used free for non-commercial purposes. The
  app shows the forecast as is (converted to °F / mph if you choose imperial units).
* **F1 live timing** (`livetiming.formula1.com`): `SessionInfo.json` for revised session start
  times, and, only if you opt in, the unofficial live-timing stream. Unofficial and undocumented.

This is an unofficial project, not associated with the Formula 1 companies. F1, FORMULA ONE and
related marks belong to their owners.
