"""Spoiler-free mode: hide results until the user says they've watched.

A result becomes a spoiler the moment a competitive session *starts* (qualifying, sprint
qualifying, sprint, race), because live timing and then the results follow from that point on.
Pressing "Show results" stores the time of the reveal; everything that started before it is fair
game, until the next competitive session starts. Practice results are not treated as spoilers.
"""
from __future__ import annotations

from .sessions import DURATION_MIN

COMPETITIVE = ("sprint_quali", "quali", "sprint", "race")
OPENF1_COMPETITIVE = ("qualifying", "race")       # OpenF1 session_type (sprint = "Race")
SPOILER_VIEWS = ("grid", "live", "results", "drivers", "constructors")


def latest_spoiler_ts(store, now: int) -> int:
    """Start time of the newest competitive session whose outcome may now be shown anywhere."""
    newest = 0
    race = store.race
    for s in (race.sessions if race else []):
        if s.id in COMPETITIVE and s.ts <= now:
            newest = max(newest, s.ts)
    res = store.get("results") or {}
    if res.get("rows") and res.get("date_ts"):
        newest = max(newest, int(res["date_ts"]))
    last = (store.get("last_session") or {}).get("session") or {}
    if str(last.get("kind", "")).lower() in OPENF1_COMPETITIVE and last.get("start"):
        newest = max(newest, int(last["start"]))
    return newest


def spoilers_hidden(store, settings, now: int) -> bool:
    if not getattr(settings, "spoiler_free", False):
        return False
    newest = latest_spoiler_ts(store, now)
    return newest > 0 and newest > float(getattr(settings, "spoiler_revealed_at", 0) or 0)


def session_over(store, now: int) -> bool:
    """True when no competitive session is running right now (for the hidden-results message)."""
    race = store.race
    for s in (race.sessions if race else []):
        if s.id in COMPETITIVE and s.ts <= now < s.ts + DURATION_MIN.get(s.id, 60) * 60:
            return False
    return True
