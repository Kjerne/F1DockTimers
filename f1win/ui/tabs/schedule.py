"""Schedule tab: circuit, start lights, countdown, session times."""
from __future__ import annotations

import time

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QHBoxLayout, QVBoxLayout, QWidget

from ...core import weather
from ...core.sessions import DURATION_MIN, current_session, next_session, session_ts
from ...core.timeutil import fmt_datetime, fmt_long, now_sec, utc_offset_label
from ..widgets import LightsGantry, Row, Text, TrackView, Wrap, with_help
from .common import RIGHT, Tab
from .weather import RAIN_W, TEMP_W, measured_for, session_cells
from ...i18n import T

LIGHT_STEP_MS = 620
LIGHT_HOLD_MS = 1100
INTRO_FADE_MS = 280


def countdown_state(race, settings, now: int) -> tuple[str, str, bool]:
    """(label, big text, live?) for the countdown block. Pure, for tests."""
    sessions = race.sessions if race else []
    live = current_session(sessions, now)
    if live is not None:
        return f"LIVE · {live.name.upper()}", "● LIVE", True
    race_ts = session_ts(sessions, "race")
    if settings.countdown_target == "session":
        nxt = next_session(sessions, now)
        if nxt is not None:
            label = T('LIGHTS OUT') if nxt.id == "race" else T('NEXT · {upper}', upper=nxt.name.upper())
            return label, fmt_long(nxt.ts - now), False
    if race_ts is None:
        return "", "", False
    if now >= race_ts:
        return T('LIGHTS OUT'), T('Race finished'), False
    return T('LIGHTS OUT'), fmt_long(race_ts - now), False


class ScheduleTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        self.anim_start: float | None = None
        self.anim_timer = QTimer(self)
        self.anim_timer.setInterval(40)
        self.anim_timer.timeout.connect(self._anim_step)
        self.session_rows: list[tuple] = []
        self.build()

    def build(self) -> None:
        t, ctx = self.t, self.ctx
        race = ctx.store.race
        if race is None:
            loading = ctx.store.status.get("loading")
            self.hint(T('Loading…') if loading else T('No upcoming race'))
            self.finish()
            self.gantry = None
            return

        top = QWidget()
        top_l = QHBoxLayout(top)
        top_l.setContentsMargins(t.px(4), t.px(15), 0, 0)
        top_l.setSpacing(t.px(8))
        left = QWidget()
        ll = QVBoxLayout(left)
        ll.setContentsMargins(0, 0, 0, 0)
        ll.setSpacing(t.px(3))
        name = Wrap(race.circuit_name, t, 14, t.on_surface)
        ll.addWidget(name)
        ll.addWidget(Text(race.location, t, 12, False, t.muted))
        ll.addSpacing(t.px(6))
        self.gantry = LightsGantry(t)
        ll.addWidget(self.gantry)
        ll.addSpacing(t.px(4))
        self.cd_label = Text(T('LIGHTS OUT'), t, 11, True, t.muted, tracking=0.6)
        ll.addWidget(self.cd_label)
        self.countdown = Text("", t, 22, True, t.accent)
        self.countdown.setAccessibleName(T('Countdown'))
        ll.addWidget(self.countdown)
        ll.addStretch(1)
        top_l.addWidget(left, 1)

        pts = ctx.tracks.points(race.circuit_id)
        self.track = None
        if pts:
            w, h = (120, 108) if ctx.compact else (170, 150)
            self.track = TrackView(pts, t, w, h)
            self.track.setToolTip(race.circuit_name)
            top_l.addWidget(self.track)
        self.lay.addWidget(top)
        self.lay.addSpacing(t.px(10))
        now = now_sec()
        fc = ctx.store.get("weather")
        forecast = fc if fc.get("round") == f"{race.season}-{race.round}" else None

        # Table header, as on the Weather tab: SESSIONS (title) · START · SKY · TEMP · RAIN
        measured = measured_for(ctx.store, race)
        has_wx = bool(forecast) or bool(measured)
        name_w = 116 if ctx.compact else 128
        head = Row(t)
        head.lay.setContentsMargins(t.px(4), 0, t.px(4), t.px(2))
        head.cell(with_help(Text(T('SESSIONS'), t, 12, True, t.on_surface, tracking=0.4), t), name_w)
        head.cell(Text(T('START'), t, 10, True, t.muted, tracking=0.3), 92)
        tip = [T('Start: the start time in your time zone.')]
        if has_wx:
            head.cell(Text(T('SKY'), t, 10, True, t.muted, tracking=0.3), None, 1)
            head.cell(Text(T('TEMP'), t, 10, True, t.muted, RIGHT, tracking=0.3), TEMP_W)
            head.cell(Text(T('RAIN'), t, 10, True, t.muted, RIGHT, tracking=0.3), RAIN_W)
            tip.append(T('Sky, temperature and the highest chance of rain during each session. Finished sessions '
                         'show what F1 measured at the track (Rained / Dry, Wet / Dry). More on the Weather tab.'))
        else:
            head.cell(QWidget(), None, 1)
        head.setToolTip("\n".join(tip))
        self.lay.addWidget(head)
        self.session_rows = []
        for s in race.sessions:
            r = Row(t)
            name_cell = Text(s.name, t, 13)
            r.cell(name_cell, name_w)
            delayed = bool(s.delay)
            when = Text(fmt_datetime(s.ts, ctx.zone, ctx.twelve) + ("*" if delayed else ""), t, 13, False,
                        t.secondary if delayed else None)
            r.cell(when, 92)
            tips = [fmt_datetime(s.ts, ctx.zone, ctx.twelve, long=True) + "  " + utc_offset_label(s.ts, ctx.zone)]
            if s.source == "feed":
                tips.append(T("Start time from F1's live timing (revised)"))
            elif s.source == "manual":
                tips.append(T('Start time corrected by hand in Settings'))
            else:
                tips.append(T('Published schedule time'))
            if delayed:
                sign = "+" if s.delay > 0 else ""
                tips.append(T("* {delay} min vs. published time", delay=f"{sign}{s.delay}"))
            if has_wx:
                wx_tip = session_cells(self, r, s, forecast, measured, now, 13)
                if wx_tip:
                    tips.append(wx_tip)
            else:
                r.cell(QWidget(), None, 1)
            r.setToolTip("\n".join(tips))
            self.lay.addWidget(r)
            self.session_rows.append((s, name_cell, when))
        self.finish()
        self.tick(now)

    def on_shown(self) -> None:
        if self.gantry is None:
            return
        if self.ctx.settings.animations:
            self.anim_start = time.monotonic()
            self.anim_timer.start()
            if self.track:
                self.track.animate_in(INTRO_FADE_MS, 900)
            self._anim_step()

    def _race_underway(self, now: int) -> bool:
        ts = session_ts(self.ctx.store.race.sessions, "race") if self.ctx.store.race else None
        return ts is not None and now >= ts

    def _anim_step(self) -> None:
        if self.anim_start is None or self.gantry is None:
            self.anim_timer.stop()
            return
        ms = (time.monotonic() - self.anim_start) * 1000
        cols = LightsGantry.COLUMNS
        lit = min(cols, int(ms // LIGHT_STEP_MS))
        done_at = cols * LIGHT_STEP_MS + LIGHT_HOLD_MS
        go = ms >= done_at and self._race_underway(now_sec())
        self.gantry.set_state(cols if go else lit, go)
        if ms >= done_at + 400:
            self.anim_timer.stop()
            self.anim_start = None

    def tick(self, now: int) -> None:
        race = self.ctx.store.race
        if race is None or self.gantry is None:
            return
        if self.anim_start is None:
            self.gantry.set_state(LightsGantry.COLUMNS, self._race_underway(now))
        label, text, live = countdown_state(race, self.ctx.settings, now)
        self.cd_label.setText(label)
        self.cd_label.setColor(self.t.accent if live else self.t.muted)
        self.countdown.setText(text)
        nxt = next_session(race.sessions, now)
        for s, name_w, when in self.session_rows:
            is_next = nxt is not None and s.ts == nxt.ts
            past = s.ts <= now
            name_w._font.setBold(is_next)
            name_w.setColor(self.t.accent if is_next else (self.t.muted if past else self.t.on_surface))
            when.setColor(self.t.on_surface if is_next else self.t.muted)
