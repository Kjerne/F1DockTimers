"""Grid, Last, Drivers and Teams tabs."""
from __future__ import annotations

from ...core.spoilers import COMPETITIVE, session_over
from ...core.timeutil import fmt_date, now_sec
from PySide6.QtWidgets import QHBoxLayout

from ..widgets import ClickText, PillButton, Text
from .common import RIGHT, Tab, empty_message
from ...i18n import T


class GridTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        grid = ctx.store.get("grid")
        rows = grid.get("rows") or []
        if not rows:
            self.hint(empty_message(grid, T('No grid available. It appears here after qualifying.')))
            self.finish()
            return
        race = ctx.store.race
        official = ctx.store.get("grid_official")
        has_official = bool(official.get("rows")) and race is not None and \
            official.get("round") == f"{race.season}-{race.round}"
        show_official = has_official and ctx.settings.grid_official
        self.header(T('STARTING GRID'))
        if has_official:
            switch = QHBoxLayout()
            switch.setSpacing(self.t.px(4))
            for flag, label, tip in ((True, T("Official"), T("The FIA starting grid with all penalties applied.")),
                                     (False, T("Provisional"), T("The order from qualifying, before penalties."))):
                b = PillButton(label, self.t, flag == show_official)
                b.setToolTip(tip)
                b.clicked.connect(lambda flag=flag: self._switch(flag))
                switch.addWidget(b)
            switch.addStretch(1)
            self.lay.addLayout(switch)
        if show_official:
            penalised = sum(1 for r in official["rows"] if (r.get("moved") or 0) > 0)
            note = (T('1 driver starts further back than qualified.') if penalised == 1 else
                    T('{count} drivers start further back than qualified.', count=penalised) if penalised else
                    T('Same order as qualifying.'))
            self.hint(T('Official grid with penalties (OpenF1), checked again just before the race.') + " " + note)
            for row in official["rows"]:
                r = self.result_row(row, 80)
                # Penalised drivers get a short red note right after their team ("+5 places"); the
                # times stay in their own column.
                m = row.get("moved") or 0
                if m > 0:
                    lay = r.lay
                    team_i = next(i for i in range(lay.count()) if lay.stretch(i) == 1)
                    lay.setStretch(team_i, 0)
                    mark = Text(T("+{places} places", places=m), self.t, 11, True, self.t.error)
                    lay.insertWidget(team_i + 1, mark, 1)
                    r.setToolTip(T('Qualified P{qpos}, starts P{pos} ({change} places)', qpos=int(row["pos"]) - m,
                                   pos=row["pos"], change=f"{m:+d}"))
        else:
            # Never present the qualifying order as the final grid.
            self.hint(T('Provisional: the qualifying order, before penalties.') if grid.get("provisional", True)
                      else T('Official starting grid'))
            for row in rows:
                self.result_row(row, 80)
        self.finish()

    def _switch(self, official: bool) -> None:
        self.ctx.settings.grid_official = official
        self.ctx.settings.save()
        self.ctx.store.changed.emit("grid")              # the panel rebuilds the Grid tab


class ResultsTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        res = ctx.store.get("results")
        rows = res.get("rows") or []
        if not rows:
            self.hint(empty_message(res, T('No results yet')))
            self.finish()
            return
        self.header(T('LAST RACE · {upper}', upper=res.get('race_name', '').upper()))
        bits = []
        if res.get("date_ts"):
            bits.append(fmt_date(int(res["date_ts"]), ctx.zone))
        if res.get("circuit"):
            bits.append(res["circuit"])
        if bits:
            self.hint(" · ".join(bits))
        fl = res.get("fastest")
        if fl:
            self.hint(T('Fastest lap  {driver}  {time}  (lap {lap})', driver=fl.get('code', ''), time=fl.get('time', ''), lap=fl.get('lap', '')))
        for row in rows:
            r = self.result_row(row, 92, points=True)
            if row.get("status"):
                r.setToolTip(T('{name} · {status}', name=row.get('name', ''), status=row['status']) + (T(' · Open on Wikipedia') if row.get("url") else ""))
        self.finish()


class DriversTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        d = ctx.store.get("drivers")
        rows = d.get("rows") or []
        if not rows:
            self.hint(empty_message(d, T('No standings available')))
            self.finish()
            return
        self.header(T('DRIVER STANDINGS'))
        for row in rows:
            self.standing_row(row, row.get("code", ""), row.get("team", ""))
        self.finish()


class ConstructorsTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        d = ctx.store.get("constructors")
        rows = d.get("rows") or []
        if not rows:
            self.hint(empty_message(d, T('No standings available')))
            self.finish()
            return
        self.header(T('CONSTRUCTOR STANDINGS'))
        for row in rows:
            self.standing_row(row, row.get("name", ""), "")
        self.finish()


class SpoilerTab(Tab):
    """Shown instead of results, grid, live timing and standings in spoiler-free mode."""

    def __init__(self, ctx, reveal, parent=None) -> None:
        super().__init__(ctx, parent)
        self.header(T('SPOILER-FREE MODE'))
        race, now = ctx.store.race, now_sec()
        started = race is not None and any(s.id in COMPETITIVE and s.ts <= now for s in race.sessions)
        what = race.name if started else (ctx.store.get("results").get("race_name") or "the last race")
        if session_over(ctx.store, now):
            self.hint(T("Results from {what} are hidden until you've watched it.", what=what))
        else:
            self.hint(T('A session at {what} is running. Live timing and results are hidden.', what=what))
        link = ClickText(T('Show results'), self.t, 13, True, self.t.accent)
        link.setToolTip(T('Reveals everything up to now. Hidden again when the next qualifying, sprint or race starts.'))
        link.clicked.connect(reveal)
        self.lay.addWidget(link)
        self.hint(T('Turn spoiler-free mode off in Settings › Formula 1.'), self.t.muted)
        self.finish()
