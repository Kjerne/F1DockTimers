import json

from f1win.core import parsers
from f1win.core.tracks import TrackLibrary, to_svg
from f1win.paths import resource_dir
from f1win.services.notifier import due_notifications
from f1win.settings import Settings


def test_settings_roundtrip(tmp_path):
    f = tmp_path / "s.json"
    s = Settings(time_format="12h", favorite_driver=" ver ", widget_geometry=[10, 20, 400, 500],
                 session_offsets={"round_key": "2026-21", "quali": 30})
    s.clamp()
    s.save(f)
    t = Settings.load(f)
    assert t.time_format == "12h" and t.favorite_driver == "VER"
    assert t.widget_geometry == [10, 20, 400, 500]
    assert t.session_offsets["quali"] == 30
    assert "password" not in f.read_text().lower()


def test_settings_bad_values_fall_back(tmp_path):
    f = tmp_path / "s.json"
    f.write_text(json.dumps({"time_format": "25h", "notify_lead_min": 9999, "ui_scale": "big",
                             "animations": "yes", "accent": 5}))
    s = Settings.load(f)
    assert s.time_format == "24h" and s.notify_lead_min == 120
    assert s.ui_scale == 100 and s.animations is True and s.accent == "#BCC2FF"
    (tmp_path / "broken.json").write_text("{not json")
    assert Settings.load(tmp_path / "broken.json").time_format == "24h"


def test_cache_restore_offline(qapp, tmp_path, fx):
    from f1win.services.store import DataStore
    a = DataStore(tmp_path / "cache.json")
    a.race = parsers.parse_next_race(fx("next.json"))
    a.set("drivers", {**parsers.parse_driver_standings(fx("driver_standings.json")), "loaded": True})
    a.save_cache()
    b = DataStore(tmp_path / "cache.json")
    assert b.load_cache()
    assert b.race.name == "Las Vegas Grand Prix"
    assert b.get("drivers")["rows"][0]["code"] == "ANT"
    assert b.last_updated() > 0
    assert not b.status["loading"]
    # Corrupt cache is ignored, not fatal
    (tmp_path / "cache.json").write_text("{oops")
    c = DataStore(tmp_path / "cache.json")
    assert not c.load_cache() and c.race is None


def test_failed_fetch_keeps_previous_rows(qapp, tmp_path, fx):
    from f1win.services.poller import Poller
    from f1win.services.store import DataStore

    class DeadHttp:
        offline = True

        def get_json(self, url, ok, fail, headers=None, timeout_ms=0):
            fail("net", 0)

        def post_form(self, *a, **k):
            pass

    store = DataStore(tmp_path / "c.json")
    store.set("drivers", {**parsers.parse_driver_standings(fx("driver_standings.json")), "loaded": True})
    p = Poller(store, Settings(), DeadHttp())
    p.update()
    assert "failed" not in store.get("drivers") or not store.get("drivers")["failed"]  # fresh: not refetched
    p.refresh_all(force=True)
    d = store.get("drivers")
    assert d["failed"] and d["rows"][0]["code"] == "ANT"
    assert store.status["offline"]
    assert p.attempts["drivers"] == 1 and p.due["drivers"] > 0


def test_live_unavailable_message(qapp, tmp_path, fx):
    from f1win.services.poller import Poller
    from f1win.services.store import DataStore

    class Http401:
        offline = False

        def get_json(self, url, ok, fail, headers=None, timeout_ms=0):
            fail("http 401", 401) if "openf1" in url else fail("net", 0)

        def post_form(self, *a, **k):
            pass

    store = DataStore(tmp_path / "c.json")
    p = Poller(store, Settings(), Http401())
    p.poll_openf1()
    assert store.get("live")["error"] == "auth"
    assert not store.get("live")["rows"]


def test_circuit_mapping_for_whole_season(fx):
    lib = TrackLibrary(resource_dir() / "tracks.txt")
    season = fx("season.json")["MRData"]["RaceTable"]["Races"]
    missing = [r["Circuit"]["circuitId"] for r in season if lib.points(r["Circuit"]["circuitId"]) is None]
    assert missing == []
    assert lib.points("las_vegas") == lib.points("vegas")       # alias
    assert lib.points("../etc") is None and lib.points("unknown_ring") is None
    svg = to_svg(lib.points("monza"), "#ffffff")
    assert svg.startswith("<svg") and "polyline" in svg


def test_notifications_once(fx):
    r = parsers.parse_next_race(fx("next.json"))
    s = Settings(notify_race=True, notify_quali=True, notify_lead_min=10)
    rc = next(x.ts for x in r.sessions if x.id == "race")
    q = next(x.ts for x in r.sessions if x.id == "quali")
    assert due_notifications(r, s, rc - 11 * 60, set()) == []
    due = due_notifications(r, s, rc - 9 * 60, set())
    assert [d[1] for d in due] == ["race"]
    assert due_notifications(r, s, rc - 9 * 60, {due[0][0]}) == []
    assert [d[1] for d in due_notifications(r, s, q - 60, set())] == ["quali"]
    s.notify_quali = False
    assert due_notifications(r, s, q - 60, set()) == []
    assert due_notifications(r, s, rc + 3600, set()) == []    # long past: no late toast


def test_settings_export_import(tmp_path):
    a = Settings(pill_style="custom", pill_radius=0, favorite_driver="NOR", widget_geometry=[1, 2, 400, 500],
                 openf1_username="me@example.com", start_with_windows=True)
    out = tmp_path / "export.json"
    a.export_to(out)
    text = out.read_text()
    assert "widget_geometry" not in text and "start_with_windows" not in text
    assert "password" not in text.lower()
    b = Settings(widget_geometry=[9, 9, 300, 300])
    changed = b.import_from(out)
    assert b.pill_style == "custom" and b.pill_radius == 0 and b.favorite_driver == "NOR"
    assert b.widget_geometry == [9, 9, 300, 300] and b.start_with_windows is False
    assert "pill_style" in changed


def test_settings_import_rejects_bad_files(tmp_path):
    import pytest
    bad = tmp_path / "bad.json"
    bad.write_text("{not json")
    with pytest.raises(ValueError):
        Settings().import_from(bad)
    other = tmp_path / "other.json"
    other.write_text('{"app": "Something", "settings": {}}')
    with pytest.raises(ValueError):
        Settings().import_from(other)
    hostile = tmp_path / "h.json"
    hostile.write_text('{"app": "NoctaliaF1", "settings": {"ui_scale": 99999, "time_format": 5, "evil": 1}}')
    s = Settings()
    s.import_from(hostile)
    assert s.ui_scale == 200 and s.time_format == "24h" and not hasattr(s, "evil")
