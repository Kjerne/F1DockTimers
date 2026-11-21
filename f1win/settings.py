"""Persistent settings (JSON in %APPDATA%\\F1DockTimers\\settings.json).

Never holds secrets: the OpenF1 password lives in Windows Credential Manager
(see services/credentials.py). Only the username is stored here.
"""
from __future__ import annotations

import json
import os
import tempfile
from dataclasses import asdict, dataclass, field, fields
from pathlib import Path

from .paths import config_dir

TABS = ["schedule", "grid", "live", "results", "drivers", "constructors"]


@dataclass
class Settings:
    # General
    start_with_windows: bool = False
    start_minimized: bool = True
    widget_enabled: bool = False
    taskbar_pill: bool = True              # countdown pill next to the tray icons
    taskbar_pill_offset: int = 0           # px further left of the tray (user drag); negative = closer
    pill_embed: bool = False               # live inside the taskbar window (experimental)
    pill_auto_dock: bool = True            # sit beside other apps' taskbar widgets
    pill_style: str = "noctalia"           # noctalia | taskbar | custom
    pill_radius: int = 7                   # 0 = square box
    pill_width: int = 0                    # 0 = automatic
    pill_height: int = 0                   # 0 = automatic, -1 = full taskbar height
    pill_font_px: float = 11.0             # largest text size; auto-fit shrinks below it
    pill_autofit: bool = True
    pill_bold_top: bool = True
    pill_align: str = "left"               # left | center
    pill_lines: int = 2                    # 1 | 2
    pill_border: bool = True
    pill_bg: str = "#1B1B29"
    pill_bg_opacity: int = 90
    pill_border_color: str = "#46464F"
    pill_text: str = "#E4E1EC"
    pill_text2: str = "#BCC2FF"
    pill_live_color: str = "#BCC2FF"
    pill_top: str = "{round} · {race} · {session}"
    pill_bottom: str = "{time} · {countdown}"
    pill_live_top: str = "{round} · {race} · ● {session}"
    pill_live_bottom: str = "{live}"
    default_tab: str = "schedule"          # schedule | drivers | constructors (as original)
    time_format: str = "24h"               # 24h | 12h
    timezone: str = "auto"                 # auto | IANA name
    language: str = "auto"                 # auto (Windows language) | language code from lang/*.txt
    schedule_refresh_h: float = 6.0
    standings_refresh_h: float = 3.0

    # Appearance
    accent: str = "#BCC2FF"
    font_family: str = "Noto Sans"         # Noto Sans (original) | monospace
    font_scale: int = 100                  # percent
    ui_scale: int = 100                    # percent, on top of Windows scaling
    popup_opacity: int = 96                # percent
    widget_opacity: int = 92               # percent (background only)
    animations: bool = True
    widget_always_on_top: bool = False
    widget_click_through: bool = False
    widget_remember_position: bool = True
    widget_locked: bool = False            # "pinned" widget: no drag/resize
    widget_geometry: list = field(default_factory=list)  # [x, y, w, h] logical px
    widget_double_click: str = "refresh"   # refresh | panel | settings | none
    popup_pinned: bool = False

    # Formula 1
    countdown_target: str = "race"         # race | session
    favorite_driver: str = ""
    favorite_team: str = ""
    team_colors: bool = True
    notify_race: bool = False
    notify_quali: bool = False
    notify_sprint: bool = False
    notify_lead_min: int = 10
    notify_results: bool = True           # podium/top 3 after quali, sprint and race
    spoiler_free: bool = False            # hide results until "Show results"
    spoiler_revealed_at: float = 0.0      # when results were last revealed (epoch s)
    session_offsets: dict = field(default_factory=dict)  # {"round_key": "...", "fp1": minutes, ...}
    live_source: str = "openf1"            # openf1 | f1_feed (experimental)
    openf1_plan: str = "free"              # free (historical, no account) | sponsor (live, account)
    openf1_username: str = ""
    debug_force_tabs: bool = False

    # Updates
    check_updates: bool = True             # ask GitHub once a day if a newer release exists
    update_checked_at: float = 0.0
    update_notified: str = ""              # version already announced (one toast per version)
    skipped_version: str = ""

    # ── persistence ─────────────────────────────────────────────────────────

    @staticmethod
    def file() -> Path:
        return config_dir() / "settings.json"

    @classmethod
    def load(cls, file: Path | None = None) -> "Settings":
        f = file or cls.file()
        s = cls()
        try:
            data = json.loads(Path(f).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return s
        if not isinstance(data, dict):
            return s
        for fd in fields(cls):
            if fd.name in data:
                cur = getattr(s, fd.name)
                val = data[fd.name]
                if isinstance(cur, bool):
                    if isinstance(val, bool):
                        setattr(s, fd.name, val)
                elif isinstance(cur, int):
                    if isinstance(val, (int, float)) and not isinstance(val, bool):
                        setattr(s, fd.name, int(val))
                elif isinstance(cur, float):
                    if isinstance(val, (int, float)) and not isinstance(val, bool):
                        setattr(s, fd.name, float(val))
                elif isinstance(cur, str):
                    if isinstance(val, str):
                        setattr(s, fd.name, val)
                elif isinstance(cur, list):
                    if isinstance(val, list):
                        setattr(s, fd.name, val)
                elif isinstance(cur, dict):
                    if isinstance(val, dict):
                        setattr(s, fd.name, val)
        s.clamp()
        return s

    def clamp(self) -> None:
        if self.time_format not in ("24h", "12h"):
            self.time_format = "24h"
        if self.default_tab not in ("schedule", "drivers", "constructors"):
            self.default_tab = "schedule"
        if self.countdown_target not in ("race", "session"):
            self.countdown_target = "race"
        if self.openf1_plan not in ("free", "sponsor"):
            self.openf1_plan = "free"
        if self.live_source not in ("openf1", "f1_feed"):
            self.live_source = "openf1"
        if self.widget_double_click not in ("refresh", "panel", "settings", "none"):
            self.widget_double_click = "refresh"
        self.notify_lead_min = max(0, min(120, self.notify_lead_min))
        self.taskbar_pill_offset = max(-400, min(1600, self.taskbar_pill_offset))   # negative: towards the tray
        if self.pill_style not in ("noctalia", "taskbar", "custom"):
            self.pill_style = "noctalia"
        if self.pill_align not in ("left", "center"):
            self.pill_align = "left"
        self.pill_lines = 1 if self.pill_lines == 1 else 2
        self.pill_radius = max(0, min(30, self.pill_radius))
        self.pill_width = max(0, min(900, self.pill_width))
        self.pill_height = max(-1, min(80, self.pill_height))
        self.pill_font_px = max(7.0, min(28.0, self.pill_font_px))
        self.pill_bg_opacity = max(0, min(100, self.pill_bg_opacity))
        for k in ("pill_top", "pill_bottom", "pill_live_top", "pill_live_bottom"):
            setattr(self, k, getattr(self, k)[:120])
        self.font_scale = max(70, min(160, self.font_scale))
        self.ui_scale = max(70, min(200, self.ui_scale))
        self.popup_opacity = max(40, min(100, self.popup_opacity))
        self.widget_opacity = max(0, min(100, self.widget_opacity))
        self.schedule_refresh_h = max(0.5, min(48.0, self.schedule_refresh_h))
        self.standings_refresh_h = max(0.5, min(48.0, self.standings_refresh_h))
        self.favorite_driver = self.favorite_driver.strip().upper()[:4]
        self.favorite_team = self.favorite_team.strip().lower()[:32]

    # Not carried between PCs: window positions depend on the monitors there.
    MACHINE_SPECIFIC = ("widget_geometry", "start_with_windows", "update_checked_at", "update_notified",
                        "skipped_version", "spoiler_revealed_at")

    def export_to(self, file: Path) -> None:
        """Write a shareable copy. Never contains secrets (the OpenF1 password is
        in Windows Credential Manager, not in Settings)."""
        data = {k: v for k, v in asdict(self).items() if k not in self.MACHINE_SPECIFIC}
        payload = {"app": "F1DockTimers", "kind": "settings", "version": 1, "settings": data}
        Path(file).write_text(json.dumps(payload, indent=2), encoding="utf-8")

    def import_from(self, file: Path) -> list[str]:
        """Read an exported file onto these settings (validated like a normal load).
        Returns the names of the fields that changed. Raises ValueError on a bad file."""
        try:
            payload = json.loads(Path(file).read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            raise ValueError(f"Not a readable settings file ({e.__class__.__name__}).") from e
        data = payload.get("settings") if isinstance(payload, dict) and payload.get("app") in ("F1DockTimers", "NoctaliaF1") else None
        if data is None and isinstance(payload, dict):
            # A plain settings.json copied from another PC or folder also works.
            known = {fd.name for fd in fields(self)}
            if len(known & payload.keys()) >= 5:
                data = payload
        if not isinstance(data, dict):
            raise ValueError("This file is not an F1DockTimers settings export.")
        data = {k: v for k, v in data.items() if k not in self.MACHINE_SPECIFIC}
        tmp_dir = Path(tempfile.mkdtemp(prefix="f1import-"))
        try:
            tmp = tmp_dir / "s.json"
            tmp.write_text(json.dumps(data), encoding="utf-8")
            incoming = Settings.load(tmp)       # same type checks and clamping as a normal load
        finally:
            for f in tmp_dir.glob("*"):
                f.unlink()
            tmp_dir.rmdir()
        changed = []
        for fd in fields(self):
            if fd.name in data and fd.name not in self.MACHINE_SPECIFIC:
                new = getattr(incoming, fd.name)
                if getattr(self, fd.name) != new:
                    setattr(self, fd.name, new)
                    changed.append(fd.name)
        return changed

    def save(self, file: Path | None = None) -> None:
        f = Path(file or self.file())
        f.parent.mkdir(parents=True, exist_ok=True)
        data = json.dumps(asdict(self), indent=2)
        # Atomic replace so a crash mid-write never leaves a corrupt file.
        fd, tmp = tempfile.mkstemp(dir=str(f.parent), prefix=".settings-", suffix=".tmp")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as fh:
                fh.write(data)
            os.replace(tmp, f)
        except OSError:
            try:
                os.unlink(tmp)
            except OSError:
                pass
            raise
