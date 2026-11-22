"""Parser for F1's live-timing SignalR stream, ported from f1feed.luau.

Experimental and off by default, exactly as in the original. This module only
parses; the network side lives in services/f1stream.py.
"""
from __future__ import annotations

import json
import re

from .sanitize import hex_color, safe
from .timeutil import now_sec, parse_iso
from .weather import parse_live_weather

RS = "\x1e"
MAX_LINE_BYTES = 4 * 1024 * 1024
MAX_DRIVERS = 30

TRACK_STATUS = {
    "AllClear": "CLEAR",
    "Yellow": "YELLOW",
    "Red": "RED",
    "SCDeployed": "SAFETY CAR",
    "VSCDeployed": "VSC",
    "VSCEnding": "VSC ENDING",
}


def _normalise(value):
    if isinstance(value, list):
        return {str(i): _normalise(v) for i, v in enumerate(value)}
    if isinstance(value, dict):
        return {str(k): _normalise(v) for k, v in value.items() if k != "_kf"}
    return value


def _merge(into: dict, patch: dict) -> None:
    for k, v in patch.items():
        if k == "_kf":
            continue
        if isinstance(v, dict) and isinstance(into.get(k), dict):
            _merge(into[k], v)
        else:
            into[k] = v


def _text(v, limit=12) -> str:
    return "" if v is None else safe(v, limit)


def _value_of(node) -> str:
    return _text(node.get("Value")) if isinstance(node, dict) else ""


def _clock_seconds(raw) -> int | None:
    m = re.match(r"^(\d+):(\d\d):(\d\d)", str(raw or ""))
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) if m else None


def _lap_seconds(raw) -> float | None:
    s = str(raw or "")
    m = re.match(r"^(\d+):(\d+\.\d+)$", s)
    if m:
        return int(m.group(1)) * 60 + float(m.group(2))
    m = re.match(r"^(\d+\.\d+)$", s)
    return float(m.group(1)) if m else None


class FeedState:
    def __init__(self) -> None:
        self.feeds: dict[str, dict] = {}
        self.last_message = 0

    def reset(self) -> None:
        self.feeds = {}
        self.last_message = 0

    def handle_line(self, line: str) -> bool:
        """Feed one SSE line. Returns True when the state changed."""
        if len(line) > MAX_LINE_BYTES or not line.startswith("data:"):
            return False
        changed = False
        for frame in line[5:].split(RS):
            payload = frame.strip()
            if not payload.startswith("{"):
                continue
            try:
                msg = json.loads(payload)
            except ValueError:
                continue
            if not isinstance(msg, dict):
                continue
            if msg.get("type") == 3 and isinstance(msg.get("result"), dict):
                for name, data in msg["result"].items():
                    if isinstance(name, str) and isinstance(data, (dict, list)):
                        self.feeds[name] = _normalise(data)
                changed = True
            elif msg.get("type") == 1 and msg.get("target") == "feed" and isinstance(msg.get("arguments"), list):
                args = msg["arguments"]
                if len(args) >= 2 and isinstance(args[0], str) and isinstance(args[1], (dict, list)):
                    cur = self.feeds.setdefault(args[0], {})
                    _merge(cur, _normalise(args[1]))
                    changed = True
        if changed:
            self.last_message = now_sec()
        return changed

    def view(self, now: int) -> dict | None:
        timing = self.feeds.get("TimingData")
        if not isinstance(timing, dict) or not isinstance(timing.get("Lines"), dict):
            return None
        drivers = self.feeds.get("DriverList") or {}
        info = self.feeds.get("SessionInfo") or {}
        kind = _text(info.get("Type"), 24)
        part = 0
        if kind == "Qualifying":
            try:
                part = int(timing.get("SessionPart") or 0)
            except (TypeError, ValueError):
                part = 0

        rows = []
        for number, line in timing["Lines"].items():
            if re.fullmatch(r"\d{1,3}", str(number)) and isinstance(line, dict) and len(rows) < MAX_DRIVERS:
                drv = drivers.get(number) if isinstance(drivers.get(number), dict) else {}
                rows.append(_build_row(number, line, drv, part))
        rows.sort(key=lambda r: (r["order"], r["pos"]))
        for i, r in enumerate(rows, 1):
            r["pos"] = i

        if part > 0:
            leader: dict[int, float] = {}
            for r in rows:
                for p in (1, 2, 3):
                    if p not in leader and r["seconds"].get(p) is not None:
                        leader[p] = r["seconds"][p]
            for r in rows:
                t = r["seconds"].get(r["best_part"])
                f = leader.get(r["best_part"])
                if t is not None and f is not None and t - f > 0.0005:
                    r["gap"] = f"+{t - f:.3f}"
        for r in rows:
            r.pop("seconds", None)
            r.pop("order", None)

        status = (self.feeds.get("TrackStatus") or {}).get("Message")
        clock = None
        ex = self.feeds.get("ExtrapolatedClock")
        if isinstance(ex, dict):
            remaining = _clock_seconds(ex.get("Remaining"))
            if remaining is not None:
                clock = {"remaining": remaining, "at": parse_iso(ex.get("Utc")) or now,
                         "running": ex.get("Extrapolating") is True}
        return {
            "rows": rows,
            "flag": TRACK_STATUS.get(str(status or ""), ""),
            "latest_sec": self.last_message,
            "session": {"name": _text(info.get("Name"), 24), "kind": kind, "part": part,
                        "status": _text(info.get("SessionStatus"), 16)},
            "clock": clock,
            "weather": parse_live_weather(self.feeds.get("WeatherData")),
        }


def _build_row(number: str, line: dict, driver: dict, part: int) -> dict:
    best = gap = state = ""
    out_part = 0
    seconds: dict[int, float | None] = {}
    best_part = 0
    if part > 0:
        times = line.get("BestLapTimes") if isinstance(line.get("BestLapTimes"), dict) else {}
        for p in (1, 2, 3):
            seconds[p] = _lap_seconds(_value_of(times.get(str(p - 1))))
        for p in range(part, 0, -1):
            v = _value_of(times.get(str(p - 1)))
            if v:
                best, best_part = v, p
                if p < part:
                    state = "prev"
                break
        if line.get("KnockedOut") is True:
            state = "out"
            for p in (3, 2, 1):
                if _value_of(times.get(str(p - 1))):
                    out_part = p
                    break
            out_part = out_part or part
        elif line.get("Cutoff") is True:
            state = "danger"
    else:
        to_leader = _text(line.get("GapToLeader"))
        if to_leader:
            best, gap = to_leader, _value_of(line.get("IntervalToPositionAhead"))
        else:
            best = _value_of(line.get("BestLapTime"))

    def _int(v, d=99):
        try:
            return int(v)
        except (TypeError, ValueError):
            return d

    return {
        "pos": _int(line.get("Position")),
        "order": _int(line.get("Line")),
        "num": number,
        "code": _text(driver.get("Tla") or f"#{number}", 4),
        "color": hex_color(driver.get("TeamColour")) or "",
        "team": _text(driver.get("TeamName"), 32),
        "best": best,
        "gap": gap,
        "state": state,
        "out_part": out_part,
        "in_pit": line.get("InPit") is True,
        "pits": _int(line.get("NumberOfPitStops"), 0),
        "seconds": seconds,
        "best_part": best_part,
    }
