"""OpenF1 Free vs Sponsor plan: rate limits, session results, no requests during sessions on Free."""
from f1win.core import parsers
from f1win.services.http import OPENF1_PLANS, RateLimiter
from f1win.settings import Settings


def test_rate_limiter_free_plan_limits():
    lim = RateLimiter({"api.openf1.org": OPENF1_PLANS["free"]})
    host, t = "api.openf1.org", 1000.0
    starts = []
    for _ in range(40):
        w = lim.wait(host, t)
        t += w
        lim.note(host, t)
        starts.append(t)
        t += 0.001
    # never more than 3 per second
    for i in range(len(starts) - 3):
        assert starts[i + 3] - starts[i] >= 1.0
    # never more than 30 in any rolling minute
    for i in range(len(starts) - 30):
        assert starts[i + 30] - starts[i] >= 60.0


def test_rate_limiter_sponsor_is_faster_and_retry_after_blocks():
    free = RateLimiter({"h": OPENF1_PLANS["free"]})
    sponsor = RateLimiter({"h": OPENF1_PLANS["sponsor"]})
    free.note("h", 0)
    sponsor.note("h", 0)
    assert sponsor.wait("h", 0) < free.wait("h", 0)
    sponsor.block("h", 30.0)
    assert sponsor.wait("h", 10.0) >= 20.0
    assert RateLimiter().wait("other.host", 0) == 0


def test_session_results_all_kinds(fx):
    drivers = parsers.parse_openf1_drivers(fx("openf1_drivers.json"))
    race = parsers.parse_openf1_session_result(fx("openf1_result_race.json"), drivers, "Race")
    assert race[0]["pos"] == "1" and race[0]["detail"] == "1:35:23.504" and race[0]["pts"] == "25"
    assert race[1]["detail"] == "+14.602"
    assert any(r["detail"] == "+1:15.402" for r in race)
    assert any(r["status"] in ("DNF", "DNS") for r in race)
    fp1 = parsers.parse_openf1_session_result(fx("openf1_result_fp1.json"), drivers, "Practice")
    assert fp1[0]["detail"] == "1:11.717" and fp1[0]["gap"] == ""
    assert fp1[1]["gap"] == "+0.082"
    quali = parsers.parse_openf1_session_result(fx("openf1_result_quali.json"), drivers, "Qualifying")
    assert quali[0]["detail"] == "1:27.470" and quali[0]["gap"].endswith("Q3")
    assert quali[-1]["gap"].endswith("Q1") or quali[-1]["status"]
    assert [int(r["pos"]) for r in quali if r["pos"].isdigit()] == sorted(int(r["pos"]) for r in quali if r["pos"].isdigit())


def test_lap_formatting():
    assert parsers.fmt_lap(71.717) == "1:11.717"
    assert parsers.fmt_lap(59.5) == "59.500"
    assert parsers.fmt_lap(5723.504) == "1:35:23.504"
    assert parsers.fmt_lap(None) == ""


def test_free_plan_stays_quiet_during_sessions(qapp, tmp_path, fx):
    from f1win.services.poller import Poller
    from f1win.services.store import DataStore

    calls = []

    class Http:
        offline = False

        def get_json(self, url, ok, fail, headers=None, timeout_ms=0):
            calls.append((url, dict(headers or {})))

        def post_form(self, *a, **k):
            calls.append(("POST", {}))

        def set_openf1_plan(self, plan):
            calls.append(("plan", plan))

    store = DataStore(tmp_path / "c.json")
    store.race = parsers.parse_next_race(fx("next.json"))
    s = Settings(openf1_plan="free", openf1_username="someone")
    p = Poller(store, s, Http())
    fp1 = next(x.ts for x in store.race.sessions if x.id == "fp1")
    assert p.openf1_restricted(store.race.sessions, fp1 + 600)
    assert not p.openf1_restricted(store.race.sessions, fp1 - 3 * 3600)
    s.openf1_plan = "sponsor"
    assert not p.openf1_restricted(store.race.sessions, fp1 + 600)
    # Free plan never sends credentials
    s.openf1_plan = "free"
    calls.clear()
    p.poll_openf1()
    assert all("Authorization" not in h for _u, h in calls if isinstance(h, dict))
    assert not any(u == "POST" for u, _h in calls)


def test_free_plan_settings_default(tmp_path):
    s = Settings()
    assert s.openf1_plan == "free"
    s.openf1_plan = "gold"
    s.clamp()
    assert s.openf1_plan == "free"
