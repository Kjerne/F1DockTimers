from f1win.core import parsers
from f1win.core.sessions import (current_session, effective_sessions, grid_applicable, live_active,
                                 near_session, next_session, session_ts)
from f1win.settings import Settings
from f1win.ui.tabs.schedule import countdown_state


def race(fx):
    return parsers.parse_next_race(fx("next.json"))


def test_next_and_current_session(fx):
    r = race(fx)
    fp1 = session_ts(r.sessions, "fp1")
    assert next_session(r.sessions, fp1 - 10).id == "fp1"
    assert next_session(r.sessions, fp1 + 10).id == "fp2"
    assert current_session(r.sessions, fp1 + 600).id == "fp1"
    assert current_session(r.sessions, fp1 + 2 * 3600) is None
    assert next_session(r.sessions, 2_000_000_000) is None


def test_live_window_and_grid_window(fx):
    r = race(fx)
    q = session_ts(r.sessions, "quali")
    rc = session_ts(r.sessions, "race")
    assert near_session(r.sessions, q - 14 * 60)
    assert not near_session(r.sessions, q - 16 * 60) or near_session(r.sessions, q - 16 * 60) is True
    assert not grid_applicable(r.sessions, q + 1800)
    assert grid_applicable(r.sessions, q + 3600)
    assert not grid_applicable(r.sessions, rc)


def test_live_requires_fresh_feed(fx):
    r = race(fx)
    q = session_ts(r.sessions, "quali")
    live = {"rows": [{"pos": 1}], "latest_sec": q + 60}
    assert live_active(live, r.sessions, q + 120)
    assert not live_active({**live, "latest_sec": q - 3600}, r.sessions, q + 120)   # stale
    assert not live_active({"rows": []}, r.sessions, q + 120)                        # no data
    assert not live_active(live, r.sessions, q + 10 * 3600)                          # not near
    assert live_active(live, r.sessions, q + 10 * 3600, debug=True)


def test_feed_and_manual_corrections(fx):
    r = race(fx)
    base = {s.id: s.ts for s in r.raw_sessions}
    eff = effective_sessions(r.raw_sessions, {"quali": base["quali"] + 1800}, {})
    q = next(s for s in eff if s.id == "quali")
    assert q.delay == 30 and q.source == "feed"
    eff = effective_sessions(r.raw_sessions, {"quali": base["quali"] + 1800}, {"quali": -15})
    q = next(s for s in eff if s.id == "quali")
    assert q.delay == -15 and q.source == "manual"
    assert [s.ts for s in eff] == sorted(s.ts for s in eff)


def test_countdown_states(fx):
    r = race(fx)
    s = Settings()
    rc = session_ts(r.sessions, "race")
    label, text, live = countdown_state(r, s, rc - (4 * 86400 + 10 * 3600 + 50 * 60))
    assert (label, text, live) == ("LIGHTS OUT", "in 4d 10h 50m", False)
    label, text, live = countdown_state(r, s, rc + 600)
    assert live and text == "● LIVE" and "RACE" in label
    s.countdown_target = "session"
    fp1 = session_ts(r.sessions, "fp1")
    label, text, _ = countdown_state(r, s, fp1 - 3600)
    assert label == "NEXT · PRACTICE 1" and text == "in 1h 0m"
    s.countdown_target = "race"
    assert countdown_state(r, s, rc + 5 * 3600)[1] == "Race finished"


def test_sprint_countdown(fx):
    season = fx("season.json")["MRData"]["RaceTable"]["Races"]
    r = parsers.parse_race(next(x for x in season if x["round"] == "17"))
    s = Settings(countdown_target="session")
    sq = session_ts(r.sessions, "sprint_quali")
    assert countdown_state(r, s, sq - 60)[0] == "NEXT · SPRINT QUALI"
    assert countdown_state(r, s, sq + 60)[0] == "LIVE · SPRINT QUALI"


def test_taskbar_pill_sequence(fx):
    from f1win.core.timeutil import zone_for
    from f1win.ui.taskbar_pill import pill_state
    r = race(fx)
    s = Settings()
    cph = zone_for("Europe/Copenhagen")
    ts = {x.id: x.ts for x in r.sessions}
    top, bottom, live = pill_state(r, None, s, ts["fp1"] - 3600, cph)
    assert top == "R21 · Las Vegas · FP1" and bottom == "Fri 01:30 · in 1h 0m" and not live
    assert pill_state(r, None, s, ts["fp1"] + 600, cph)[:3] == ("R21 · Las Vegas · ● FP1", "LIVE", True)
    # FP1 over -> FP2 is next
    assert pill_state(r, None, s, ts["fp1"] + 3700, cph)[0].endswith("FP2")
    assert pill_state(r, None, s, ts["quali"] - 60, cph)[0].endswith("Quali")
    assert pill_state(r, None, s, ts["race"] - 60, cph)[0].endswith("GP")
    # Qualifying part from the live feed
    live = {"session": {"part": 2}, "rows": []}
    assert pill_state(r, live, s, ts["quali"] + 1500, cph)[0] == "R21 · Las Vegas · ● Q2"
    assert pill_state(r, None, s, ts["race"] + 4 * 3600, cph)[1] == "Weekend over"
    assert pill_state(None, None, s, 0)[1] == "No upcoming race"
