"""Windows notifications before race, qualifying and sprint, and the top 3 afterwards (each optional).

Delivered as native Windows toasts through QSystemTrayIcon.showMessage. Each
(season, round, session) is notified at most once, also across restarts.
"""
from __future__ import annotations

from ..i18n import T

import json
import logging
from pathlib import Path

from PySide6.QtCore import QObject, QTimer

from ..core.sessions import SESSION_NAMES, session_ts
from ..core.spoilers import OPENF1_COMPETITIVE, spoilers_hidden
from ..core.timeutil import fmt_datetime, fmt_long, now_sec, zone_for
from ..paths import cache_dir

log = logging.getLogger(__name__)

KINDS = (("race", "notify_race"), ("quali", "notify_quali"), ("sprint", "notify_sprint"))
GRACE_S = 300
RESULT_FRESH_S = 6 * 3600     # only announce results of sessions that ended in the last 6 hours


def result_notification(data: dict, settings, now: int, sent: set[str]) -> tuple[str, str, str] | None:
    """(key, title, message) for a fresh qualifying/sprint/race classification, else None. Pure."""
    if not getattr(settings, "notify_results", False):
        return None
    sess = data.get("session") or {}
    rows = data.get("rows") or []
    if not rows or str(sess.get("kind", "")).lower() not in OPENF1_COMPETITIVE or not sess.get("key"):
        return None
    key = f"result-{sess['key']}"
    end = int(sess.get("end") or 0)
    if key in sent or not end or now - end > RESULT_FRESH_S:
        return None
    top = [r for r in rows if str(r.get("pos", "")).isdigit()][:3]
    if not top:
        return None
    where = sess.get("location") or sess.get("circuit") or ""
    title = T('{where} · {get} results', where=where, get=sess.get('name', '')).strip(" ·")
    msg = "   ".join(f"{r['pos']}. {r.get('code', '')}" for r in top)
    return key, title, msg


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
        store.changed.connect(self._data_changed)

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
                msg = T('Lights out {fmt_long} ({when})', fmt_long=fmt_long(ts - now), when=when)
            else:
                msg = T('{name} starts {fmt_long} ({when})', name=name, fmt_long=fmt_long(ts - now), when=when)
            self.show(f"{race.name} · {name}", msg)
            self._save()

    def _data_changed(self, key: str) -> None:
        if key != "last_session":
            return
        now = now_sec()
        if spoilers_hidden(self.store, self.settings, now):
            return                      # spoiler-free: never announce who won
        hit = result_notification(self.store.get("last_session") or {}, self.settings, now, self.sent)
        if hit is None:
            return
        key, title, msg = hit
        self.sent.add(key)
        self.show(title, msg)
        self._save()

    def _save(self) -> None:
        try:
            keep = sorted(self.sent)[-100:]
            self.file.write_text(json.dumps(keep), encoding="utf-8")
        except OSError:
            log.warning("could not save notification state")
