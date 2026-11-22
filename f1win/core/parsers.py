"""Pure parsers: API JSON -> plain dict rows (JSON-cacheable, UI-ready).

Ported from the load* functions in service.luau, extended with wins, dates,
finishing status and fastest lap where Jolpica provides them. Nothing here
invents data: missing fields become empty strings.
"""
from __future__ import annotations

from .sanitize import bounded, hex_color, path, safe, safe_link
from .sessions import Race, build_sessions, _coord
from .timeutil import parse_iso, parse_local, parse_utc


def _num(v, default=0.0) -> float:
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def _fmt_pts(v) -> str:
    n = _num(v, None)  # type: ignore[arg-type]
    if n is None:
        return safe(v, 6)
    return str(int(n)) if n == int(n) else f"{n:g}"


def driver_code(driver) -> str:
    if not isinstance(driver, dict):
        return "?"
    if driver.get("code"):
        return safe(driver["code"], 4)
    return safe(str(driver.get("familyName") or "?")[:3].upper(), 4)


def driver_name(driver) -> str:
    if not isinstance(driver, dict):
        return ""
    return safe(f"{driver.get('givenName', '')} {driver.get('familyName', '')}".strip(), 40)


def _link(obj) -> str:
    return safe_link(obj.get("url")) or "" if isinstance(obj, dict) else ""


# ── Jolpica ──────────────────────────────────────────────────────────────────

def parse_race(r) -> Race | None:
    if not isinstance(r, dict):
        return None
    circuit = r.get("Circuit") if isinstance(r.get("Circuit"), dict) else {}
    loc = circuit.get("Location") if isinstance(circuit.get("Location"), dict) else {}
    race = Race(
        season=safe(r.get("season"), 8),
        round=safe(r.get("round"), 4),
        name=safe(r.get("raceName"), 48),
        circuit_id=safe(circuit.get("circuitId"), 40),
        circuit_name=safe(circuit.get("circuitName"), 48),
        locality=safe(loc.get("locality"), 32),
        country=safe(loc.get("country"), 32),
        url=_link(r),
    )
    race.lat, race.lon = _coord(loc.get("lat"), 90), _coord(loc.get("long"), 180)
    race.raw_sessions = build_sessions(r)
    race.sessions = list(race.raw_sessions)
    if not race.raw_sessions:
        return None
    return race


def parse_next_race(data) -> Race | None:
    races = path(data, "MRData", "RaceTable", "Races")
    return parse_race(races[0]) if isinstance(races, list) and races else None


def first_future_race(data, now: int) -> Race | None:
    """From a season calendar, the first race whose start is still ahead (or live)."""
    races = path(data, "MRData", "RaceTable", "Races")
    for r in bounded(races, 40):
        race = parse_race(r)
        if race is None:
            continue
        race_ts = next((s.ts for s in race.raw_sessions if s.id == "race"), None)
        if race_ts is not None and race_ts + 3 * 3600 > now:
            return race
    return None


def parse_driver_standings(data) -> dict:
    lists = bounded(path(data, "MRData", "StandingsTable", "StandingsLists"), 4)
    entries = bounded(lists[0].get("DriverStandings") if lists else None, 60)
    leader = _num(entries[0].get("points")) if entries else 0
    rows = []
    for i, e in enumerate(entries):
        cons = e.get("Constructors") if isinstance(e.get("Constructors"), list) else []
        con = cons[-1] if cons else {}
        pts = _num(e.get("points"))
        rows.append({
            "pos": safe(e.get("positionText") or e.get("position"), 3),
            "code": driver_code(e.get("Driver")),
            "name": driver_name(e.get("Driver")),
            "team": safe(con.get("name"), 32),
            "team_id": safe(con.get("constructorId"), 32),
            "pts": _fmt_pts(e.get("points")),
            "wins": int(_num(e.get("wins"))),
            "gap": 0 if i == 0 else int(leader - pts),
            "url": _link(e.get("Driver")),
        })
    season = safe(lists[0].get("season"), 8) if lists else ""
    rnd = safe(lists[0].get("round"), 4) if lists else ""
    return {"rows": rows[:30], "season": season, "round": rnd}


def parse_constructor_standings(data) -> dict:
    lists = bounded(path(data, "MRData", "StandingsTable", "StandingsLists"), 4)
    entries = bounded(lists[0].get("ConstructorStandings") if lists else None, 60)
    leader = _num(entries[0].get("points")) if entries else 0
    rows = []
    for i, e in enumerate(entries):
        c = e.get("Constructor") if isinstance(e.get("Constructor"), dict) else {}
        pts = _num(e.get("points"))
        rows.append({
            "pos": safe(e.get("positionText") or e.get("position"), 3),
            "name": safe(c.get("name"), 32),
            "team_id": safe(c.get("constructorId"), 32),
            "pts": _fmt_pts(e.get("points")),
            "wins": int(_num(e.get("wins"))),
            "gap": 0 if i == 0 else int(leader - pts),
            "url": _link(c),
        })
    season = safe(lists[0].get("season"), 8) if lists else ""
    rnd = safe(lists[0].get("round"), 4) if lists else ""
    return {"rows": rows[:15], "season": season, "round": rnd}


def parse_results(data) -> dict:
    races = bounded(path(data, "MRData", "RaceTable", "Races"), 4)
    r0 = races[0] if races else {}
    entries = bounded(r0.get("Results") if isinstance(r0, dict) else None, 60)
    rows = []
    fastest = None
    try:
        lead_laps = int(entries[0].get("laps")) if entries else 0
    except (TypeError, ValueError):
        lead_laps = 0
    for e in entries:
        con = e.get("Constructor") if isinstance(e.get("Constructor"), dict) else {}
        timed = path(e, "Time", "time")
        status = safe(e.get("status"), 24)
        try:
            down = lead_laps - int(e.get("laps"))
        except (TypeError, ValueError):
            down = 0
        if status == "Lapped" or (status.startswith("+") and "Lap" in status):
            # Jolpica's Time for a lapped car is measured within its own lap, so
            # "+4.427" would read as on the lead lap. Show the laps down instead.
            timed = f"+{down} Lap{'s' if down != 1 else ''}" if down > 0 else (status or timed)
        rows.append({
            "pos": safe(e.get("positionText"), 3),
            "code": driver_code(e.get("Driver")),
            "name": driver_name(e.get("Driver")),
            "team": safe(con.get("name"), 32),
            "team_id": safe(con.get("constructorId"), 32),
            "detail": safe(timed or status, 24),
            "status": status,
            "pts": _fmt_pts(e.get("points")),
            "grid": safe(e.get("grid"), 3),
            "url": _link(e.get("Driver")),
        })
        fl = e.get("FastestLap")
        if isinstance(fl, dict) and str(fl.get("rank")) == "1":
            fastest = {
                "code": driver_code(e.get("Driver")),
                "time": safe(path(fl, "Time", "time"), 12),
                "lap": safe(fl.get("lap"), 4),
            }
    date_ts = parse_utc(r0.get("date"), r0.get("time")) if isinstance(r0, dict) else None
    return {
        "race_name": safe(r0.get("raceName"), 48) if isinstance(r0, dict) else "",
        "season": safe(r0.get("season"), 8) if isinstance(r0, dict) else "",
        "round": safe(r0.get("round"), 4) if isinstance(r0, dict) else "",
        "date_ts": date_ts or 0,
        "circuit": safe(path(r0, "Circuit", "circuitName"), 48),
        "fastest": fastest,
        "rows": rows[:30],
    }


def parse_qualifying(data) -> dict:
    races = bounded(path(data, "MRData", "RaceTable", "Races"), 4)
    r0 = races[0] if races else {}
    entries = bounded(r0.get("QualifyingResults") if isinstance(r0, dict) else None, 60)
    rows = []
    for q in entries:
        con = q.get("Constructor") if isinstance(q.get("Constructor"), dict) else {}
        rows.append({
            "pos": safe(q.get("position"), 3),
            "code": driver_code(q.get("Driver")),
            "name": driver_name(q.get("Driver")),
            "team": safe(con.get("name"), 32),
            "team_id": safe(con.get("constructorId"), 32),
            "detail": safe(q.get("Q3") or q.get("Q2") or q.get("Q1") or "", 16),
            "url": _link(q.get("Driver")),
        })
    return {
        "round": safe(r0.get("round"), 4) if isinstance(r0, dict) else "",
        "season": safe(r0.get("season"), 8) if isinstance(r0, dict) else "",
        "rows": rows[:30],
    }


def parse_openf1_starting_grid(arr, drivers: dict, provisional: list[dict]) -> list[dict]:
    """OpenF1 /starting_grid (the official grid, penalties applied) -> Grid rows. Names, teams and
    links come from the provisional (qualifying) rows; ``moved`` is the change from that order."""
    by_code = {r.get("code"): r for r in provisional or []}
    rows = []
    for e in bounded(arr, 30):
        if not isinstance(e, dict):
            continue
        k = _driver_key(e.get("driver_number"))
        try:
            pos = int(e.get("position"))
        except (TypeError, ValueError):
            continue
        if not k or not 1 <= pos <= 30:
            continue
        info = drivers.get(k, {})
        code = info.get("acr") or f"#{k}"
        q = by_code.get(code, {})
        try:
            moved = pos - int(q.get("pos"))
        except (TypeError, ValueError):
            moved = 0
        rows.append({"pos": str(pos), "code": code, "name": q.get("name", ""),
                     "team": q.get("team") or info.get("team", ""), "team_id": q.get("team_id", ""),
                     "color": info.get("color", ""), "url": q.get("url", ""),
                     "detail": fmt_lap(e.get("lap_duration")), "moved": moved})
    rows.sort(key=lambda r: int(r["pos"]))
    return rows


def summarise_openf1_weather(arr) -> dict | None:
    """OpenF1 /weather for one session -> what it was like: air range, warmest track, rain or not."""
    air, track, hum, wind, rain = [], [], [], [], False
    for e in bounded(arr, 2000):
        if not isinstance(e, dict):
            continue
        for key, out in (("air_temperature", air), ("track_temperature", track), ("humidity", hum),
                         ("wind_speed", wind)):
            v = e.get(key)
            if isinstance(v, (int, float)) and -50 < v < 90:
                out.append(float(v))
        if e.get("rainfall") not in (0, None, False, "0"):
            rain = True
    if not air:
        return None
    return {"air_min": min(air), "air_max": max(air), "track_max": max(track) if track else None,
            "track_min": min(track) if track else None, "humidity": sum(hum) / len(hum) if hum else None,
            "wind_max": max(wind) * 3.6 if wind else None, "rain": rain, "samples": len(air)}


def grid_from_results(results: dict) -> list[dict]:
    """Official starting grid from a finished race's ``grid`` field (0 = pit lane)."""
    rows = []
    for r in results.get("rows", []):
        g = r.get("grid", "")
        if not str(g).isdigit():
            continue
        rows.append({**r, "pos": g if g != "0" else "PL", "detail": "Pit lane" if g == "0" else ""})
    rows.sort(key=lambda r: (r["pos"] == "PL", int(r["pos"]) if r["pos"].isdigit() else 99))
    return rows


# ── F1 timing feed ───────────────────────────────────────────────────────────

FEED_SESSIONS = {
    "Practice 1": "fp1", "Practice 2": "fp2", "Practice 3": "fp3",
    "Sprint Qualifying": "sprint_quali", "Sprint Shootout": "sprint_quali",
    "Sprint": "sprint", "Qualifying": "quali", "Race": "race",
}
MAX_FEED_SHIFT = 12 * 3600


def parse_session_info(info, race: Race) -> tuple[str, int] | None:
    """SessionInfo.json -> (session id, start ts) if it belongs to ``race``."""
    if not isinstance(info, dict) or race is None:
        return None
    meeting = info.get("Meeting") if isinstance(info.get("Meeting"), dict) else {}
    try:
        if int(meeting.get("Number")) != int(race.round):
            return None
    except (TypeError, ValueError):
        return None
    sid = FEED_SESSIONS.get(str(info.get("Name") or ""))
    ts = parse_local(info.get("StartDate"), info.get("GmtOffset"))
    if sid is None or ts is None:
        return None
    base = next((s for s in race.raw_sessions if s.id == sid), None)
    if base is None or abs(ts - base.ts) > MAX_FEED_SHIFT:
        return None
    return sid, ts


# ── OpenF1 ───────────────────────────────────────────────────────────────────

def _driver_key(n) -> str | None:
    k = str("" if n is None else n)
    return k if k.isdigit() and len(k) <= 3 else None


def parse_openf1_drivers(arr) -> dict:
    out = {}
    for d in bounded(arr, 60):
        if not isinstance(d, dict):
            continue
        k = _driver_key(d.get("driver_number"))
        if k:
            out[k] = {
                "acr": safe(d.get("name_acronym") or f"#{k}", 8),
                "color": hex_color(d.get("team_colour")) or "",
                "team": safe(d.get("team_name"), 32),
            }
    return out


def parse_openf1_positions(arr, drivers: dict) -> tuple[list[dict], int]:
    latest: dict[str, dict] = {}
    for r in bounded(arr, 20000):
        if not isinstance(r, dict):
            continue
        k = _driver_key(r.get("driver_number"))
        try:
            pos = int(r.get("position"))
        except (TypeError, ValueError):
            continue
        date = r.get("date") if isinstance(r.get("date"), str) else ""
        if k and 1 <= pos <= 99:
            old = latest.get(k)
            if old is None or date > old["date"]:
                latest[k] = {"pos": pos, "date": date}
    rows, newest = [], 0
    for k, v in latest.items():
        ts = parse_iso(v["date"]) or 0
        newest = max(newest, ts)
        info = drivers.get(k, {})
        rows.append({
            "pos": v["pos"], "num": k, "code": info.get("acr", f"#{k}"),
            "color": info.get("color", ""), "team": info.get("team", ""),
            "best": "", "gap": "", "state": "", "out_part": 0, "in_pit": False, "pits": None,
        })
    rows.sort(key=lambda r: r["pos"])
    return rows[:30], newest


def _fmt_gap(v) -> str:
    if v is None or v == "":
        return ""
    if isinstance(v, (int, float)):
        return "+" + (fmt_lap(v) if v > 0 else "0.000")
    return safe(v, 12)


def apply_openf1_intervals(rows: list[dict], arr) -> None:
    latest: dict[str, dict] = {}
    for r in bounded(arr, 20000):
        if not isinstance(r, dict):
            continue
        k = _driver_key(r.get("driver_number"))
        date = r.get("date") if isinstance(r.get("date"), str) else ""
        if k and (k not in latest or date > latest[k]["date"]):
            latest[k] = {"date": date, "lead": r.get("gap_to_leader"), "int": r.get("interval")}
    for row in rows:
        v = latest.get(row.get("num", ""))
        if v:
            row["best"] = "" if row["pos"] == 1 else _fmt_gap(v["lead"])
            row["gap"] = "" if row["pos"] == 1 else _fmt_gap(v["int"])


def parse_openf1_flag(arr) -> str:
    last = ""
    for m in bounded(arr, 2000):
        if (isinstance(m, dict) and m.get("category") == "Flag" and m.get("scope") == "Track"
                and isinstance(m.get("flag"), str) and m["flag"]):
            last = safe(m["flag"], 16)
        elif isinstance(m, dict) and m.get("category") == "SafetyCar" and isinstance(m.get("message"), str):
            msg = m["message"].upper()
            if "VIRTUAL SAFETY CAR DEPLOYED" in msg:
                last = "VSC"
            elif "VIRTUAL SAFETY CAR ENDING" in msg:
                last = "VSC ENDING"
            elif "SAFETY CAR DEPLOYED" in msg:
                last = "SAFETY CAR"
    return last


def parse_openf1_race_control(arr, limit: int = 4) -> list[str]:
    msgs = [safe(m.get("message"), 90) for m in bounded(arr, 2000)
            if isinstance(m, dict) and isinstance(m.get("message"), str)]
    return msgs[-limit:][::-1]


def parse_openf1_session(arr) -> dict | None:
    if not isinstance(arr, list) or not arr or not isinstance(arr[-1], dict):
        return None
    s = arr[-1]
    return {
        "name": safe(s.get("session_name"), 24),
        "kind": safe(s.get("session_type"), 24),
        "part": 0,
        "status": "",
        "start": parse_iso(s.get("date_start")) or 0,
        "end": parse_iso(s.get("date_end")) or 0,
        "location": safe(s.get("location"), 32),
        "circuit": safe(s.get("circuit_short_name"), 32),
        "key": s.get("session_key") if isinstance(s.get("session_key"), int) else None,
        "cancelled": bool(s.get("is_cancelled")),
    }


# ── OpenF1 session results (free plan: final classification after a session) ─

def fmt_lap(seconds) -> str:
    """71.717 -> "1:11.717"; 5723.504 -> "1:35:23.504"."""
    try:
        s = float(seconds)
    except (TypeError, ValueError):
        return ""
    if s <= 0:
        return ""
    h, rem = divmod(s, 3600)
    m, sec = divmod(rem, 60)
    if h >= 1:
        return f"{int(h)}:{int(m):02d}:{sec:06.3f}"
    if m >= 1:
        return f"{int(m)}:{sec:06.3f}"
    return f"{sec:.3f}"


def _gap_text(g) -> str:
    if g is None or g == "":
        return ""
    if isinstance(g, (int, float)):
        return "" if g <= 0 else "+" + fmt_lap(g)
    text = safe(g, 12)
    if "LAP" in text.upper():
        n = "".join(ch for ch in text if ch.isdigit()) or "1"
        return f"+{n} Lap{'s' if n != '1' else ''}"
    return text


def parse_openf1_session_result(arr, drivers: dict, kind: str) -> list[dict]:
    """OpenF1 /session_result -> rows like the Last tab. ``kind`` is the OpenF1
    session_type ("Practice", "Qualifying", "Race"); sprint quali is "Qualifying"."""
    rows = []
    for e in bounded(arr, 60):
        if not isinstance(e, dict):
            continue
        k = _driver_key(e.get("driver_number"))
        if not k:
            continue
        info = drivers.get(k, {})
        dur, gap = e.get("duration"), e.get("gap_to_leader")
        part = 0
        if isinstance(dur, list):           # qualifying: one entry per part
            idx = max((i for i, v in enumerate(dur) if v is not None), default=-1)
            part = idx + 1
            dur = dur[idx] if idx >= 0 else None
            gap = gap[idx] if isinstance(gap, list) and 0 <= idx < len(gap) else None
        try:
            pos = int(e.get("position"))
        except (TypeError, ValueError):
            pos = None
        status = "DSQ" if e.get("dsq") else "DNS" if e.get("dns") else "DNF" if e.get("dnf") else ""
        if kind == "Race":
            detail = status or (fmt_lap(dur) if pos == 1 else _gap_text(gap))
            extra = ""
        else:
            detail = status or fmt_lap(dur)
            extra = "" if pos == 1 else _gap_text(gap)
        if kind == "Qualifying" and part and not status:
            extra = (extra + "  " if extra else "") + f"Q{part}"
        pts = e.get("points")
        rows.append({
            "pos": str(pos) if pos is not None else "–",
            "num": k,
            "code": info.get("acr", f"#{k}"),
            "team": info.get("team", ""),
            "color": info.get("color", ""),
            "detail": detail,
            "gap": extra,
            "laps": e.get("number_of_laps") if isinstance(e.get("number_of_laps"), int) else None,
            "pts": "" if not pts else (str(int(pts)) if float(pts) == int(pts) else f"{pts:g}"),
            "status": status,
        })
    rows.sort(key=lambda r: (not r["pos"].isdigit(), int(r["pos"]) if r["pos"].isdigit() else 99))
    return rows[:30]
