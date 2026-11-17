"""Live tab: running order, flag and session clock (OpenF1 or the experimental F1 stream)."""
from __future__ import annotations

from PySide6.QtWidgets import QHBoxLayout, QWidget

from ...core import credentials_hint
from ...core.sessions import live_active, near_session, next_session
from ...core.teams import friendly_flag
from ...core.timeutil import fmt_clock, fmt_datetime, now_sec
from ..widgets import Row, Text, Wrap
from .common import RIGHT, Tab

MESSAGES = {
    "auth": credentials_hint.AUTH,
    "login": "OpenF1 rejected the stored username and password. Check them in Settings › Formula 1.",
    "token": "OpenF1 refused the access token. A new one is requested automatically.",
    "rate": "The live feed is rate limited; it will retry shortly.",
    "net": "The live feed could not be reached; retrying.",
    "free": ("Live timing during a session needs the OpenF1 Sponsor plan (Settings › Live data). "
             "On the free plan the full classification appears below about 20–30 minutes after "
             "the session ends."),
}


class LiveTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        self.clock_label = None
        self.build()

    def build(self) -> None:
        t, ctx = self.t, self.ctx
        now = now_sec()
        race = ctx.store.race
        sessions = race.sessions if race else []
        live = ctx.store.get("live")
        if not live_active(live, sessions, now, ctx.settings.debug_force_tabs):
            self.header("LIVE TIMING")
            problem = live.get("error") or ""
            if race is not None and near_session(sessions, now) and problem:
                self.lay.addWidget(Wrap("A session is running, but live timing is not available.", t, 13, t.on_surface))
            else:
                self.lay.addWidget(Wrap("No session running right now.", t, 13, t.on_surface))
                self.hint("Live positions and track flags appear here automatically during "
                          "practice, qualifying and the race.")
            if problem in MESSAGES:
                self.hint(MESSAGES[problem])
            elif ctx.settings.live_source == "openf1" and race is not None and near_session(sessions, now):
                self.hint(MESSAGES["free" if ctx.settings.openf1_plan == "free" else "auth"])
            nxt = next_session(sessions, now) if race else None
            if nxt is not None:
                self.lay.addWidget(Text(f"Next: {nxt.name} · {fmt_datetime(nxt.ts, ctx.zone, ctx.twelve)}",
                                        t, 12, True, t.accent))
            self.last_session()
            self.finish()
            return

        session = live.get("session") if isinstance(live.get("session"), dict) else None
        title = "● LIVE"
        if session and session.get("name"):
            part = int(session.get("part") or 0)
            title = f"● LIVE · {session['name']}" + (f" Q{part}" if part > 0 else "")
        head = QWidget()
        hl = QHBoxLayout(head)
        hl.setContentsMargins(0, 0, 0, 0)
        hl.setSpacing(t.px(10))
        hl.addWidget(Text(title, t, 12, True, t.accent))
        self.clock = live.get("clock") if isinstance(live.get("clock"), dict) else None
        if self.clock:
            self.clock_label = Text("", t, 12, False, t.on_surface)
            hl.addWidget(self.clock_label)
        flag = friendly_flag(live.get("flag"))
        if flag:
            hl.addWidget(Text(flag, t, 12, False, t.muted))
        hl.addStretch(1)
        self.lay.addWidget(head)
        msgs = live.get("messages") or []
        if msgs:
            m = Wrap(msgs[0], t, 11, t.muted)
            m.setToolTip("\n".join(msgs))
            self.lay.addWidget(m)

        fd = (ctx.settings.favorite_driver or "").upper()
        qualifying = bool(session and session.get("kind") == "Qualifying")
        for i, row in enumerate(live.get("rows") or []):
            if i >= 24:
                break
            state = row.get("state", "")
            dimmed = state in ("out", "prev")
            r = Row(t)
            if ctx.settings.team_colors and row.get("color"):
                r.cell(ctx.chip(row, row["color"]))
            r.cell(self.pos_label(row.get("pos", "")), 28)
            code_color = t.accent if (fd and row.get("code") == fd) else (t.muted if state == "out" else t.on_surface)
            r.cell(Text(row.get("code", ""), t, 13, True, code_color), 46)
            team = row.get("team", "")
            if state == "out" and int(row.get("out_part") or 0) > 0:
                team = f"out Q{row['out_part']}"
            elif row.get("in_pit"):
                team = f"{team}  ·  PIT"
            r.cell(Text(team, t, 13, False, t.muted), None, 1)
            if row.get("pits") is not None and session and not qualifying:
                r.cell(Text(f"{row['pits']} pit", t, 12, False, t.muted, RIGHT), 44)
            if row.get("best") or row.get("gap"):
                r.cell(Text(row.get("best", ""), t, 13, False, t.muted if dimmed else t.on_surface, RIGHT), 80)
                r.cell(Text(row.get("gap", ""), t, 12, False, t.error if state == "danger" else t.muted, RIGHT), 60)
            self.lay.addWidget(r)
        self.finish()
        self.tick(now)

    def last_session(self) -> None:
        """Final classification of the latest finished session (OpenF1, free plan)."""
        t, ctx = self.t, self.ctx
        data = ctx.store.get("last_session")
        rows = data.get("rows") or []
        sess = data.get("session") or {}
        if not rows:
            return
        self.lay.addSpacing(t.px(10))
        where = sess.get("location") or sess.get("circuit") or ""
        self.header(f"LAST SESSION · {where.upper()} · {str(sess.get('name', '')).upper()}".replace(" ·  · ", " · "))
        when = fmt_datetime(sess["start"], ctx.zone, ctx.twelve, long=True) if sess.get("start") else ""
        self.hint(f"Final classification · {when} · OpenF1" if when else "Final classification · OpenF1")
        fd = (ctx.settings.favorite_driver or "").upper()
        for row in rows:
            r = Row(t)
            if ctx.settings.team_colors and row.get("color"):
                r.cell(ctx.chip(row, row["color"]))
            r.cell(self.pos_label(row.get("pos", "")), 28)
            code_color = t.accent if (fd and row.get("code") == fd) else t.on_surface
            r.cell(Text(row.get("code", ""), t, 13, True, code_color), 44)
            r.cell(Text(row.get("team", ""), t, 13, False, t.muted), None, 1)
            r.cell(Text(row.get("detail", ""), t, 13, False, t.muted if row.get("status") else t.on_surface,
                        RIGHT), 92)
            r.cell(Text(row.get("gap", ""), t, 12, False, t.muted, RIGHT), 78)
            if row.get("laps") is not None:
                r.setToolTip(f"{row['laps']} laps" + (f" · {row['pts']} pts" if row.get("pts") else ""))
            self.lay.addWidget(r)

    def tick(self, now: int) -> None:
        if self.clock_label is not None and self.clock:
            left = self.clock["remaining"] - ((now - self.clock["at"]) if self.clock.get("running") else 0)
            self.clock_label.setText(f"{fmt_clock(left)} left")
