"""Windows notifications before race, qualifying and sprint (each optional).

Delivered as native Windows toasts through QSystemTrayIcon.showMessage. Each
(season, round, session) is notified at most once, also across restarts.
"""
from __future__ import annotations

import json
import logging
from pathlib import Path

from PySide6.QtCore import QObject, QTimer

from ..core.sessions import SESSION_NAMES, session_ts
from ..core.timeutil import fmt_datetime, fmt_long, now_sec, zone_for
from ..paths import cache_dir

log = logging.getLogger(__name__)

KINDS = (("race", "notify_race"), ("quali", "notify_quali"), ("sprint", "notify_sprint"))
GRACE_S = 300


def due_notifications(race, settings, now: int, sent: set[str]) -> list[tuple[str, str, int]]:
    """[(key, session id, ts)] that should fire now. Pure, for tests."""
    if race is None:
        return []
    out = []
    lead = max(0, min(120, int(settings.notify_lead_min))) * 60
    for sid, flag in KINDS:
        if not getattr(settings, flag, False):
            continue
        ts = session_ts(race.sessions, sid)
        if ts is None:
            continue
        key = f"{race.season}-{race.round}-{sid}"
        if key in sent:
            continue
        if ts - lead <= now < ts + GRACE_S:
            out.append((key, sid, ts))
    return out


class Notifier(QObject):
    def __init__(self, store, settings, show, file: Path | None = None, parent=None) -> None:
        super().__init__(parent)
        self.store = store
        self.settings = settings
        self.show = show  # callable(title, message)
        self.file = Path(file or cache_dir() / "notified.json")
        self.sent: set[str] = set()
        try:
            data = json.loads(self.file.read_text(encoding="utf-8"))
            if isinstance(data, list):
                self.sent = {str(x) for x in data}
        except (OSError, ValueError):
            pass
        self.timer = QTimer(self)
        self.timer.setInterval(15000)
        self.timer.timeout.connect(self.check)

    def start(self) -> None:
        self.timer.start()
        QTimer.singleShot(3000, self.check)

    def check(self) -> None:
        now = now_sec()
        race = self.store.race
        for key, sid, ts in due_notifications(race, self.settings, now, self.sent):
            self.sent.add(key)
            zone = zone_for(self.settings.timezone)
            when = fmt_datetime(ts, zone, self.settings.time_format == "12h")
            name = SESSION_NAMES.get(sid, sid)
            if sid == "race":
                msg = f"Lights out {fmt_long(ts - now)} ({when})"
            else:
                msg = f"{name} starts {fmt_long(ts - now)} ({when})"
            self.show(f"{race.name} · {name}", msg)
            self._save()

    def _save(self) -> None:
        try:
            keep = sorted(self.sent)[-100:]
            self.file.write_text(json.dumps(keep), encoding="utf-8")
        except OSError:
            log.warning("could not save notification state")
