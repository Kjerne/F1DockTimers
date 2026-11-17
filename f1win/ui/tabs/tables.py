"""Grid, Last, Drivers and Teams tabs."""
from __future__ import annotations

from ...core.timeutil import fmt_date
from .common import Tab, empty_message


class GridTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        grid = ctx.store.get("grid")
        rows = grid.get("rows") or []
        if not rows:
            self.hint(empty_message(grid, "No grid available. It appears here after qualifying."))
            self.finish()
            return
        self.header("STARTING GRID")
        if grid.get("provisional", True):
            # Never present the qualifying order as the final grid.
            self.hint("Provisional — excludes penalties")
        else:
            self.hint("Official starting grid")
        for row in rows:
            self.result_row(row, 80)
        self.finish()


class ResultsTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        res = ctx.store.get("results")
        rows = res.get("rows") or []
        if not rows:
            self.hint(empty_message(res, "No results yet"))
            self.finish()
            return
        self.header(f"LAST RACE · {res.get('race_name', '').upper()}")
        bits = []
        if res.get("date_ts"):
            bits.append(fmt_date(int(res["date_ts"]), ctx.zone))
        if res.get("circuit"):
            bits.append(res["circuit"])
        if bits:
            self.hint(" · ".join(bits))
        fl = res.get("fastest")
        if fl:
            self.hint(f"Fastest lap  {fl.get('code', '')}  {fl.get('time', '')}  (lap {fl.get('lap', '')})")
        for row in rows:
            r = self.result_row(row, 92, points=True)
            if row.get("status"):
                r.setToolTip(f"{row.get('name', '')} · {row['status']}" + (" · Open on Wikipedia" if row.get("url") else ""))
        self.finish()


class DriversTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        d = ctx.store.get("drivers")
        rows = d.get("rows") or []
        if not rows:
            self.hint(empty_message(d, "No standings available"))
            self.finish()
            return
        self.header("DRIVER STANDINGS")
        for row in rows:
            self.standing_row(row, row.get("code", ""), row.get("team", ""))
        self.finish()


class ConstructorsTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        d = ctx.store.get("constructors")
        rows = d.get("rows") or []
        if not rows:
            self.hint(empty_message(d, "No standings available"))
            self.finish()
            return
        self.header("CONSTRUCTOR STANDINGS")
        for row in rows:
            self.standing_row(row, row.get("name", ""), "")
        self.finish()
