from f1win.core import parsers
from f1win.core.sessions import Race


def test_next_race(fx):
    race = parsers.parse_next_race(fx("next.json"))
    assert race.name == "Las Vegas Grand Prix"
    assert race.round == "21" and race.season == "2026"
    assert race.circuit_id == "vegas"
    assert race.location == "Las Vegas, USA"
    assert [s.id for s in race.sessions] == ["fp1", "fp2", "fp3", "quali", "race"]
    assert not race.is_sprint_weekend
    assert race.url.startswith("https://en.wikipedia.org/")


def test_sprint_weekend_detection(fx):
    season = fx("season.json")["MRData"]["RaceTable"]["Races"]
    singapore = next(r for r in season if r["round"] == "17")
    race = parsers.parse_race(singapore)
    assert race.is_sprint_weekend
    assert [s.id for s in race.sessions] == ["fp1", "sprint_quali", "sprint", "quali", "race"]
    ts = [s.ts for s in race.sessions]
    assert ts == sorted(ts)


def test_empty_next_returns_none():
    assert parsers.parse_next_race({"MRData": {"RaceTable": {"Races": []}}}) is None
    assert parsers.parse_next_race({}) is None
    assert parsers.parse_next_race("garbage") is None


def test_season_transition_picks_first_future(fx):
    data = fx("season.json")
    # After the finale nothing is left
    assert parsers.first_future_race(data, 2_000_000_000) is None
    # Before the season the first race is chosen
    assert parsers.first_future_race(data, 0).round == "1"


def test_driver_standings(fx):
    d = parsers.parse_driver_standings(fx("driver_standings.json"))
    rows = d["rows"]
    assert rows[0]["code"] == "ANT" and rows[0]["pts"] == "390" and rows[0]["wins"] == 9
    assert rows[0]["gap"] == 0 and rows[1]["gap"] > 0
    assert rows[0]["team_id"] == "mercedes"
    assert len(rows) <= 30


def test_constructor_standings(fx):
    d = parsers.parse_constructor_standings(fx("constructor_standings.json"))
    assert d["rows"][0]["name"] == "Mercedes"
    assert d["rows"][0]["wins"] == 12
    assert len(d["rows"]) == 11


def test_results_status_and_fastest(fx):
    r = parsers.parse_results(fx("results.json"))
    assert r["race_name"] == "Brazilian Grand Prix"
    rows = {x["code"]: x for x in r["rows"]}
    assert rows["NOR"]["detail"] == "1:35:23.504" and rows["NOR"]["pts"] == "25"
    assert rows["HAM"]["detail"] == "+1 Lap"        # lapped car, not "+4.427"
    assert rows["PER"]["detail"] == "+2 Laps"
    assert rows["STR"]["detail"] == "Retired"
    assert rows["BOT"]["pos"] == "W"
    assert r["fastest"] and r["fastest"]["time"]
    assert r["date_ts"] > 0


def test_qualifying_and_confirmed_grid(fx):
    q = parsers.parse_qualifying(fx("qualifying.json"))
    assert q["rows"][0]["pos"] == "1" and q["rows"][0]["detail"]
    grid = parsers.grid_from_results(parsers.parse_results(fx("results.json")))
    assert grid[0]["pos"] == "1"
    assert all(r["pos"] in ("PL",) or r["pos"].isdigit() for r in grid)


def test_sanitising():
    from f1win.core.sanitize import safe, safe_link
    assert safe("Ab‮cd\x00e") == "Ab cd e"
    assert safe("x" * 100, 10) == "x" * 10 + "…"
    assert safe_link("http://en.wikipedia.org/wiki/Lando_Norris") == "https://en.wikipedia.org/wiki/Lando_Norris"
    assert safe_link("https://evil.example/wiki") is None
    assert safe_link("https://user@en.wikipedia.org/") is None
    assert safe_link("https://en.wikipedia.org:8443/") is None
    assert safe_link("javascript:alert(1)") is None


def test_session_info_feed(fx):
    race = parsers.parse_next_race(fx("next.json"))
    info = {"Meeting": {"Number": 21}, "Name": "Qualifying",
            "StartDate": "2026-11-20T20:30:00", "GmtOffset": "-08:00:00"}
    sid, ts = parsers.parse_session_info(info, race)
    assert sid == "quali"
    base = next(s.ts for s in race.raw_sessions if s.id == "quali")
    assert ts - base == 30 * 60
    # Another weekend in the feed is ignored
    assert parsers.parse_session_info({**info, "Meeting": {"Number": 20}}, race) is None
    # An absurd shift is ignored
    far = {**info, "StartDate": "2026-11-25T20:30:00"}
    assert parsers.parse_session_info(far, race) is None


def test_openf1_live(fx):
    drivers = parsers.parse_openf1_drivers(fx("openf1_drivers.json"))
    assert drivers and all(len(v["acr"]) <= 8 for v in drivers.values())
    rows, newest = parsers.parse_openf1_positions(fx("openf1_position.json"), drivers)
    assert rows[0]["pos"] == 1 and newest > 0
    assert [r["pos"] for r in rows] == sorted(r["pos"] for r in rows)
    parsers.apply_openf1_intervals(rows, fx("openf1_intervals.json"))
    assert rows[0]["best"] == ""
    flag = parsers.parse_openf1_flag(fx("openf1_race_control.json"))
    assert flag == "CHEQUERED"
    assert parsers.parse_openf1_race_control(fx("openf1_race_control.json"))


def test_openf1_garbage_is_harmless():
    assert parsers.parse_openf1_drivers({"detail": "nope"}) == {}
    assert parsers.parse_openf1_positions([{"driver_number": "x"}, None, 5], {}) == ([], 0)
    assert parsers.parse_openf1_flag(None) == ""
    assert parsers.parse_openf1_session([]) is None


def test_race_roundtrip(fx):
    race = parsers.parse_next_race(fx("next.json"))
    again = Race.from_dict(race.to_dict())
    assert again.name == race.name
    assert [(s.id, s.ts) for s in again.raw_sessions] == [(s.id, s.ts) for s in race.raw_sessions]
