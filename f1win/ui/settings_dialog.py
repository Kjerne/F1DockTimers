"""Settings window. Short hints on the page, the details in hover tooltips (TIPS)."""
from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (QCheckBox, QColorDialog, QComboBox, QDialog, QDialogButtonBox, QDoubleSpinBox,
                               QFileDialog,
                               QFormLayout, QHBoxLayout, QLabel, QLineEdit, QMessageBox, QPushButton, QScrollArea,
                               QSpinBox, QTabWidget, QVBoxLayout, QWidget)

import dataclasses

from ..core.sessions import SESSION_NAMES
from ..core.timeutil import now_sec, zone_for
from ..core.timeutil import system_zone_name
from ..services import credentials
from ..services.poller import round_key
from ..version import APP_TITLE, VERSION
from .taskbar_pill import PRESETS, TOKENS, PillPreview, PillStyle, pill_state


def zone_names() -> list[str]:
    try:
        from zoneinfo import available_timezones
        return sorted(z for z in available_timezones() if "/" in z and not z.startswith(("Etc/", "SystemV/")))
    except Exception:
        return ["Europe/Copenhagen", "Europe/London", "UTC"]


def _arrow_file(color: str, up: bool = False) -> str:
    """A small chevron for combo and spin boxes, written once to the cache folder."""
    from ..paths import cache_dir
    path = cache_dir() / f"chevron_{'up' if up else 'down'}_{color.lstrip('#')}.svg"
    if not path.exists():
        d = "M6 15l6 -6l6 6" if up else "M6 9l6 6l6 -6"
        path.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" '
                        f'stroke="{color}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">'
                        f'<path d="{d}"/></svg>', encoding="utf-8")
    return path.as_posix()


def stylesheet(t) -> str:
    arrow = _arrow_file(t.muted)
    up = _arrow_file(t.muted, up=True)
    return f"""
    QDialog, QWidget#page {{ background:{t.surface}; color:{t.on_surface}; }}
    QLabel {{ color:{t.on_surface}; background:transparent; }}
    QLabel[hint="true"] {{ color:{t.muted}; }}
    QTabWidget::pane {{ border:1px solid {t.outline}; border-radius:8px; top:-1px; }}
    QTabBar::tab {{ background:transparent; color:{t.on_surface}; padding:6px 12px; border-radius:6px; margin:2px; }}
    QTabBar::tab:selected {{ background:{t.accent}; color:{t.on_accent}; }}
    QTabBar::tab:hover:!selected {{ background:rgba(228,225,236,0.08); }}
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {{ background:#24243A; color:{t.on_surface};
        border:1px solid {t.outline}; border-radius:6px; padding:4px 6px; selection-background-color:{t.accent};
        selection-color:{t.on_accent}; }}
    QComboBox {{ combobox-popup: 0; padding-right:24px; }}
    QComboBox:hover, QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover {{ border-color:{t.muted}; }}
    QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {{ border-color:{t.accent}; }}
    QComboBox::drop-down {{ subcontrol-origin:padding; subcontrol-position:center right; width:22px; border:none; }}
    QComboBox::down-arrow {{ image:url({arrow}); width:10px; height:10px; }}
    QComboBox QAbstractItemView {{ border:1px solid {t.outline}; outline:0; padding:2px; }}
    QComboBox QAbstractItemView::item {{ min-height:24px; padding:2px 8px; }}
    QComboBox QAbstractItemView {{ background:#24243A; color:{t.on_surface}; selection-background-color:{t.accent};
        selection-color:{t.on_accent}; }}
    QSpinBox, QDoubleSpinBox {{ padding-right:22px; }}
    QSpinBox::up-button, QDoubleSpinBox::up-button {{ subcontrol-origin:border; subcontrol-position:top right;
        width:20px; border:none; border-left:1px solid {t.outline}; border-top-right-radius:6px; background:#2A2A40; }}
    QSpinBox::down-button, QDoubleSpinBox::down-button {{ subcontrol-origin:border; subcontrol-position:bottom right;
        width:20px; border:none; border-left:1px solid {t.outline}; border-bottom-right-radius:6px; background:#2A2A40; }}
    QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
    QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {{ background:#383852; }}
    QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {{ image:url({up}); width:9px; height:9px; }}
    QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {{ image:url({arrow}); width:9px; height:9px; }}
    QToolTip {{ background:#24243A; color:{t.on_surface}; border:1px solid {t.outline}; padding:4px 6px; }}
    QCheckBox {{ color:{t.on_surface}; spacing:8px; }}
    QPushButton {{ background:#2A2A40; color:{t.on_surface}; border:1px solid {t.outline}; border-radius:6px;
        padding:5px 12px; }}
    QPushButton:hover {{ background:#33334C; }}
    QPushButton:default {{ background:{t.accent}; color:{t.on_accent}; border:none; }}
    QScrollArea {{ background:transparent; border:none; }}
    """


TIPS = {
    # General
    "start_win": "Launch F1DockTimers automatically when you sign in to Windows.",
    "auto_update": "Ask GitHub once a day whether a newer F1DockTimers exists. Sends no personal data.",
    "start_min": "On: start quietly in the tray. Off: open the panel at launch.",
    "widget_on": "A movable F1 panel that stays on your desktop.",
    "default_tab": "Tab the panel opens on. Live and Grid take over automatically during a session / after qualifying.",
    "time_fmt": "How session times are written: 14:00 or 02:00 PM.",
    "tz": "Zone for all session times. Automatic follows Windows. Click, then type to jump (e.g. \"Europe/Co\").",
    "sched_h": "How often the race calendar is checked for changes.",
    "stand_h": "How often driver/team standings and the last result are refreshed.",
    # Appearance
    "accent_btn": "Highlight colour: selected tab, countdown, favourites.",
    "font_fam": "Noto Sans is the original look; Monospace uses Cascadia Mono / Consolas.",
    "font_scale": "Text size in the panel and widget.",
    "ui_scale": "Size of the whole panel and widget, on top of Windows display scaling.",
    "pop_op": "How see-through the tray panel background is.",
    "wid_op": "How see-through the desktop widget background is (text stays solid).",
    "anim": "Circuit fade-in, start lights sequence and popup slide.",
    "on_top": "Keep the desktop widget above other windows.",
    "click_through": "Mouse clicks go through the widget. Turn this off again from the tray icon menu.",
    "locked": "Stop the widget from being moved or resized by accident.",
    "remember": "Restore the widget where you left it after a restart.",
    "dbl": "What a double-click on the desktop widget does.",
    # Taskbar
    "pill_on": "Small countdown next to the tray icons. Hides itself over fullscreen games.",
    "p_style": "Noctalia: dark box with accent text. Match taskbar: transparent with Windows colours. Custom: your colours.",
    "p_bg": "Background colour (Custom style).",
    "p_bg_op": "Background see-through amount (Custom style). 0 % = only text.",
    "p_text": "Colour of the first line (Custom style).",
    "p_text2": "Colour of the second line (Custom style).",
    "p_live": "Colour used while a session is live (Custom style).",
    "p_border": "Thin outline around the countdown box.",
    "p_border_c": "Outline colour (Custom style).",
    "p_radius": "Rounded corners. 0 = square box.",
    "p_width": "Fixed width, or Automatic to fit the text. You can also drag the box's left edge on the taskbar.",
    "p_hmode": "Automatic leaves a small gap, Full fills the taskbar height, Custom uses the pixel value.",
    "p_height": "Box height in pixels (Custom height).",
    "p_offset": "Distance from the tray icons. You can also drag the box along the taskbar.",
    "p_font": "Largest text size. With auto-fit the text shrinks when it would not fit.",
    "p_autofit": "Shrink the text so it always fits inside the box.",
    "p_bold": "Write the first line in bold.",
    "p_align": "Text alignment inside the box.",
    "p_lines": "One compact line or two lines.",
    "p_preset": "Ready-made layouts. Fills the four line fields below.",
    "p_top": "What the first line shows. Use the {tokens} listed below.",
    "p_bottom": "What the second line shows.",
    "p_ltop": "First line while a session is live.",
    "p_lbottom": "Second line while a session is live.",
    # Formula 1
    "cd_target": "Next race: big countdown always to the Grand Prix (LIGHTS OUT in 4d 9h).\n"
                 "Next session: counts to whatever is next (NEXT · PRACTICE 1 in 2d 6h), LIGHTS OUT for the race.\n"
                 "Also used by the tray tooltip. The taskbar countdown always shows the next session.",
    "fav_driver": "Shown in the accent colour in every table.",
    "fav_team": "Team shown in the accent colour in every table.",
    "team_colors": "Coloured bar beside each driver and team.",
    "n_race": "Windows notification before the race starts.",
    "n_quali": "Windows notification before qualifying starts.",
    "n_sprint": "Windows notification before the sprint starts.",
    "lead": "How many minutes before the session the notification appears.",
    # Live data
    "live_mode": "OpenF1 Free: no account; full results ~20–30 min after each session.\n"
                 "Free + F1 live stream: live order and gaps during sessions from F1's own (unofficial) stream, "
                 "full results from OpenF1 afterwards. May stop working at any time.\n"
                 "OpenF1 Sponsor: official live data during sessions; needs a paid OpenF1 account.",
    "user": "Your OpenF1 account e-mail (Sponsor plan only).",
    "pw": "Stored in Windows Credential Manager, never in files or logs.",
    "debug": "Show Grid and Live outside race weekends using the last session's data. For testing.",
}


class SettingsDialog(QDialog):
    applied = Signal()
    refresh_requested = Signal()
    clear_cache_requested = Signal()
    reopen_requested = Signal()
    check_updates_requested = Signal()

    def __init__(self, settings, store, theme, parent=None) -> None:
        super().__init__(parent)
        self.s = settings
        self.store = store
        self.t = theme
        self.setWindowTitle(f"{APP_TITLE} · Settings")
        self.setStyleSheet(stylesheet(theme))
        self.resize(560, 620)
        root = QVBoxLayout(self)
        tabs = QTabWidget()
        tabs.addTab(self._page(self._general()), "General")
        tabs.addTab(self._page(self._appearance()), "Appearance")
        tabs.addTab(self._page(self._taskbar()), "Taskbar")
        tabs.addTab(self._page(self._formula1()), "Formula 1")
        tabs.addTab(self._page(self._live()), "Live data")
        tabs.addTab(self._page(self._about()), "About")
        root.addWidget(tabs)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        bb.button(QDialogButtonBox.StandardButton.Save).setDefault(True)
        bb.accepted.connect(self._save)
        bb.rejected.connect(self.reject)
        root.addWidget(bb)
        self._apply_tips()

    def _apply_tips(self) -> None:
        """Hover help on the field and on its label."""
        from PySide6.QtWidgets import QFormLayout as _QFL
        forms = self.findChildren(_QFL)
        for name, tip in TIPS.items():
            wdg = getattr(self, name, None)
            if wdg is None:
                continue
            wdg.setToolTip(tip)
            for form in forms:
                lab = form.labelForField(wdg)
                if lab is not None:
                    lab.setToolTip(tip)

    # ── pages ───────────────────────────────────────────────────────────────

    def showEvent(self, e) -> None:
        super().showEvent(e)
        from PySide6.QtCore import QObject
        from PySide6.QtWidgets import QAbstractSpinBox

        class NoWheelUnlessFocused(QObject):
            def eventFilter(self, obj, ev):
                if ev.type() == ev.Type.Wheel and not obj.hasFocus():
                    ev.ignore()
                    return True
                return False

        if not hasattr(self, "_wheel_guard"):
            self._wheel_guard = NoWheelUnlessFocused(self)
            for wdg in self.findChildren(QComboBox) + self.findChildren(QAbstractSpinBox):
                wdg.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
                wdg.installEventFilter(self._wheel_guard)

    def _page(self, form: QWidget) -> QScrollArea:
        sa = QScrollArea()
        sa.setWidgetResizable(True)
        form.setObjectName("page")
        sa.setWidget(form)
        return sa

    def _hint(self, text: str) -> QLabel:
        lab = QLabel(text)
        lab.setWordWrap(True)
        lab.setProperty("hint", True)
        return lab

    def _check(self, label: str, value: bool, tip: str = "") -> QCheckBox:
        c = QCheckBox(label)
        c.setChecked(value)
        if tip:
            c.setToolTip(tip)
        return c

    def _combo(self, options: list[tuple[str, str]], value: str) -> QComboBox:
        c = QComboBox()
        for val, label in options:
            c.addItem(label, val)
        i = c.findData(value)
        c.setCurrentIndex(max(0, i))
        return c

    def _general(self) -> QWidget:
        s = self.s
        w = QWidget()
        f = QFormLayout(w)
        self.start_win = self._check("Start with Windows", s.start_with_windows)
        self.start_min = self._check("Start minimized to the tray (no popup at launch)", s.start_minimized)
        self.widget_on = self._check("Show the desktop widget", s.widget_enabled)
        f.addRow(self.start_win)
        f.addRow(self.start_min)
        f.addRow(self.widget_on)
        self.default_tab = self._combo([("schedule", "Schedule"), ("drivers", "Drivers"),
                                        ("constructors", "Teams")], s.default_tab)
        f.addRow("Default tab", self.default_tab)
        self.time_fmt = self._combo([("24h", "24-hour"), ("12h", "12-hour")], s.time_format)
        f.addRow("Time format", self.time_fmt)
        self.tz = QComboBox()
        self.tz.setMaxVisibleItems(16)
        self.tz.setToolTip("Click, then type to jump, e.g. \"Europe/Co\"")
        self.tz.addItem(f"Automatic (Windows: {system_zone_name()})", "auto")
        for z in zone_names():
            self.tz.addItem(z, z)
        i = self.tz.findData(s.timezone)
        self.tz.setCurrentIndex(max(0, i))
        f.addRow("Time zone", self.tz)
        self.sched_h = QDoubleSpinBox()
        self.sched_h.setRange(0.5, 48)
        self.sched_h.setSuffix(" h")
        self.sched_h.setValue(s.schedule_refresh_h)
        self.stand_h = QDoubleSpinBox()
        self.stand_h.setRange(0.5, 48)
        self.stand_h.setSuffix(" h")
        self.stand_h.setValue(s.standings_refresh_h)
        f.addRow("Schedule refresh", self.sched_h)
        f.addRow("Standings refresh", self.stand_h)
        row = QHBoxLayout()
        b1 = QPushButton("Refresh data now")
        b1.clicked.connect(self.refresh_requested)
        b2 = QPushButton("Clear cache")
        b2.clicked.connect(self._clear_cache)
        row.addWidget(b1)
        row.addWidget(b2)
        row.addStretch(1)
        f.addRow(row)
        self.auto_update = self._check("Check for updates automatically (once a day)", s.check_updates)
        f.addRow(self.auto_update)
        row = QHBoxLayout()
        b5 = QPushButton(f"Check for updates (you have {VERSION})")
        b5.clicked.connect(self.check_updates_requested)
        row.addWidget(b5)
        row.addStretch(1)
        f.addRow(row)
        row = QHBoxLayout()
        b3 = QPushButton("Export settings…")
        b3.setToolTip("Save all settings to a .json file (no passwords)")
        b3.clicked.connect(self._export)
        b4 = QPushButton("Import settings…")
        b4.setToolTip("Load settings from an exported .json file")
        b4.clicked.connect(self._import)
        row.addWidget(b3)
        row.addWidget(b4)
        row.addStretch(1)
        f.addRow(row)
        return w

    def _appearance(self) -> QWidget:
        s = self.s
        w = QWidget()
        f = QFormLayout(w)
        self.accent = s.accent
        self.accent_btn = QPushButton(s.accent)
        self.accent_btn.clicked.connect(self._pick_accent)
        reset = QPushButton("Original")
        reset.clicked.connect(lambda: self._set_accent("#BCC2FF"))
        row = QHBoxLayout()
        row.addWidget(self.accent_btn)
        row.addWidget(reset)
        row.addStretch(1)
        f.addRow("Accent colour", row)
        self._set_accent(s.accent)
        self.font_fam = self._combo([("Noto Sans", "Noto Sans (original)"), ("monospace", "Monospace")], s.font_family)
        f.addRow("Typeface", self.font_fam)
        self.font_scale = QSpinBox()
        self.font_scale.setRange(70, 160)
        self.font_scale.setSuffix(" %")
        self.font_scale.setValue(s.font_scale)
        f.addRow("Font size", self.font_scale)
        self.ui_scale = QSpinBox()
        self.ui_scale.setRange(70, 200)
        self.ui_scale.setSuffix(" %")
        self.ui_scale.setValue(s.ui_scale)
        f.addRow("Interface scale", self.ui_scale)
        self.pop_op = QSpinBox()
        self.pop_op.setRange(40, 100)
        self.pop_op.setSuffix(" %")
        self.pop_op.setValue(s.popup_opacity)
        f.addRow("Popup opacity", self.pop_op)
        self.wid_op = QSpinBox()
        self.wid_op.setRange(0, 100)
        self.wid_op.setSuffix(" %")
        self.wid_op.setValue(s.widget_opacity)
        f.addRow("Widget background opacity", self.wid_op)
        self.anim = self._check("Animations (circuit fade-in, start lights, popup slide)", s.animations)
        f.addRow(self.anim)
        f.addRow(QLabel("Desktop widget"))
        self.on_top = self._check("Always on top", s.widget_always_on_top)
        self.click_through = self._check("Click-through (mouse passes through; turn off from the tray menu)",
                                         s.widget_click_through)
        self.locked = self._check("Lock position and size (pin)", s.widget_locked)
        self.remember = self._check("Remember position and size", s.widget_remember_position)
        for c in (self.on_top, self.click_through, self.locked, self.remember):
            f.addRow(c)
        self.dbl = self._combo([("refresh", "Refresh data"), ("panel", "Open the tray panel"),
                                ("settings", "Open settings"), ("none", "Nothing")], s.widget_double_click)
        f.addRow("Double-click on widget", self.dbl)
        return w

    def _formula1(self) -> QWidget:
        s = self.s
        w = QWidget()
        f = QFormLayout(w)
        self.cd_target = self._combo([("race", "Next race"), ("session", "Next session")], s.countdown_target)
        f.addRow("Countdown target", self.cd_target)
        f.addRow(self._hint("Race = always to the Grand Prix. Session = to FP1, FP2, Quali … in turn. Hover for more."))
        self.fav_driver = QComboBox()
        self.fav_driver.setMaxVisibleItems(16)
        self.fav_driver.addItem("(none)", "")
        drivers = self.store.get("drivers").get("rows") or []
        for r in drivers:
            code = r.get("code", "")
            label = f"{code}  ·  {r.get('name', '')}" + (f"  ({r['team']})" if r.get("team") else "")
            self.fav_driver.addItem(label, code)
        i = self.fav_driver.findData(s.favorite_driver)
        if i < 0 and s.favorite_driver:
            self.fav_driver.addItem(s.favorite_driver, s.favorite_driver)
            i = self.fav_driver.count() - 1
        self.fav_driver.setCurrentIndex(max(0, i))
        f.addRow("Favourite driver", self.fav_driver)
        if not drivers:
            f.addRow(self._hint("The driver list fills in once the standings have loaded."))
        self.fav_team = QComboBox()
        self.fav_team.setMaxVisibleItems(16)
        self.fav_team.addItem("(none)", "")
        rows = self.store.get("constructors").get("rows") or []
        for r in rows:
            self.fav_team.addItem(r.get("name", ""), r.get("team_id", ""))
        i = self.fav_team.findData(s.favorite_team)
        if i < 0 and s.favorite_team:
            self.fav_team.addItem(s.favorite_team, s.favorite_team)
            i = self.fav_team.count() - 1
        self.fav_team.setCurrentIndex(max(0, i))
        f.addRow("Favourite team", self.fav_team)
        self.team_colors = self._check("Team colour chips", s.team_colors)
        f.addRow(self.team_colors)

        f.addRow(QLabel("Notifications"))
        self.n_race = self._check("Before the race", s.notify_race)
        self.n_quali = self._check("Before qualifying", s.notify_quali)
        self.n_sprint = self._check("Before the sprint", s.notify_sprint)
        for c in (self.n_race, self.n_quali, self.n_sprint):
            f.addRow(c)
        self.lead = QSpinBox()
        self.lead.setRange(0, 120)
        self.lead.setSuffix(" min")
        self.lead.setValue(s.notify_lead_min)
        f.addRow("Lead time", self.lead)

        f.addRow(QLabel("Session-time corrections"))
        f.addRow(self._hint("Only needed if a delay isn't picked up automatically. Resets next round."))
        self.offsets: dict[str, QSpinBox] = {}
        race = self.store.race
        offs = s.session_offsets if race and s.session_offsets.get("round_key") == round_key(race) else {}
        if race is None:
            f.addRow(self._hint("No upcoming race loaded."))
        else:
            for sess in race.raw_sessions:
                sp = QSpinBox()
                sp.setRange(-720, 720)
                sp.setSuffix(" min")
                sp.setValue(int(offs.get(sess.id, 0) or 0))
                self.offsets[sess.id] = sp
                f.addRow(SESSION_NAMES.get(sess.id, sess.id), sp)
        return w

    def _color_btn(self, value: str, on_change=None) -> QPushButton:
        b = QPushButton()

        def show(c):
            b.setProperty("color", QColor(c).name().upper())
            fg = "#000000" if QColor(c).lightnessF() > 0.55 else "#FFFFFF"
            b.setText(b.property("color"))
            b.setStyleSheet(f"background:{b.property('color')}; color:{fg}; border:1px solid {self.t.outline};")

        def pick():
            c = QColorDialog.getColor(QColor(b.property("color")), self, "Colour")
            if c.isValid():
                show(c.name())
                if on_change:
                    on_change()

        show(value)
        b.clicked.connect(pick)
        return b

    def _taskbar(self) -> QWidget:
        s = self.s
        w = QWidget()
        f = QFormLayout(w)
        upd = self._update_preview
        self.pill_on = self._check("Show the taskbar countdown beside the tray icons", s.taskbar_pill)
        f.addRow(self.pill_on)
        self.preview = PillPreview()
        f.addRow(self.preview)
        f.addRow(self._hint("On the taskbar: drag to move · drag the left edge to resize · double-click it to reset."))

        self.p_style = self._combo([("noctalia", "Noctalia (dark panel, accent text)"),
                                    ("taskbar", "Match my Windows taskbar (transparent, system colours)"),
                                    ("custom", "Custom colours")], s.pill_style)
        f.addRow("Style", self.p_style)
        self.p_bg = self._color_btn(s.pill_bg, upd)
        self.p_bg_op = QSpinBox()
        self.p_bg_op.setRange(0, 100)
        self.p_bg_op.setSuffix(" %")
        self.p_bg_op.setValue(s.pill_bg_opacity)
        self.p_text = self._color_btn(s.pill_text, upd)
        self.p_text2 = self._color_btn(s.pill_text2, upd)
        self.p_live = self._color_btn(s.pill_live_color, upd)
        self.p_border = self._check("Border", s.pill_border)
        self.p_border_c = self._color_btn(s.pill_border_color, upd)
        self.custom_rows = [("Background", self.p_bg), ("Background opacity", self.p_bg_op),
                            ("First line colour", self.p_text), ("Second line colour", self.p_text2),
                            ("Live highlight", self.p_live), ("Border colour", self.p_border_c)]
        for label, wdg in self.custom_rows:
            f.addRow(label, wdg)
        f.addRow(self.p_border)

        self.p_radius = QSpinBox()
        self.p_radius.setRange(0, 30)
        self.p_radius.setSuffix(" px")
        self.p_radius.setSpecialValueText("0 (square box)")
        self.p_radius.setValue(s.pill_radius)
        f.addRow("Corner radius", self.p_radius)
        self.p_width = QSpinBox()
        self.p_width.setRange(0, 900)
        self.p_width.setSuffix(" px")
        self.p_width.setSpecialValueText("Automatic")
        self.p_width.setValue(s.pill_width)
        f.addRow("Width", self.p_width)
        self.p_hmode = self._combo([("auto", "Automatic"), ("full", "Full taskbar height"), ("custom", "Custom")],
                                   "full" if s.pill_height == -1 else ("custom" if s.pill_height > 0 else "auto"))
        self.p_height = QSpinBox()
        self.p_height.setRange(16, 80)
        self.p_height.setSuffix(" px")
        self.p_height.setValue(s.pill_height if s.pill_height > 0 else 28)
        row = QHBoxLayout()
        row.addWidget(self.p_hmode)
        row.addWidget(self.p_height)
        f.addRow("Height", row)
        self.p_offset = QSpinBox()
        self.p_offset.setRange(0, 1600)
        self.p_offset.setSuffix(" px left of the tray")
        self.p_offset.setValue(s.taskbar_pill_offset)
        f.addRow("Position", self.p_offset)

        self.p_font = QDoubleSpinBox()
        self.p_font.setRange(7, 28)
        self.p_font.setSingleStep(0.5)
        self.p_font.setSuffix(" px")
        self.p_font.setValue(s.pill_font_px)
        f.addRow("Text size (max)", self.p_font)
        self.p_autofit = self._check("Auto-fit text to the pill size", s.pill_autofit)
        self.p_bold = self._check("First line bold", s.pill_bold_top)
        f.addRow(self.p_autofit)
        f.addRow(self.p_bold)
        self.p_align = self._combo([("left", "Left"), ("center", "Centre")], s.pill_align)
        f.addRow("Alignment", self.p_align)
        self.p_lines = self._combo([("2", "Two lines"), ("1", "One line")], str(s.pill_lines))
        f.addRow("Lines", self.p_lines)

        self.p_preset = QComboBox()
        self.p_preset.addItem("(custom)", None)
        for i, pr in enumerate(PRESETS):
            self.p_preset.addItem(pr[0], i)
        f.addRow("Layout preset", self.p_preset)
        self.p_top = QLineEdit(s.pill_top)
        self.p_bottom = QLineEdit(s.pill_bottom)
        self.p_ltop = QLineEdit(s.pill_live_top)
        self.p_lbottom = QLineEdit(s.pill_live_bottom)
        f.addRow("First line", self.p_top)
        f.addRow("Second line", self.p_bottom)
        f.addRow("First line (live)", self.p_ltop)
        f.addRow("Second line (live)", self.p_lbottom)
        tokens = self._hint("Tokens: " + " ".join("{" + k + "}" for k in TOKENS) + "  (hover for examples)")
        tokens.setToolTip("\n".join("{" + k + "}  " + v for k, v in TOKENS.items()))
        f.addRow(tokens)

        def preset(i):
            idx = self.p_preset.itemData(i)
            if idx is None:
                return
            _, a, b, c, d = PRESETS[idx]
            for e, v in ((self.p_top, a), (self.p_bottom, b), (self.p_ltop, c), (self.p_lbottom, d)):
                e.setText(v)
            self.p_lines.setCurrentIndex(self.p_lines.findData("2" if b else "1"))

        self.p_preset.currentIndexChanged.connect(preset)
        for wdg in (self.p_style, self.p_hmode, self.p_align, self.p_lines):
            wdg.currentIndexChanged.connect(upd)
        for wdg in (self.p_bg_op, self.p_radius, self.p_width, self.p_height, self.p_font):
            wdg.valueChanged.connect(upd)
        for wdg in (self.p_autofit, self.p_bold, self.p_border):
            wdg.toggled.connect(upd)
        for wdg in (self.p_top, self.p_bottom, self.p_ltop, self.p_lbottom):
            wdg.textChanged.connect(upd)
        upd()
        return w

    def _pill_values(self) -> dict:
        hmode = self.p_hmode.currentData()
        return dict(
            taskbar_pill=self.pill_on.isChecked(), pill_style=self.p_style.currentData(),
            pill_bg=self.p_bg.property("color"), pill_bg_opacity=self.p_bg_op.value(),
            pill_text=self.p_text.property("color"), pill_text2=self.p_text2.property("color"),
            pill_live_color=self.p_live.property("color"), pill_border=self.p_border.isChecked(),
            pill_border_color=self.p_border_c.property("color"), pill_radius=self.p_radius.value(),
            pill_width=self.p_width.value(),
            pill_height=-1 if hmode == "full" else (self.p_height.value() if hmode == "custom" else 0),
            taskbar_pill_offset=self.p_offset.value(), pill_font_px=self.p_font.value(),
            pill_autofit=self.p_autofit.isChecked(), pill_bold_top=self.p_bold.isChecked(),
            pill_align=self.p_align.currentData(), pill_lines=int(self.p_lines.currentData()),
            pill_top=self.p_top.text(), pill_bottom=self.p_bottom.text(),
            pill_live_top=self.p_ltop.text(), pill_live_bottom=self.p_lbottom.text(),
        )

    def _update_preview(self, *_a) -> None:
        if not hasattr(self, "p_lbottom"):
            return
        custom = self.p_style.currentData() == "custom"
        for _label, wdg in self.custom_rows:
            wdg.setEnabled(custom)
        self.p_height.setEnabled(self.p_hmode.currentData() == "custom")
        tmp = dataclasses.replace(self.s, **self._pill_values())
        lines = pill_state(self.store.race, self.store.get("live"), tmp, now_sec(), zone_for(tmp.timezone))
        style = PillStyle.resolve(tmp, self.t)
        h = tmp.pill_height if tmp.pill_height > 0 else (30 if tmp.pill_height == -1 else 24)
        w = tmp.pill_width or 190
        self.preview.set_preview(style, lines, (w, h), tmp.pill_font_px, tmp.pill_autofit, QColor("#1C1C1C"))

    def _live(self) -> QWidget:
        s = self.s
        w = QWidget()
        f = QFormLayout(w)
        mode = "sponsor" if s.openf1_plan == "sponsor" and s.live_source == "openf1" else (
            "combo" if s.live_source == "f1_feed" else "free")
        self.live_mode = self._combo([
            ("free", "OpenF1 Free · results after each session"),
            ("combo", "OpenF1 Free + F1 live stream (experimental)"),
            ("sponsor", "OpenF1 Sponsor · live during sessions (account)")], mode)
        f.addRow("Live data", self.live_mode)
        self.live_hint = self._hint("")
        f.addRow(self.live_hint)
        self.user = QLineEdit(s.openf1_username)
        self.user.setPlaceholderText("you@example.com")
        f.addRow("OpenF1 username", self.user)
        self.pw = QLineEdit()
        self.pw.setEchoMode(QLineEdit.EchoMode.Password)
        has = bool(s.openf1_username and credentials.get_password(s.openf1_username))
        self.pw.setPlaceholderText("•••••••• (stored)" if has else "password")
        f.addRow("OpenF1 password", self.pw)
        forget = QPushButton("Forget stored password")
        forget.setToolTip("Remove the password from Windows Credential Manager.")
        forget.clicked.connect(self._forget)
        f.addRow(forget)
        hints = {
            "free": "No account needed. Live tab shows final results ~20–30 min after each session.",
            "combo": "Live during sessions via F1's unofficial stream, OpenF1 results after. May break anytime.",
            "sponsor": "Official live timing during sessions. Needs a paid OpenF1 account below.",
        }

        def mode_changed(*_a):
            m = self.live_mode.currentData()
            for wdg in (self.user, self.pw, forget):
                wdg.setEnabled(m == "sponsor")
            self.live_hint.setText(hints[m])

        self.live_mode.currentIndexChanged.connect(mode_changed)
        mode_changed()
        self.debug = self._check("Debug: force Grid and Live tabs", s.debug_force_tabs)
        f.addRow(self.debug)
        return w

    def _about(self) -> QWidget:
        w = QWidget()
        v = QVBoxLayout(w)
        lab = QLabel(
            f"<b>{APP_TITLE}</b> {VERSION}<br>"
            "F1 schedule, countdowns, live timing and standings for Windows.<br><br>"
            "Inspired by the <a href='https://noctalia.dev/plugins/community/f1'>Noctalia F1 plugin</a> "
            "(gcap0n1, MIT).<br>"
            "Data: Jolpica-F1 · OpenF1. Circuits: bacinger/f1-circuits.<br><br>"
            "<span style='color:#8F8F9E'>Unofficial · not affiliated with Formula 1. "
            "Full credits in THIRD_PARTY_NOTICES.md.</span>")
        lab.setWordWrap(True)
        lab.setTextFormat(Qt.TextFormat.RichText)
        lab.setOpenExternalLinks(True)
        v.addWidget(lab)
        v.addStretch(1)
        return w

    # ── actions ─────────────────────────────────────────────────────────────

    def _set_accent(self, c: str) -> None:
        self.accent = QColor(c).name().upper()
        self.accent_btn.setText(self.accent)
        fg = "#262C67" if QColor(c).lightnessF() > 0.55 else "#FFFFFF"
        self.accent_btn.setStyleSheet(f"background:{self.accent}; color:{fg}; border:none;")

    def _pick_accent(self) -> None:
        c = QColorDialog.getColor(QColor(self.accent), self, "Accent colour")
        if c.isValid():
            self._set_accent(c.name())

    def _forget(self) -> None:
        credentials.delete_password(self.user.text().strip() or self.s.openf1_username)
        self.pw.clear()
        self.pw.setPlaceholderText("password")

    def _export(self) -> None:
        import os
        start = os.path.join(os.path.expanduser("~"), "Documents", "F1DockTimers-settings.json")
        path, _ = QFileDialog.getSaveFileName(self, "Export settings", start, "Settings (*.json)")
        if not path:
            return
        try:
            self.s.export_to(path)
        except OSError as e:
            QMessageBox.warning(self, "Export settings", f"Could not write the file:\n{e}")
            return
        QMessageBox.information(self, "Export settings",
                                "Settings exported. Passwords are not included; window positions stay on this PC.")

    def _import(self) -> None:
        import os
        start = os.path.join(os.path.expanduser("~"), "Documents")
        path, _ = QFileDialog.getOpenFileName(self, "Import settings", start, "Settings (*.json)")
        if not path:
            return
        try:
            changed = self.s.import_from(path)
        except ValueError as e:
            QMessageBox.warning(self, "Import settings", str(e))
            return
        self.s.save()
        self.applied.emit()
        QMessageBox.information(self, "Import settings",
                                f"Imported and applied ({len(changed)} setting{'s' if len(changed) != 1 else ''} "
                                "changed). Settings will reopen with the new values.")
        self.accept()
        self.reopen_requested.emit()

    def _clear_cache(self) -> None:
        self.clear_cache_requested.emit()
        QMessageBox.information(self, "Cache", "Cache cleared. Data will be fetched again.")

    def _save(self) -> None:
        s = self.s
        s.start_with_windows = self.start_win.isChecked()
        s.start_minimized = self.start_min.isChecked()
        s.check_updates = self.auto_update.isChecked()
        s.widget_enabled = self.widget_on.isChecked()
        for k, v in self._pill_values().items():
            setattr(s, k, v)
        s.default_tab = self.default_tab.currentData()
        s.time_format = self.time_fmt.currentData()
        s.timezone = self.tz.currentData() or "auto"
        s.schedule_refresh_h = self.sched_h.value()
        s.standings_refresh_h = self.stand_h.value()
        s.accent = self.accent
        s.font_family = self.font_fam.currentData()
        s.font_scale = self.font_scale.value()
        s.ui_scale = self.ui_scale.value()
        s.popup_opacity = self.pop_op.value()
        s.widget_opacity = self.wid_op.value()
        s.animations = self.anim.isChecked()
        s.widget_always_on_top = self.on_top.isChecked()
        s.widget_click_through = self.click_through.isChecked()
        s.widget_locked = self.locked.isChecked()
        s.widget_remember_position = self.remember.isChecked()
        s.widget_double_click = self.dbl.currentData()
        s.countdown_target = self.cd_target.currentData()
        s.favorite_driver = self.fav_driver.currentData() or ""
        s.favorite_team = self.fav_team.currentData() or ""
        s.team_colors = self.team_colors.isChecked()
        s.notify_race = self.n_race.isChecked()
        s.notify_quali = self.n_quali.isChecked()
        s.notify_sprint = self.n_sprint.isChecked()
        s.notify_lead_min = self.lead.value()
        race = self.store.race
        if race is not None and self.offsets:
            offs = {k: sp.value() for k, sp in self.offsets.items() if sp.value()}
            s.session_offsets = {"round_key": round_key(race), **offs} if offs else {}
        m = self.live_mode.currentData()
        s.live_source = "f1_feed" if m == "combo" else "openf1"
        s.openf1_plan = "sponsor" if m == "sponsor" else "free"
        user = self.user.text().strip()
        if user != s.openf1_username and s.openf1_username and not self.pw.text():
            credentials.delete_password(s.openf1_username)
        s.openf1_username = user
        if user and self.pw.text():
            if not credentials.set_password(user, self.pw.text()):
                QMessageBox.warning(self, "OpenF1", "Could not store the password in Windows Credential Manager.")
        self.pw.clear()
        s.debug_force_tabs = self.debug.isChecked()
        s.clamp()
        s.save()
        self.applied.emit()
        self.accept()
