"""Spoiler-free mode and the results notification."""
from types import SimpleNamespace

from f1win.core.sessions import Session
from f1win.core.spoilers import latest_spoiler_ts, spoilers_hidden
from f1win.services.notifier import result_notification

NOW = 1_800_000_000


class Store:
    def __init__(self, sessions=(), results=None, last=None):
        self.race = SimpleNamespace(sessions=list(sessions)) if sessions else None
        self.data = {"results": results or {}, "last_session": last or {}}

    def get(self, k):
        return self.data.get(k, {})


def settings(**kw):
    base = dict(spoiler_free=True, spoiler_revealed_at=0.0, notify_results=True)
    base.update(kw)
    return SimpleNamespace(**base)


def test_practice_is_not_a_spoiler_but_quali_is():
    fp = Store([Session("fp1", NOW - 3600)])
    assert latest_spoiler_ts(fp, NOW) == 0 and not spoilers_hidden(fp, settings(), NOW)
    q = Store([Session("fp1", NOW - 90000), Session("quali", NOW - 600)])
    assert spoilers_hidden(q, settings(), NOW)


def test_reveal_lasts_until_the_next_competitive_session():
    st = Store([Session("quali", NOW - 7200), Session("race", NOW + 86400)])
    assert not spoilers_hidden(st, settings(spoiler_revealed_at=NOW - 60), NOW)
    assert spoilers_hidden(st, settings(spoiler_revealed_at=NOW - 60), NOW + 86400 + 10)   # race started


def test_off_means_never_hidden_and_old_results_count():
    st = Store(results={"rows": [{"pos": 1}], "date_ts": NOW - 3 * 86400})
    assert not spoilers_hidden(st, settings(spoiler_free=False), NOW)
    assert spoilers_hidden(st, settings(spoiler_revealed_at=NOW - 4 * 86400), NOW)
    assert not spoilers_hidden(st, settings(spoiler_revealed_at=NOW - 86400), NOW)


def last(kind="Race", end=NOW - 1800, key=9000):
    rows = [{"pos": "1", "code": "VER"}, {"pos": "2", "code": "NOR"}, {"pos": "3", "code": "LEC"}, {"pos": "4", "code": "HAM"}]
    return {"session": {"kind": kind, "name": kind, "location": "Las Vegas", "key": key, "end": end}, "rows": rows}


def test_result_notification_top3_once():
    hit = result_notification(last(), settings(), NOW, set())
    assert hit == ("result-9000", "Las Vegas · Race results", "1. VER   2. NOR   3. LEC")
    assert result_notification(last(), settings(), NOW, {"result-9000"}) is None


def test_result_notification_skips_practice_old_and_off():
    assert result_notification(last("Practice"), settings(), NOW, set()) is None
    assert result_notification(last(end=NOW - 7 * 3600), settings(), NOW, set()) is None
    assert result_notification(last(), settings(notify_results=False), NOW, set()) is None
