"""Shared context and row builders for the six tabs (panel.luau helpers)."""
from __future__ import annotations

from dataclasses import dataclass, field

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QSizePolicy, QVBoxLayout, QWidget

from ...core.teams import team_color
from ...core.timeutil import zone_for
from ..theme import Theme
from ..widgets import Chip, Row, Text, Wrap

RIGHT = Qt.AlignmentFlag.AlignRight
LEFT = Qt.AlignmentFlag.AlignLeft


@dataclass
class Ctx:
    store: object
    settings: object
    theme: Theme
    tracks: object
    open_link: object
    compact: bool = False
    extra: dict = field(default_factory=dict)

    @property
    def zone(self):
        return zone_for(self.settings.timezone)

    @property
    def twelve(self) -> bool:
        return self.settings.time_format == "12h"

    def is_favorite(self, row: dict) -> bool:
        fd = (self.settings.favorite_driver or "").upper().replace(" ", "")
        ft = (self.settings.favorite_team or "").lower().replace(" ", "")
        return bool((fd and row.get("code") == fd) or (ft and row.get("team_id") == ft))

    def chip(self, row: dict, explicit: str | None = None):
        if not self.settings.team_colors:
            return None
        color = explicit or team_color(row.get("team_id", ""))
        return Chip(color, self.theme)


class Tab(QWidget):
    """A tab body. build() creates widgets; tick() moves countdowns cheaply."""

    def __init__(self, ctx: Ctx, parent=None) -> None:
        super().__init__(parent)
        self.ctx = ctx
        self.t = ctx.theme
        self.lay = QVBoxLayout(self)
        self.lay.setContentsMargins(0, self.t.px(8), self.t.px(4), 0)
        self.lay.setSpacing(self.t.px(4))
        self.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Minimum)

    def tick(self, now: int) -> None:
        pass

    def on_shown(self) -> None:
        pass

    # ── builders ────────────────────────────────────────────────────────────

    def header(self, text: str) -> Text:
        w = Text(text, self.t, 11, True, self.t.muted, tracking=0.3)
        self.lay.addWidget(w)
        return w

    def hint(self, text: str, color: str | None = None) -> Wrap:
        w = Wrap(text, self.t, 12, color)
        self.lay.addWidget(w)
        return w

    def finish(self) -> None:
        self.lay.addStretch(1)

    def pos_label(self, pos) -> Text:
        try:
            n = int(pos)
        except (TypeError, ValueError):
            n = None
        color = self.t.on_surface if (n is not None and n <= 3) else self.t.muted
        return Text(str(pos), self.t, 13, False, color, RIGHT)

    def _row(self, row: dict) -> Row:
        return Row(self.t, row.get("url", ""), self.ctx.open_link)

    def result_row(self, row: dict, detail_width: int, points: bool = False) -> Row:
        """[chip] pos  code  team  detail  [points]"""
        r = self._row(row)
        chip = self.ctx.chip(row)
        if chip:
            r.cell(chip)
        r.cell(self.pos_label(row.get("pos", "")), 28)
        code_color = self.t.accent if self.ctx.is_favorite(row) else self.t.on_surface
        r.cell(Text(row.get("code", ""), self.t, 13, True, code_color), 44)
        team = Text(row.get("team", ""), self.t, 13, False, self.t.muted)
        team.setToolTip(row.get("name", ""))
        r.cell(team, None, 1)
        r.cell(Text(row.get("detail", ""), self.t, 13, False, self.t.on_surface, RIGHT), detail_width)
        if points:
            pts = row.get("pts", "")
            r.cell(Text(pts if pts != "0" else "", self.t, 13, False, self.t.muted, RIGHT), 32)
        self.lay.addWidget(r)
        return r

    def standing_row(self, row: dict, primary: str, secondary: str) -> Row:
        """[chip] pos  primary  secondary  wins  gap  points"""
        r = self._row(row)
        chip = self.ctx.chip(row)
        if chip:
            r.cell(chip)
        r.cell(self.pos_label(row.get("pos", "")), 28)
        color = self.t.accent if self.ctx.is_favorite(row) else self.t.on_surface
        if secondary:
            prim = Text(primary, self.t, 13, True, color)
            prim.setToolTip(row.get("name", ""))
            r.cell(prim, 48)
            r.cell(Text(secondary, self.t, 13, False, self.t.muted), None, 1)
        else:
            r.cell(Text(primary, self.t, 13, True, color), None, 1)
        wins = int(row.get("wins") or 0)
        w = Text(f"{wins}W" if wins else "", self.t, 12, False, self.t.muted, RIGHT)
        w.setToolTip(f"{wins} win{'s' if wins != 1 else ''}")
        r.cell(w, 30)
        gap = int(row.get("gap") or 0)
        r.cell(Text(f"-{gap}" if gap > 0 else "", self.t, 12, False, self.t.muted, RIGHT), 40)
        r.cell(Text(row.get("pts", ""), self.t, 13, True, self.t.on_surface, RIGHT), 44)
        self.lay.addWidget(r)
        return r


def empty_message(dataset: dict, none_text: str) -> str:
    if dataset.get("failed"):
        return "Couldn't reach F1 data right now. Retrying automatically."
    if dataset.get("loaded"):
        return none_text
    return "Loading…"
