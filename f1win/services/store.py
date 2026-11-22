"""Shared in-process data store (the noctalia.state equivalent) + disk cache.

The tray popup and the desktop widget both read from one DataStore, so the
data is fetched once and shown everywhere.
"""
from __future__ import annotations

import json
import logging
import os
import tempfile
import time
from pathlib import Path

from PySide6.QtCore import QObject, Signal

from ..core.sessions import Race
from ..paths import cache_dir

log = logging.getLogger(__name__)
CACHE_VERSION = 2
PERSISTED = ("drivers", "constructors", "results", "grid", "last_session", "weather", "grid_official", "measured_weather")
ROWLESS = ("weather", "measured_weather")


def empty(key: str) -> dict:
    base = {"loaded": False, "failed": False, "rows": []}
    if key == "live":
        return {"rows": [], "flag": "", "latest_sec": 0, "error": "", "source": "openf1",
                "session": None, "clock": None, "messages": []}
    return base


class DataStore(QObject):
    changed = Signal(str)

    def __init__(self, cache_file: Path | None = None, parent=None) -> None:
        super().__init__(parent)
        self.cache_file = Path(cache_file or cache_dir() / "cache.json")
        self.race: Race | None = None
        self.auto_ts: dict[str, int] = {}          # session id -> feed start (current round)
        self.data: dict[str, dict] = {k: empty(k) for k in PERSISTED + ("live",)}
        self.status = {"loading": True, "error": "", "offline": False}
        self.fetched: dict[str, float] = {}       # key -> wall-clock seconds of last success
        self.from_cache = False

    # ── access ──────────────────────────────────────────────────────────────

    def get(self, key: str) -> dict:
        return self.data.get(key) or empty(key)

    def set(self, key: str, value: dict, success: bool = True) -> None:
        self.data[key] = value
        if success and key != "live":
            self.fetched[key] = time.time()
        self.changed.emit(key)

    def set_race(self, race: Race | None, success: bool = True) -> None:
        self.race = race
        if success:
            self.fetched["race"] = time.time()
        self.changed.emit("race")

    def set_status(self, loading: bool | None = None, error: str | None = None, offline: bool | None = None) -> None:
        if loading is not None:
            self.status["loading"] = loading
        if error is not None:
            self.status["error"] = error
        if offline is not None:
            self.status["offline"] = offline
        self.changed.emit("status")

    def last_updated(self) -> float:
        vals = [v for k, v in self.fetched.items() if k in ("race",) + PERSISTED]
        return max(vals) if vals else 0.0

    # ── cache ───────────────────────────────────────────────────────────────

    def save_cache(self) -> None:
        payload = {
            "version": CACHE_VERSION,
            "saved": time.time(),
            "race": self.race.to_dict() if self.race else None,
            "auto_ts": self.auto_ts,
            "fetched": self.fetched,
            "data": {k: self.data[k] for k in PERSISTED if self.data[k].get("loaded")},
        }
        try:
            self.cache_file.parent.mkdir(parents=True, exist_ok=True)
            fd, tmp = tempfile.mkstemp(dir=str(self.cache_file.parent), prefix=".cache-", suffix=".tmp")
            with os.fdopen(fd, "w", encoding="utf-8") as fh:
                json.dump(payload, fh)
            os.replace(tmp, self.cache_file)
        except OSError:
            log.warning("could not write cache")

    def load_cache(self) -> bool:
        try:
            payload = json.loads(self.cache_file.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return False
        if not isinstance(payload, dict) or payload.get("version") != CACHE_VERSION:
            return False
        try:
            if isinstance(payload.get("race"), dict):
                self.race = Race.from_dict(payload["race"])
            self.auto_ts = {str(k): int(v) for k, v in (payload.get("auto_ts") or {}).items()}
            self.fetched = {str(k): float(v) for k, v in (payload.get("fetched") or {}).items()}
            for k, v in (payload.get("data") or {}).items():
                # Tables need their rows; the weather data has its own shape (hours / sessions).
                if k in PERSISTED and isinstance(v, dict) and (isinstance(v.get("rows"), list) or k in ROWLESS):
                    v["failed"] = False
                    self.data[k] = v
        except (TypeError, ValueError, KeyError):
            log.warning("cache unreadable; ignoring")
            self.race, self.auto_ts, self.fetched = None, {}, {}
            self.data = {k: empty(k) for k in PERSISTED + ("live",)}
            return False
        self.from_cache = True
        self.status["loading"] = False
        return True

    def clear_cache(self) -> None:
        try:
            self.cache_file.unlink()
        except OSError:
            pass
        self.fetched = {}
