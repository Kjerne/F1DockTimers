"""Session maths, ported from shared.luau and service.luau."""
from __future__ import annotations

from dataclasses import dataclass, field

from .timeutil import parse_utc

SESSION_NAMES = {
    "fp1": "Practice 1",
    "fp2": "Practice 2",
    "fp3": "Practice 3",
    "sprint_quali": "Sprint Quali",
    "sprint": "Sprint",
    "quali": "Qualifying",
    "race": "Race",
}
SESSION_IDS = list(SESSION_NAMES)

# Nominal running time, used only to say "LIVE" in the countdown when no live
# feed is available. The live window for polling is the original's 15 min
# before to 3 h after.
DURATION_MIN = {"fp1": 60, "fp2": 60, "fp3": 60, "sprint_quali": 45, "sprint": 60, "quali": 60, "race": 120}

LIVE_BEFORE = 15 * 60
LIVE_AFTER = 3 * 3600


@dataclass
class Session:
    id: str
    ts: int
    delay: int = 0          # minutes against the published time
    source: str = "published"  # published | feed | manual

    @property
    def name(self) -> str:
        return SESSION_NAMES.get(self.id, self.id)

    def to_dict(self) -> dict:
        return {"id": self.id, "ts": self.ts, "delay": self.delay, "source": self.source}

    @staticmethod
    def from_dict(d: dict) -> "Session":
        return Session(str(d["id"]), int(d["ts"]), int(d.get("delay", 0)), str(d.get("source", "published")))


@dataclass
class Race:
    season: str
    round: str
    name: str
    circuit_id: str
    circuit_name: str
    locality: str
    country: str
    url: str
    sessions: list[Session] = field(default_factory=list)  # effective, sorted
    raw_sessions: list[Session] = field(default_factory=list)  # as published

    def to_dict(self) -> dict:
        return {
            "season": self.season, "round": self.round, "name": self.name,
            "circuit_id": self.circuit_id, "circuit_name": self.circuit_name,
            "locality": self.locality, "country": self.country, "url": self.url,
            "raw_sessions": [s.to_dict() for s in self.raw_sessions],
        }

    @staticmethod
    def from_dict(d: dict) -> "Race":
        r = Race(
            str(d.get("season", "")), str(d.get("round", "")), str(d.get("name", "")),
            str(d.get("circuit_id", "")), str(d.get("circuit_name", "")),
            str(d.get("locality", "")), str(d.get("country", "")), str(d.get("url", "")),
        )
        r.raw_sessions = [Session.from_dict(s) for s in d.get("raw_sessions", [])]
        r.sessions = [Session(s.id, s.ts, 0, "published") for s in r.raw_sessions]
        return r

    @property
    def is_sprint_weekend(self) -> bool:
        return any(s.id in ("sprint", "sprint_quali") for s in self.raw_sessions)

    @property
    def location(self) -> str:
        if self.locality and self.country:
            return f"{self.locality}, {self.country}"
        return self.locality or self.country


def build_sessions(race: dict) -> list[Session]:
    """Sorted session list from one Jolpica race object."""
    if not isinstance(race, dict):
        return []
    raw = [
        ("fp1", race.get("FirstPractice")),
        ("fp2", race.get("SecondPractice")),
        ("fp3", race.get("ThirdPractice")),
        ("sprint_quali", race.get("SprintQualifying") or race.get("SprintShootout")),
        ("sprint", race.get("Sprint")),
        ("quali", race.get("Qualifying")),
        ("race", {"date": race.get("date"), "time": race.get("time")}),
    ]
    out = []
    for sid, s in raw:
        if isinstance(s, dict):
            ts = parse_utc(s.get("date"), s.get("time"))
            if ts is not None:
                out.append(Session(sid, ts))
    out.sort(key=lambda s: s.ts)
    return out


def effective_sessions(raw: list[Session], auto_ts: dict, manual_min: dict) -> list[Session]:
    """Published times, overridden by the timing feed, overridden by manual minutes."""
    out = []
    for base in raw:
        source = "published"
        ts = base.ts
        if base.id in auto_ts:
            ts, source = int(auto_ts[base.id]), "feed"
        minutes = int(manual_min.get(base.id, 0) or 0)
        if minutes:
            ts, source = base.ts + minutes * 60, "manual"
        out.append(Session(base.id, ts, (ts - base.ts) // 60, source))
    out.sort(key=lambda s: s.ts)
    return out


def session_ts(sessions, sid: str) -> int | None:
    for s in sessions or []:
        if s.id == sid:
            return s.ts
    return None


def next_session(sessions, now: int) -> Session | None:
    for s in sessions or []:
        if s.ts > now:
            return s
    return None


def current_session(sessions, now: int) -> Session | None:
    """The session that is nominally running at ``now``, by its usual duration."""
    for s in sessions or []:
        if s.ts <= now < s.ts + DURATION_MIN.get(s.id, 60) * 60:
            return s
    return None


def near_session(sessions, now: int) -> bool:
    return any(s.ts - LIVE_BEFORE <= now <= s.ts + LIVE_AFTER for s in sessions or [])


def window_end(sessions, now: int) -> int | None:
    ends = [s.ts + LIVE_AFTER for s in sessions or [] if s.ts - LIVE_BEFORE <= now <= s.ts + LIVE_AFTER]
    return max(ends) if ends else None


def grid_applicable(sessions, now: int) -> bool:
    q = session_ts(sessions, "quali")
    r = session_ts(sessions, "race")
    if q is None or r is None:
        return False
    return q + 3600 <= now < r


def weekend_window(sessions) -> tuple[int, int] | None:
    if not sessions:
        return None
    return min(s.ts for s in sessions), max(s.ts for s in sessions)


def live_active(live: dict | None, sessions, now: int, debug: bool = False) -> bool:
    """Live only near a session AND with a fresh position feed."""
    if not isinstance(live, dict) or not live.get("rows"):
        return False
    if debug:
        return True
    if not near_session(sessions, now):
        return False
    latest = live.get("latest_sec") or 0
    return latest > 0 and (now - latest) < 15 * 60
