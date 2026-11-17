"""The only component that touches the network, ported from service.luau.

Ticks once a second (cheap: it only compares deadlines) and starts requests
when they are due. Every request is async; failures back off 1, 2, 4 ... min
(max 15) and keep the previously loaded rows.
"""
from __future__ import annotations

import logging
import time
from urllib.parse import quote

from PySide6.QtCore import QObject, QTimer, Signal

from ..core import parsers
from ..core.sessions import (DURATION_MIN, effective_sessions, grid_applicable, near_session, session_ts,
                             weekend_window)
from ..core.timeutil import now_sec
from . import credentials
from .http import HttpClient
from .store import DataStore, empty

log = logging.getLogger(__name__)

JOLPICA = "https://api.jolpi.ca/ergast/f1/"
OPENF1 = "https://api.openf1.org/v1/"
OPENF1_TOKEN = "https://api.openf1.org/token"
F1_TIMING = "https://livetiming.formula1.com/static/"

LIVE_EVERY_S = 15            # OpenF1 allows 30 requests a minute; 4 requests per poll
SESSION_INFO_EVERY_S = 60
MIN_REFRESH_GAP_S = 5
RESULT_RETRY_S = 600         # session results not published yet: look again in 10 min
RESULT_REFRESH_S = 6 * 3600
WAKE_JUMP_S = 90             # wall clock moved this much more than the monotonic clock


def round_key(race) -> str:
    return f"{race.season}-{race.round}" if race else ""


class Poller(QObject):
    tick = Signal(int)          # now (Unix seconds), once a second, for countdowns
    woke = Signal()

    def __init__(self, store: DataStore, settings, http: HttpClient | None = None, parent=None) -> None:
        super().__init__(parent)
        self.store = store
        self.settings = settings
        self.http = http or HttpClient(self)
        self.due = {"schedule": 0.0, "drivers": 0.0, "constructors": 0.0, "results": 0.0}
        self.busy: set[str] = set()
        self.attempts: dict[str, int] = {}
        self.generation = 0
        self.grid_key: str | None = None
        self.grid_retry_at = 0.0
        self.post_race_round: str | None = None
        self.session_info_at = 0.0
        self.last_live = 0.0
        self.live_backoff_until = 0.0
        self.last_refresh = 0.0
        self.drivers_map: dict | None = None
        self.of_positions: list | None = None
        self.of_intervals: list | None = None
        self.token: str | None = None
        self.token_expires = 0.0
        self.stream = None       # services.f1stream.F1Stream, created lazily
        self._last_wall = time.time()
        self._last_mono = time.monotonic()
        self._timer = QTimer(self)
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self.update)
        self._save_timer = QTimer(self)
        self._save_timer.setSingleShot(True)
        self._save_timer.setInterval(1500)
        self._save_timer.timeout.connect(self.store.save_cache)
        self.result_due = 0.0
        self.result_wanted_ts = 0
        self.http.set_openf1_plan(settings.openf1_plan) if hasattr(self.http, 'set_openf1_plan') else None
        self._schedule_from_cache()

    # ── lifecycle ───────────────────────────────────────────────────────────

    def start(self) -> None:
        self._timer.start()
        self.update()

    def stop(self) -> None:
        self._timer.stop()
        if self.stream is not None:
            self.stream.stop()
        self.store.save_cache()

    def _schedule_from_cache(self) -> None:
        """Fresh cached data is not refetched at startup; stale data is."""
        mono, wall = time.monotonic(), time.time()
        every = {"schedule": self.settings.schedule_refresh_h * 3600,
                 "drivers": self.settings.standings_refresh_h * 3600,
                 "constructors": self.settings.standings_refresh_h * 3600,
                 "results": self.settings.standings_refresh_h * 3600}
        for name, interval in every.items():
            key = "race" if name == "schedule" else name
            fetched = self.store.fetched.get(key, 0)
            age = wall - fetched if fetched else interval
            self.due[name] = mono + max(0.0, interval - age) if fetched else 0.0
        if self.store.race is not None:
            self.republish_race()

    def refresh_all(self, force: bool = False) -> None:
        mono = time.monotonic()
        if not force and mono - self.last_refresh < MIN_REFRESH_GAP_S:
            return
        self.last_refresh = mono
        for k in self.due:
            self.due[k] = 0
        self.attempts.clear()
        self.grid_key, self.grid_retry_at = None, 0
        self.live_backoff_until = 0
        self.last_live = 0
        self.result_due = 0
        self.store.set_status(loading=True)
        self.update()

    def settings_changed(self) -> None:
        self.republish_race()
        self.grid_key = None
        if self.settings.live_source != "f1_feed" and self.stream is not None:
            self.stream.stop()
        self._reset_live()
        self.last_live = 0
        self.result_due = 0
        if hasattr(self.http, 'set_openf1_plan'):
            self.http.set_openf1_plan(self.settings.openf1_plan)

    # ── helpers ─────────────────────────────────────────────────────────────

    def _retry_delay(self, name: str) -> float:
        n = self.attempts.get(name, 0) + 1
        self.attempts[name] = n
        return min(60 * 2 ** (n - 1), 900)

    def _failed_state(self, key: str) -> dict:
        prev = self.store.get(key)
        out = dict(prev)
        out["failed"] = True
        return out

    def _dirty(self) -> None:
        self._save_timer.start()

    def _get(self, name: str, url: str, ok, fail=None, scoped=False, auth=False) -> None:
        if name in self.busy:
            return
        self.busy.add(name)
        gen = self.generation
        headers = {}
        if auth and self.token:
            headers["Authorization"] = f"Bearer {self.token}"

        def on_ok(data):
            self.busy.discard(name)
            if scoped and gen != self.generation:
                return
            ok(data)

        def on_fail(reason, status):
            self.busy.discard(name)
            if scoped and gen != self.generation:
                return
            self.store.set_status(offline=self.http.offline)
            if fail:
                fail(reason, status)

        self.http.get_json(url, on_ok, on_fail, headers)

    # ── sessions ────────────────────────────────────────────────────────────

    def manual_offsets(self) -> dict:
        offs = self.settings.session_offsets or {}
        race = self.store.race
        if race is None or offs.get("round_key") != round_key(race):
            return {}
        return {k: v for k, v in offs.items() if k != "round_key" and isinstance(v, int)}

    def republish_race(self) -> None:
        race = self.store.race
        if race is None:
            return
        race.sessions = effective_sessions(race.raw_sessions, self.store.auto_ts, self.manual_offsets())
        self.store.set_race(race, success=False)

    def _reset_per_race(self) -> None:
        self.generation += 1
        self.store.auto_ts = {}
        self.grid_key, self.grid_retry_at = None, 0
        self.busy -= {"grid", "of_drivers", "of_pos", "of_flag", "of_int", "of_session"}
        self.store.set("grid", empty("grid"), success=False)
        self._reset_live()
        offs = self.settings.session_offsets or {}
        if offs and offs.get("round_key") != round_key(self.store.race):
            self.settings.session_offsets = {}

    def _reset_live(self) -> None:
        self.drivers_map = None
        self.of_positions = self.of_intervals = None
        if self.stream is not None:
            self.stream.state.reset()
        self.store.set("live", empty("live"), success=False)

    # ── schedule ────────────────────────────────────────────────────────────

    def load_schedule(self) -> None:
        def ok(data):
            race = parsers.parse_next_race(data)
            if race is None:
                self._load_next_season()
                return
            self._accept_race(race)

        def fail(reason, _status):
            self.due["schedule"] = time.monotonic() + self._retry_delay("schedule")
            self.store.set_status(loading=False, error=reason)

        self._get("schedule", JOLPICA + "current/next.json", ok, fail)

    def _load_next_season(self) -> None:
        """Season transition: Jolpica's current/next is empty after the finale."""
        year = time.gmtime().tm_year
        cur = self.store.race.season if self.store.race else str(year)
        try:
            target = max(int(cur) + 1, year)
        except ValueError:
            target = year + 1

        def ok(data):
            race = parsers.first_future_race(data, now_sec())
            if race is None:
                self.store.race = None
                self.store.set_race(None)
                self.due["schedule"] = time.monotonic() + self.settings.schedule_refresh_h * 3600
                self.store.set_status(loading=False, error="no_race")
                return
            self._accept_race(race)

        def fail(reason, _status):
            self.due["schedule"] = time.monotonic() + self._retry_delay("schedule")
            self.store.set_status(loading=False, error=reason)

        self.busy.discard("schedule")
        self._get("schedule_next", f"{JOLPICA}{target}/races.json", ok, fail)

    def _accept_race(self, race) -> None:
        self.attempts.pop("schedule", None)
        self.due["schedule"] = time.monotonic() + self.settings.schedule_refresh_h * 3600
        old = self.store.race
        changed = old is None or old.round != race.round or old.season != race.season
        self.store.race = race
        if changed:
            self._reset_per_race()
        self.republish_race()
        self.store.set_race(race)
        self.store.set_status(loading=False, error="")
        self._dirty()

    def load_session_info(self) -> None:
        race = self.store.race
        if race is None:
            return

        def ok(info):
            got = parsers.parse_session_info(info, self.store.race)
            if got is None:
                return
            sid, ts = got
            if self.store.auto_ts.get(sid) != ts:
                self.store.auto_ts[sid] = ts
                self.republish_race()
                self._dirty()

        self._get("session_info", F1_TIMING + "SessionInfo.json", ok)

    # ── standings, results, grid ────────────────────────────────────────────

    def _dataset(self, name: str, url: str, parse) -> None:
        def ok(data):
            parsed = parse(data)
            parsed.update(loaded=True, failed=False)
            self.attempts.pop(name, None)
            self.store.set(name, parsed)
            self._dirty()

        def fail(_reason, _status):
            self.due[name] = time.monotonic() + self._retry_delay(name)
            self.store.set(name, self._failed_state(name), success=False)

        self._get(name, url, ok, fail)

    def load_grid(self, force: bool) -> None:
        race = self.store.race
        url = JOLPICA + ("current/last/qualifying.json" if force else f"{race.season}/{race.round}/qualifying.json")
        key = f"{round_key(race)}:{force}"
        self.grid_key = key

        def ok(data):
            parsed = parsers.parse_qualifying(data)
            if not parsed["rows"]:
                # Jolpica has not published qualifying yet: try again in a few minutes.
                self.grid_key = None
                self.grid_retry_at = time.monotonic() + self._retry_delay("grid")
                self.store.set("grid", {**empty("grid"), "loaded": True}, success=False)
                return
            parsed.update(loaded=True, failed=False, provisional=True)
            self.attempts.pop("grid", None)
            self.store.set("grid", parsed)
            self._dirty()

        def fail(_reason, _status):
            self.grid_key = None
            self.grid_retry_at = time.monotonic() + self._retry_delay("grid")
            self.store.set("grid", self._failed_state("grid"), success=False)

        self._get("grid", url, ok, fail, scoped=True)

    # ── live: OpenF1 ────────────────────────────────────────────────────────

    def _publish_live(self, **changes) -> None:
        live = dict(self.store.get("live"))
        live.update(changes)
        self.store.set("live", live, success=False)

    def _live_failure(self, _reason, status) -> None:
        if status in (401, 403):
            err = "token" if self.token else "auth"
            self.token = None
            self.live_backoff_until = time.monotonic() + 60
        elif status == 429:
            err = "rate"
            self.live_backoff_until = time.monotonic() + 60
        else:
            err = "net"
        self._publish_live(error=err, source="openf1")

    def _with_token(self, fn) -> None:
        creds = credentials.resolve(self.settings.openf1_username) if self.settings.openf1_plan == "sponsor" else None
        if creds is None:
            self.token = None
            fn()
            return
        if self.token and time.time() < self.token_expires - 120:
            fn()
            return
        if "token" in self.busy:
            return
        self.busy.add("token")
        user, pw = creds
        body = f"username={quote(user, safe='')}&password={quote(pw, safe='')}".encode()

        def ok(data):
            self.busy.discard("token")
            tok = data.get("access_token") if isinstance(data, dict) else None
            if isinstance(tok, str) and tok:
                self.token = tok
                try:
                    self.token_expires = time.time() + float(data.get("expires_in") or 3600)
                except (TypeError, ValueError):
                    self.token_expires = time.time() + 3600
                fn()
            else:
                self._login_failed()

        def fail(_reason, _status):
            self.busy.discard("token")
            self._login_failed()

        self.http.post_form(OPENF1_TOKEN, body, ok, fail)

    def _login_failed(self) -> None:
        self.token = None
        self.live_backoff_until = time.monotonic() + 300
        self._publish_live(error="login", source="openf1")

    def _compose_openf1(self) -> None:
        """Rebuild the running order from the latest positions, drivers and intervals,
        whatever order their answers arrived in."""
        if self.of_positions is None:
            return
        rows, newest = parsers.parse_openf1_positions(self.of_positions, self.drivers_map or {})
        if self.of_intervals is not None:
            parsers.apply_openf1_intervals(rows, self.of_intervals)
        self._publish_live(rows=rows, latest_sec=newest, error="", source="openf1")

    def poll_openf1(self) -> None:
        def run():
            if self.drivers_map is None:
                def drivers(arr):
                    self.drivers_map = parsers.parse_openf1_drivers(arr)
                    self._compose_openf1()

                self._get("of_drivers", OPENF1 + "drivers?session_key=latest", drivers,
                          self._live_failure, scoped=True, auth=True)
                self._get("of_session", OPENF1 + "sessions?session_key=latest",
                          lambda arr: self._publish_live(session=parsers.parse_openf1_session(arr)),
                          None, scoped=True, auth=True)

            def positions(arr):
                self.of_positions = arr
                self._compose_openf1()

            def intervals(arr):
                self.of_intervals = arr
                self._compose_openf1()

            def race_control(arr):
                self._publish_live(flag=parsers.parse_openf1_flag(arr),
                                   messages=parsers.parse_openf1_race_control(arr))

            self._get("of_pos", OPENF1 + "position?session_key=latest", positions,
                      self._live_failure, scoped=True, auth=True)
            newest = self.store.get("live").get("latest_sec") or 0
            if newest:
                # Only the last minute: a whole race of intervals is several MB.
                since = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(newest - 60))
                self._get("of_int", f"{OPENF1}intervals?session_key=latest&date>={since}", intervals,
                          None, scoped=True, auth=True)
            self._get("of_flag", OPENF1 + "race_control?session_key=latest", race_control,
                      None, scoped=True, auth=True)

        self._with_token(run)

    # ── OpenF1: last finished session (works on the free plan) ─────────────

    def openf1_restricted(self, sessions, now: int) -> bool:
        """Free plan: OpenF1 serves nothing while a session runs, so stay quiet from
        15 min before a session until 20 min after its usual end."""
        if self.settings.openf1_plan == "sponsor":
            return False
        return any(s.ts - 15 * 60 <= now <= s.ts + (DURATION_MIN.get(s.id, 60) + 20) * 60 for s in sessions or [])

    def load_last_session(self) -> None:
        now = now_sec()

        def got_session(arr):
            sess = parsers.parse_openf1_session(arr)
            if not sess or not sess.get("key"):
                self.result_due = time.monotonic() + RESULT_RETRY_S
                return
            if sess["end"] and sess["end"] + 10 * 60 > now:
                # Still running (or just ended): results come after it.
                self.result_due = time.monotonic() + max(RESULT_RETRY_S, sess["end"] + 20 * 60 - now)
                return
            prev = self.store.get("last_session")
            if (prev.get("session") or {}).get("key") == sess["key"] and prev.get("rows") and not prev.get("partial"):
                self.result_due = time.monotonic() + RESULT_REFRESH_S
                return
            key = sess["key"]
            holder: dict = {}

            def got_drivers(arr2):
                holder["drivers"] = parsers.parse_openf1_drivers(arr2)
                self._get("of_result", f"{OPENF1}session_result?session_key={key}", got_result, failed)

            def got_result(arr3):
                rows = parsers.parse_openf1_session_result(arr3, holder.get("drivers", {}), sess.get("kind", ""))
                if not rows:
                    self.result_due = time.monotonic() + RESULT_RETRY_S
                    return
                self.attempts.pop("last_session", None)
                self.result_due = time.monotonic() + RESULT_REFRESH_S
                self.store.set("last_session", {"loaded": True, "failed": False, "session": sess, "rows": rows})
                self._dirty()

            self._get("of_res_drivers", f"{OPENF1}drivers?session_key={key}", got_drivers, failed)

        def failed(_reason, status):
            if status in (401, 403):     # a session went live meanwhile (free plan)
                self.result_due = time.monotonic() + RESULT_RETRY_S
            else:
                self.result_due = time.monotonic() + self._retry_delay("last_session")

        self._get("of_res_session", OPENF1 + "sessions?session_key=latest", got_session, failed)

    # ── live: experimental F1 stream ────────────────────────────────────────

    def run_f1_stream(self, now: int) -> None:
        from .f1stream import F1Stream  # lazy: only when the user opted in
        if self.stream is None:
            self.stream = F1Stream(self.http.nam, self)
            self.stream.updated.connect(self._stream_updated)
            self.stream.failed.connect(lambda err: self._publish_live(error=err, source="f1_feed"))
        self.stream.ensure_running()

    def _stream_updated(self) -> None:
        if self.stream is None:
            return
        view = self.stream.state.view(now_sec())
        if view is None:
            return
        self._publish_live(rows=view["rows"], flag=view["flag"], latest_sec=view["latest_sec"],
                           session=view["session"], clock=view["clock"], error="", source="f1_feed")

    # ── scheduler ───────────────────────────────────────────────────────────

    def update(self) -> None:
        now = now_sec()
        mono = time.monotonic()
        wall = time.time()

        # Sleep/resume or clock change: the wall clock jumped ahead of the monotonic one.
        if (wall - self._last_wall) - (mono - self._last_mono) > WAKE_JUMP_S or wall - self._last_wall > WAKE_JUMP_S:
            log.info("clock jump detected (sleep/resume?); refreshing")
            self._last_wall, self._last_mono = wall, mono
            self.woke.emit()
            self.refresh_all(force=True)
            return
        self._last_wall, self._last_mono = wall, mono

        if mono >= self.due["schedule"]:
            self.due["schedule"] = mono + 3600  # placeholder until the answer sets it
            self.load_schedule()
        standings_every = self.settings.standings_refresh_h * 3600
        for name, url, parse in (
            ("drivers", "current/driverStandings.json", parsers.parse_driver_standings),
            ("constructors", "current/constructorStandings.json", parsers.parse_constructor_standings),
            ("results", "current/last/results.json", parsers.parse_results),
        ):
            if mono >= self.due[name]:
                self.due[name] = mono + standings_every
                self._dataset(name, JOLPICA + url, parse)

        race = self.store.race
        sessions = race.sessions if race else []
        if self.store.status.get("loading") and not self.busy and self.store.race is not None:
            self.store.set_status(loading=False)

        # After a race the standings, result and schedule change: refresh once per round.
        race_ts = session_ts(sessions, "race")
        if race is not None and race_ts and now > race_ts + 3 * 3600 + 900 and self.post_race_round != round_key(race):
            self.post_race_round = round_key(race)
            for k in self.due:
                self.due[k] = mono

        # F1's timing feed: follow delays during the weekend, once a minute.
        win = weekend_window(race.raw_sessions) if race else None
        if win and win[0] - 3 * 3600 <= now <= win[1] + 4 * 3600 and mono - self.session_info_at >= SESSION_INFO_EVERY_S:
            self.session_info_at = mono
            self.load_session_info()

        force = self.settings.debug_force_tabs
        if race is not None and (force or grid_applicable(sessions, now)):
            key = f"{round_key(race)}:{force}"
            if mono >= self.grid_retry_at and self.grid_key != key:
                self.load_grid(force)

        # Last finished session from OpenF1 (free plan friendly). Look again once a
        # scheduled session has ended, otherwise every few hours.
        finished = [s.ts for s in sessions
                    if now > s.ts + (DURATION_MIN.get(s.id, 60) + 20) * 60 and s.ts > self.result_wanted_ts]
        if finished:
            self.result_wanted_ts = max(finished)
            self.result_due = min(self.result_due, mono)
        if mono >= self.result_due and not self.openf1_restricted(sessions, now) \
                and not ({"of_res_session", "of_res_drivers", "of_result"} & self.busy):
            self.result_due = mono + RESULT_RETRY_S   # replaced by the answer
            self.load_last_session()

        if near_session(sessions, now) or force:
            if self.settings.live_source == "f1_feed":
                self.run_f1_stream(now)
            else:
                if self.stream is not None:
                    self.stream.stop()
                sponsor = self.settings.openf1_plan == "sponsor"
                if not sponsor and not force:
                    # Free plan: no live data during sessions. Say so instead of polling.
                    if self.store.get("live").get("error") != "free":
                        self._publish_live(error="free", source="openf1")
                elif mono - self.last_live >= LIVE_EVERY_S and mono >= self.live_backoff_until:
                    self.last_live = mono
                    self.poll_openf1()
        else:
            if self.stream is not None:
                self.stream.stop()
            live = self.store.get("live")
            if live.get("rows") or live.get("latest_sec") or live.get("error"):
                self._reset_live()

        self.tick.emit(now)
