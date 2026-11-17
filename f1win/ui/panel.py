"""The F1 panel (render() in panel.luau): header, title, pill tabs, tab body.

One class serves both the tray popup and the desktop widget; each owns an
instance, both read the same DataStore.
"""
from __future__ import annotations

from PySide6.QtCore import QRectF, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QKeySequence, QPainter, QPainterPath, QPen
from PySide6.QtWidgets import QFrame, QHBoxLayout, QScrollArea, QVBoxLayout, QWidget

from ..core.sessions import grid_applicable, live_active
from ..core.timeutil import fmt_stamp, now_sec
from .tabs.common import Ctx
from .tabs.live import LiveTab
from .tabs.schedule import ScheduleTab
from .tabs.tables import ConstructorsTab, DriversTab, GridTab, ResultsTab
from .widgets import Divider, FlagGlyph, IconButton, PillButton, Text

PIN_SVG = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="{c}" stroke-width="2" '
           'stroke-linecap="round" stroke-linejoin="round"><path d="M15 4.5l-4 4l-4 1.5l-1.5 1.5l7 7l1.5 -1.5l1.5 -4l4 -4"/>'
           '<path d="M9 15l-4.5 4.5"/><path d="M14.5 4l5.5 5.5"/></svg>')
GEAR_SVG = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="{c}" stroke-width="2" '
            'stroke-linecap="round" stroke-linejoin="round"><path d="M10.325 4.317c.426 -1.756 2.924 -1.756 3.35 0a1.724 '
            '1.724 0 0 0 2.573 1.066c1.543 -.94 3.31 .826 2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756 .426 1.756 2.924 0 '
            '3.35a1.724 1.724 0 0 0 -1.066 2.573c.94 1.543 -.826 3.31 -2.37 2.37a1.724 1.724 0 0 0 -2.572 1.065c-.426 1.756 '
            '-2.924 1.756 -3.35 0a1.724 1.724 0 0 0 -2.573 -1.066c-1.543 .94 -3.31 -.826 -2.37 -2.37a1.724 1.724 0 0 0 '
            '-1.065 -2.572c-1.756 -.426 -1.756 -2.924 0 -3.35a1.724 1.724 0 0 0 1.066 -2.573c-.94 -1.543 .826 -3.31 2.37 '
            '-2.37c1 .608 2.296 .07 2.572 -1.065z"/><path d="M9 12a3 3 0 1 0 6 0a3 3 0 0 0 -6 0"/></svg>')
REFRESH_SVG = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="{c}" stroke-width="2" '
               'stroke-linecap="round" stroke-linejoin="round"><path d="M20 11a8.1 8.1 0 0 0 -15.5 -2m-.5 -4v4h4"/>'
               '<path d="M4 13a8.1 8.1 0 0 0 15.5 2m.5 4v-4h-4"/></svg>')

TAB_LABELS = {"schedule": "Schedule", "grid": "Grid", "live": "Live", "results": "Last",
              "drivers": "Drivers", "constructors": "Teams"}
RELEVANT = {"schedule": {"race", "status"}, "grid": {"grid", "race"}, "live": {"live", "race"},
            "results": {"results"}, "drivers": {"drivers"}, "constructors": {"constructors"}}


def tab_list(store, settings, now: int) -> list[tuple[str, str]]:
    race = store.race
    sessions = race.sessions if race else []
    force = settings.debug_force_tabs
    tabs = [("schedule", "Schedule")]
    if (race is not None and grid_applicable(sessions, now)) or force:
        tabs.append(("grid", "Grid"))
    is_live = live_active(store.get("live"), sessions, now, force)
    tabs.append(("live", "● Live" if is_live else "Live"))
    tabs += [("results", "Last"), ("drivers", "Drivers"), ("constructors", "Teams")]
    return tabs


def pick_view(store, settings, now: int) -> str:
    race = store.race
    sessions = race.sessions if race else []
    if live_active(store.get("live"), sessions, now, settings.debug_force_tabs):
        return "live"
    if race is not None and grid_applicable(sessions, now):
        return "grid"
    return settings.default_tab if settings.default_tab in ("drivers", "constructors") else "schedule"


class F1Panel(QWidget):
    close_requested = Signal()
    settings_requested = Signal()
    refresh_requested = Signal()
    pin_toggled = Signal(bool)

    def __init__(self, ctx: Ctx, mode: str = "popup", parent=None) -> None:
        super().__init__(parent)
        self.ctx = ctx
        self.mode = mode
        self.view = "schedule"
        self.bg_opacity = 1.0
        self.pinned = False
        self.tab_widget = None
        self._tabs_sig: list = []
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, False)
        self._rebuild_timer = QTimer(self)
        self._rebuild_timer.setSingleShot(True)
        self._rebuild_timer.setInterval(60)
        self._rebuild_timer.timeout.connect(self.rebuild_body)
        self.ctx.store.changed.connect(self._data_changed)
        self.build_chrome()

    # ── chrome ──────────────────────────────────────────────────────────────

    def build_chrome(self) -> None:
        old = self.layout()
        if old is not None:
            QWidget().setLayout(old)  # reparent the old layout away so it is deleted
        t = self.ctx.theme
        root = QVBoxLayout(self)
        pad = t.px(14)
        root.setContentsMargins(pad, t.px(12), pad, t.px(8))
        root.setSpacing(t.px(10))

        header = QHBoxLayout()
        header.setSpacing(t.px(8))
        header.addWidget(FlagGlyph(t, t.accent, 16))
        header.addWidget(Text("FORMULA 1", t, 11, True, t.muted, tracking=0.6))
        header.addStretch(1)
        self.round_label = Text("", t, 11, False, t.muted, Qt.AlignmentFlag.AlignRight)
        header.addWidget(self.round_label)
        if self.mode == "popup":
            self.pin_btn = IconButton(PIN_SVG, t, "Pin (keep open when clicking elsewhere)", 14)
            self.pin_btn.setActive(self.pinned)
            self.pin_btn.clicked.connect(self._toggle_pin)
            header.addWidget(self.pin_btn)
            gear = IconButton(GEAR_SVG, t, "Settings (Ctrl+,)", 14)
            gear.clicked.connect(self.settings_requested)
            header.addWidget(gear)
        root.addLayout(header)
        root.addWidget(Divider(t))

        self.title = Text("", t, 20, True, t.on_surface)
        self.title.setAccessibleName("Grand Prix")
        root.addWidget(self.title)

        self.tabbar_host = QWidget()
        self.tabbar = QHBoxLayout(self.tabbar_host)
        self.tabbar.setContentsMargins(0, 0, 0, 0)
        self.tabbar.setSpacing(t.px(2))
        root.addWidget(self.tabbar_host)

        self.scroll = QScrollArea()
        self.scroll.setFrameShape(QFrame.Shape.NoFrame)
        self.scroll.setWidgetResizable(True)
        self.scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.scroll.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.scroll.setStyleSheet(
            "QScrollArea{background:transparent;} QScrollArea>QWidget>QWidget{background:transparent;}"
            f"QScrollBar:vertical{{background:transparent;width:{t.px(6)}px;margin:0;}}"
            f"QScrollBar::handle:vertical{{background:rgba(198,197,208,0.45);border-radius:{t.px(3)}px;min-height:{t.px(24)}px;}}"
            "QScrollBar::handle:vertical:hover{background:rgba(198,197,208,0.7);}"
            "QScrollBar::add-line,QScrollBar::sub-line{height:0;}"
            "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}")
        self.scroll.viewport().setAutoFillBackground(False)
        root.addWidget(self.scroll, 1)

        foot = QHBoxLayout()
        foot.setSpacing(t.px(6))
        self.footer = Text("", t, 10, False, t.muted)
        self.footer.setColor(QColor(t.muted).darker(125).name())
        foot.addWidget(self.footer, 1)
        refresh = IconButton(REFRESH_SVG, t, "Refresh (R)", 12)
        refresh.clicked.connect(self.refresh_requested)
        foot.addWidget(refresh)
        root.addLayout(foot)

        self._tabs_sig = []
        self.rebuild_body()

    def _toggle_pin(self) -> None:
        self.pinned = not self.pinned
        self.pin_btn.setActive(self.pinned)
        self.pin_toggled.emit(self.pinned)

    def set_pinned(self, on: bool) -> None:
        self.pinned = on
        if self.mode == "popup":
            self.pin_btn.setActive(on)

    # ── content ─────────────────────────────────────────────────────────────

    def _data_changed(self, key: str) -> None:
        if key in RELEVANT.get(self.view, set()) or key == "race":
            self._rebuild_timer.start()
        elif key == "live":
            self._refresh_tabbar(now_sec())
        self._update_header()

    def _update_header(self) -> None:
        store = self.ctx.store
        race = store.race
        self.round_label.setText(f"Round {race.round} · {race.season}" if race else "")
        if race is not None:
            self.title.setText(race.name)
        elif store.status.get("loading"):
            self.title.setText("Loading…")
        else:
            self.title.setText("No upcoming race")
        stamp = fmt_stamp(store.last_updated(), self.ctx.zone, self.ctx.twelve)
        bits = [f"Updated {stamp}"]
        if store.status.get("offline"):
            bits.append("offline · showing cached data")
        elif store.status.get("error") and store.status["error"] not in ("", "no_race"):
            bits.append(f"couldn't load ({store.status['error']}), retrying")
        self.footer.setText("  ·  ".join(bits))

    def _refresh_tabbar(self, now: int) -> None:
        tabs = tab_list(self.ctx.store, self.ctx.settings, now)
        sig = [(i, lbl, i == self.view) for i, lbl in tabs]
        if sig == self._tabs_sig:
            return
        self._tabs_sig = sig
        while self.tabbar.count():
            item = self.tabbar.takeAt(0)
            if item.widget():
                item.widget().hide()          # never painted again, even before deletion
                item.widget().deleteLater()
        for tid, label in tabs:
            b = PillButton(label, self.ctx.theme, tid == self.view)
            b.clicked.connect(lambda tid=tid: self.set_view(tid))
            self.tabbar.addWidget(b)
        self.tabbar.addStretch(1)

    def valid_views(self, now: int) -> list[str]:
        return [tid for tid, _ in tab_list(self.ctx.store, self.ctx.settings, now)]

    def set_view(self, view: str, animate: bool = True) -> None:
        if view not in self.valid_views(now_sec()):
            view = "schedule"
        changed = view != self.view
        self.view = view
        self.rebuild_body(reset_scroll=changed)
        if animate and isinstance(self.tab_widget, ScheduleTab):
            self.tab_widget.on_shown()

    def open_fresh(self) -> None:
        """Called when the popup opens: pick the tab like onOpen() did."""
        self.set_view(pick_view(self.ctx.store, self.ctx.settings, now_sec()))

    def cycle(self, step: int) -> None:
        views = self.valid_views(now_sec())
        i = views.index(self.view) if self.view in views else 0
        self.set_view(views[(i + step) % len(views)])

    def rebuild_body(self, reset_scroll: bool = False) -> None:
        now = now_sec()
        if self.view not in self.valid_views(now):
            self.view = "schedule"
        self._update_header()
        self._tabs_sig = []
        self._refresh_tabbar(now)
        bar = self.scroll.verticalScrollBar()
        keep = 0 if reset_scroll else bar.value()
        cls = {"schedule": ScheduleTab, "grid": GridTab, "live": LiveTab, "results": ResultsTab,
               "drivers": DriversTab, "constructors": ConstructorsTab}[self.view]
        self.ctx.compact = self.width() and self.width() < self.ctx.theme.px(400)
        w = cls(self.ctx)
        old = self.scroll.takeWidget()
        self.scroll.setWidget(w)
        if old is not None:
            old.deleteLater()
        self.tab_widget = w
        QTimer.singleShot(0, lambda: bar.setValue(keep))

    def tick(self, now: int) -> None:
        if self.tab_widget is not None:
            self.tab_widget.tick(now)
        if now % 30 == 0:
            self._refresh_tabbar(now)
            if self.view not in self.valid_views(now):
                self.set_view("schedule", animate=False)
            self._update_header()

    def resizeEvent(self, e) -> None:
        super().resizeEvent(e)
        compact = self.width() < self.ctx.theme.px(400)
        if compact != bool(self.ctx.compact) and self.view == "schedule":
            self._rebuild_timer.start()

    # ── input ───────────────────────────────────────────────────────────────

    def keyPressEvent(self, e) -> None:
        k = e.key()
        if k == Qt.Key.Key_Left:
            self.cycle(-1)
        elif k == Qt.Key.Key_Right:
            self.cycle(1)
        elif k == Qt.Key.Key_R and not e.modifiers():
            self.refresh_requested.emit()
        elif k == Qt.Key.Key_Escape:
            self.close_requested.emit()
        elif e.matches(QKeySequence.StandardKey.Preferences) or (
                k == Qt.Key.Key_Comma and e.modifiers() & Qt.KeyboardModifier.ControlModifier):
            self.settings_requested.emit()
        elif k in (Qt.Key.Key_PageDown, Qt.Key.Key_Down):
            bar = self.scroll.verticalScrollBar()
            bar.setValue(bar.value() + (bar.pageStep() if k == Qt.Key.Key_PageDown else bar.singleStep() * 3))
        elif k in (Qt.Key.Key_PageUp, Qt.Key.Key_Up):
            bar = self.scroll.verticalScrollBar()
            bar.setValue(bar.value() - (bar.pageStep() if k == Qt.Key.Key_PageUp else bar.singleStep() * 3))
        else:
            super().keyPressEvent(e)

    # ── painting ────────────────────────────────────────────────────────────

    def paintEvent(self, _e) -> None:
        t = self.ctx.theme
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        rad = t.px(12)
        path = QPainterPath()
        path.addRoundedRect(r, rad, rad)
        bg = QColor(t.surface)
        bg.setAlphaF(self.bg_opacity)
        p.fillPath(path, bg)
        border = QColor(t.outline)
        border.setAlphaF(0.55 if self.bg_opacity > 0.05 else 0.0)
        p.setPen(QPen(border, 1))
        p.drawPath(path)
