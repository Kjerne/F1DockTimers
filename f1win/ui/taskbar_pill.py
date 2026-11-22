"""Taskbar countdown pill (the Noctalia bar widget, Windows style).

Windows 11 has no supported way to put custom content inside the taskbar, so
this is a small frameless, topmost, non-activating window placed over the
empty taskbar area just left of the notification icons. Every 2 s it checks
the real window stack under itself and hides when anything (a fullscreen or
borderless game, a video) covers the taskbar there; otherwise it re-asserts
topmost, because clicking the taskbar raises the taskbar.

Shows the session running now, otherwise the next one:
FP1 -> FP2 -> FP3 -> Quali -> GP (SQ / Sprint on sprint weekends).

Everything visible is configurable: style preset (Noctalia, match the
Windows taskbar, custom colours), corner radius (0 = square box), size (auto,
drag the left edge, or exact pixels), auto-fitting text, one or two lines and
what each line says (templates with {tokens}).
"""
from __future__ import annotations

import string
import sys
from dataclasses import dataclass

from PySide6.QtCore import QPoint, QRect, QRectF, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QCursor, QFont, QFontMetricsF, QGuiApplication, QImage, QPainter, QPainterPath
from PySide6.QtWidgets import QApplication, QToolTip, QWidget

from ..core.sessions import SESSION_NAMES, current_session, live_active, next_session
from ..core.teams import friendly_flag
from ..core.timeutil import MONTHS, fmt_clock, fmt_datetime, fmt_long, fmt_short, now_sec, to_local
from ..services import taskbar_embed, winshell
from ..i18n import T

SHORT = {"fp1": "FP1", "fp2": "FP2", "fp3": "FP3", "sprint_quali": "SQ", "sprint": "Sprint",
         "quali": "Quali", "race": "GP"}

TOKENS = {
    "round": T('Round, e.g. R21'),
    "race": T('Short race name, e.g. Las Vegas'),
    "race_full": T('Las Vegas Grand Prix'),
    "session": T('FP1 / FP2 / Q2 / Sprint / GP'),
    "session_full": T('Practice 1'),
    "time": T('Local start, e.g. Fri 01:30'),
    "date": T('20 Nov'),
    "countdown": T('in 2d 6h 19m'),
    "countdown_short": "2d",
    "city": T('Las Vegas'),
    "country": T('USA'),
    "circuit": T('Las Vegas Strip Street Circuit'),
    "live": T('LIVE, or LIVE · 12:34 left'),
    "flag": T('Track flag while live'),
    "leader": T('Leader while live'),
    "temp": T('Air temperature: live at the track, else the forecast, e.g. 18°'),
    "rain": T('Chance of rain during the session, e.g. 20%'),
    "sky": T('Forecast sky, e.g. Partly cloudy'),
    "wx": T('Temperature and rain chance, e.g. 18° · 20%'),
    "track_temp": T('Track temperature while live, e.g. 31°'),
}

# (label, top, bottom, live top, live bottom)
PRESETS = [
    ("Round, race and session", "{round} · {race} · {session}", "{time} · {countdown}",
     "{round} · {race} · ● {session}", "{live}"),
    (T('Session and countdown'), "{session} · {time}", "{countdown}", "● {session}", "{live}"),
    (T('Race and countdown'), "{round} · {race}", "{session} {countdown}", "● {session} {race}", "{live}"),
    (T('Compact (one line)'), "{session} {countdown_short}", "", "● {session} LIVE", ""),
]

DEFAULT_TOP, DEFAULT_BOTTOM, DEFAULT_LIVE_TOP, DEFAULT_LIVE_BOTTOM = PRESETS[0][1:]


# ── content ──────────────────────────────────────────────────────────────────

def short_race(race) -> str:
    """"Las Vegas" from "Las Vegas Grand Prix"."""
    name = (race.name or "").replace(" Grand Prix", "").strip() or race.locality
    return name if len(name) <= 18 else name[:17] + "…"


def pill_fields(race, live: dict | None, settings, now: int, zone=None) -> tuple[dict, bool]:
    """Every template token's value, and whether a session is running. Pure."""
    f = {k: "" for k in TOKENS}
    if race is None:
        f.update(race="F1", session="F1", countdown=T('No upcoming race'), countdown_short="–")
        return f, False
    f.update(round=f"R{race.round}", race=short_race(race), race_full=race.name, city=race.locality,
             country=race.country, circuit=race.circuit_name)
    sessions = race.sessions
    live = live if isinstance(live, dict) else {}
    cur = current_session(sessions, now)
    if cur is not None:
        name = SHORT.get(cur.id, cur.id)
        sess = live.get("session") if isinstance(live.get("session"), dict) else {}
        part = int(sess.get("part") or 0)
        if cur.id in ("quali", "sprint_quali") and part:
            name = f"{'SQ' if cur.id == 'sprint_quali' else 'Q'}{part}"
        f.update(session=name, session_full=SESSION_NAMES.get(cur.id, cur.id),
                 time=fmt_datetime(cur.ts, zone, settings.time_format == "12h"), live="LIVE",
                 countdown=T('live now'), countdown_short="LIVE")
        if live_active(live, sessions, now, settings.debug_force_tabs):
            clock = live.get("clock")
            if isinstance(clock, dict):
                left = clock["remaining"] - ((now - clock["at"]) if clock.get("running") else 0)
                f["live"] = f"LIVE · {fmt_clock(left)} left"
            f["flag"] = friendly_flag(live.get("flag"))
            rows = live.get("rows") or []
            # Spoiler-free: no leader unless results were revealed after this session started.
            hidden = getattr(settings, "spoiler_free", False) and \
                float(getattr(settings, "spoiler_revealed_at", 0) or 0) < cur.ts
            f["leader"] = rows[0].get("code", "") if rows and not hidden else ""
        return f, True
    nxt = next_session(sessions, now)
    if nxt is None:
        f.update(session="—", countdown=T('Weekend over'), countdown_short=T('done'))
        return f, False
    dt = to_local(nxt.ts, zone)
    f.update(session=SHORT.get(nxt.id, nxt.id), session_full=SESSION_NAMES.get(nxt.id, nxt.id),
             time=fmt_datetime(nxt.ts, zone, settings.time_format == "12h"),
             date=f"{dt.day} {MONTHS[dt.month - 1]}", countdown=fmt_long(nxt.ts - now),
             countdown_short=fmt_short(nxt.ts - now))
    return f, False


class _Safe(dict):
    def __missing__(self, key):
        return "{" + key + "}"


def render_template(tpl: str, fields: dict) -> str:
    """Fill {tokens}; unknown tokens stay visible, format specs are ignored (no
    attribute/index access), and doubled separators from empty tokens collapse."""
    out = []
    for literal, name, _spec, _conv in string.Formatter().parse(tpl or ""):
        out.append(literal)
        if name is not None:
            key = name.split(".")[0].split("[")[0]
            out.append(str(fields[key]) if key in fields else "{" + name + "}")
    text = "".join(out)
    while "·  ·" in text or " ·  " in text:
        text = text.replace("·  ·", "·").replace(" ·  ", " · ")
    return " ".join(text.split()).strip(" ·")


WEATHER_TOKENS = ("{temp}", "{rain}", "{sky}", "{wx}", "{track_temp}")


def weather_fields(race, live: dict | None, forecast: dict | None, settings, now: int) -> dict:
    """{temp} {rain} {sky} {wx} {track_temp} for the current or next session. Pure."""
    from ..core import weather as wx
    from ..core.sessions import DURATION_MIN
    out = {"temp": "", "rain": "", "sky": "", "wx": "", "track_temp": ""}
    if race is None:
        return out
    units = getattr(settings, "units", "metric")
    s = current_session(race.sessions, now) or next_session(race.sessions, now)
    if s is not None and forecast and forecast.get("round") == f"{race.season}-{race.round}":
        win = wx.forecast_window(forecast, s.ts, s.ts + DURATION_MIN.get(s.id, 60) * 60)
        if win:
            out.update(temp=wx.temp(win["temp"], units), rain=f"{round(win.get('rain_prob') or 0)}%",
                       sky=wx.condition(win.get("code")), wx=wx.short(win, units))
    lw = (live or {}).get("weather") if isinstance(live, dict) else None
    if lw and current_session(race.sessions, now) is not None:
        if lw.get("air") is not None:
            out["temp"] = wx.temp(lw["air"], units)
            out["wx"] = out["temp"] + (f" · {out['rain']}" if out["rain"] else "")
        if lw.get("track") is not None:
            out["track_temp"] = wx.temp(lw["track"], units)
        if lw.get("rain"):
            out["sky"] = T("Raining")
    return out


def pill_state(race, live, settings, now: int, zone=None, forecast: dict | None = None) -> tuple[str, str, bool]:
    """(top line, bottom line, live?) after applying the user's templates. Pure."""
    fields, is_live = pill_fields(race, live, settings, now, zone)
    templates = (settings.pill_top, settings.pill_bottom, settings.pill_live_top, settings.pill_live_bottom)
    if any(tok in tpl for tpl in templates for tok in WEATHER_TOKENS):   # only when the user shows weather
        fields.update(weather_fields(race, live, forecast, settings, now))
    if race is None:
        return "F1", T('No upcoming race'), False
    if is_live:
        top, bottom = settings.pill_live_top, settings.pill_live_bottom
    else:
        top, bottom = settings.pill_top, settings.pill_bottom
    one = settings.pill_lines == 1
    return render_template(top, fields), "" if one else render_template(bottom, fields), is_live


# ── style ────────────────────────────────────────────────────────────────────

def windows_theme() -> dict:
    """Taskbar look from the registry: dark/light and the accent colour."""
    out = {"dark": True, "accent": None}
    if sys.platform != "win32":
        return out
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                            r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize") as k:
            out["dark"] = winreg.QueryValueEx(k, "SystemUsesLightTheme")[0] == 0
    except OSError:
        pass
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\DWM") as k:
            abgr = winreg.QueryValueEx(k, "AccentColor")[0]
            out["accent"] = QColor(abgr & 0xFF, (abgr >> 8) & 0xFF, (abgr >> 16) & 0xFF).name()
    except OSError:
        pass
    return out


@dataclass
class PillStyle:
    bg: QColor
    border: QColor | None
    top: QColor
    bottom: QColor
    live: QColor
    hover: QColor
    radius: float
    bold_top: bool
    align: Qt.AlignmentFlag
    family: str

    @staticmethod
    def resolve(s, theme) -> "PillStyle":
        align = Qt.AlignmentFlag.AlignHCenter if s.pill_align == "center" else Qt.AlignmentFlag.AlignLeft
        radius = max(0, s.pill_radius)
        if s.pill_style == "taskbar":
            win = windows_theme()
            fg = QColor("#FFFFFF" if win["dark"] else "#000000")
            sub = QColor("#C5C5C5" if win["dark"] else "#3A3A3A")
            bg = QColor(0, 0, 0)
            bg.setAlphaF(0.004)                         # invisible, but still clickable
            hover = QColor(255, 255, 255) if win["dark"] else QColor(0, 0, 0)
            hover.setAlphaF(0.08)
            live = QColor(win["accent"] or theme.accent)
            return PillStyle(bg, None, fg, sub, live, hover, radius, s.pill_bold_top, align, theme.family)
        if s.pill_style == "custom":
            bg = QColor(s.pill_bg)
            bg.setAlphaF(max(0.004, s.pill_bg_opacity / 100.0))
            border = QColor(s.pill_border_color) if s.pill_border else None
            hover = QColor(s.pill_text)
            hover.setAlphaF(0.08)
            return PillStyle(bg, border, QColor(s.pill_text), QColor(s.pill_text2), QColor(s.pill_live_color),
                             hover, radius, s.pill_bold_top, align, theme.family)
        bg = QColor(theme.surface)
        bg.setAlphaF(0.90)
        hover = QColor(theme.on_surface)
        hover.setAlphaF(0.07)
        border = QColor(theme.outline) if s.pill_border else None
        return PillStyle(bg, border, QColor(theme.on_surface), QColor(theme.accent), QColor(theme.accent),
                         hover, radius, s.pill_bold_top, align, theme.family)


def fit_font(family: str, lines: list[str], width: float, height: float, max_px: float,
             autofit: bool, bold_first: bool) -> list[QFont]:
    """Largest font sizes (<= max_px) at which every line fits the box."""
    def fonts(px):
        out = []
        for i, _ in enumerate(lines):
            f = QFont(family)
            f.setPixelSize(max(6, round(px)))
            f.setBold(bold_first and i == 0)
            f.setHintingPreference(QFont.HintingPreference.PreferNoHinting)
            out.append(f)
        return out

    px = max_px
    if not autofit:
        return fonts(px)
    while px > 7:
        fs = fonts(px)
        tall = sum(QFontMetricsF(f).height() for f in fs) * 0.92
        wide = max((QFontMetricsF(f).horizontalAdvance(t) for f, t in zip(fs, lines)), default=0)
        if tall <= height and wide <= width:
            return fs
        px -= 0.5
    return fonts(7)


def paint_pill(p: QPainter, rect: QRectF, style: PillStyle, top: str, bottom: str, live: bool,
               hover: bool, max_px: float, autofit: bool, pad: float) -> None:
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    r = rect.adjusted(0.5, 0.5, -0.5, -0.5)
    path = QPainterPath()
    rad = min(style.radius, r.height() / 2)
    if rad > 0:
        path.addRoundedRect(r, rad, rad)
    else:
        path.addRect(r)
    p.fillPath(path, style.bg)
    if hover:
        p.fillPath(path, style.hover)
    border = style.live if live else style.border
    if border is not None:
        p.setPen(border)
        p.drawPath(path)
    lines = [top] + ([bottom] if bottom else [])
    inner_w, inner_h = rect.width() - 2 * pad, rect.height() - 2
    fonts = fit_font(style.family, lines, inner_w, inner_h, max_px, autofit, style.bold_top)
    heights = [QFontMetricsF(f).height() * (0.92 if len(lines) > 1 else 1.0) for f in fonts]
    y = rect.top() + (rect.height() - sum(heights)) / 2
    colors = [style.live if live else style.top, style.top if live else style.bottom]
    for i, (text, f, h) in enumerate(zip(lines, fonts, heights)):
        p.setFont(f)
        p.setPen(colors[i])
        fm = QFontMetricsF(f)
        shown = fm.elidedText(text, Qt.TextElideMode.ElideRight, inner_w)
        p.drawText(QRectF(rect.left() + pad, y, inner_w, h),
                   int(style.align | Qt.AlignmentFlag.AlignVCenter), shown)
        y += h


# ── window ───────────────────────────────────────────────────────────────────

EDGE = 6


class TaskbarPill(QWidget):
    clicked = Signal()
    menu_requested = Signal(QPoint)
    offset_changed = Signal(int)

    def __init__(self, ctx, settings, tray_rect_fn, parent=None) -> None:
        super().__init__(parent, Qt.WindowType.FramelessWindowHint | Qt.WindowType.Tool
                         | Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.NoDropShadowWindowHint
                         | Qt.WindowType.WindowDoesNotAcceptFocus)
        self.ctx = ctx
        self.settings = settings
        self.tray_rect_fn = tray_rect_fn
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setMouseTracking(True)
        self.setAccessibleName(T('Formula 1 countdown'))
        self.top, self.bottom, self.live = "F1", "", False
        self.style_ = PillStyle.resolve(settings, ctx.theme)
        self._hover = False
        self._press: QPoint | None = None
        self._mode = ""            # "" | move | resize
        self._press_offset = 0
        self._press_width = 0
        self._moved = False
        self._fs_hidden = False
        self.guard = QTimer(self)
        self.guard.setInterval(2000)
        self.guard.timeout.connect(self._guard)
        # Clicking the taskbar lifts it over us; re-raise quickly (cheap SetWindowPos only).
        self.raiser = QTimer(self)
        self.raiser.setInterval(2000)           # safety net; taskbar clicks are caught by the hook
        self.raiser.timeout.connect(self._raise)
        self._fg_watch: winshell.ForegroundWatch | None = None
        self._embed: taskbar_embed.EmbeddedHost | None = None   # set while living inside the taskbar
        self._embed_key = None
        self._embed_failed = False

    # ── state ───────────────────────────────────────────────────────────────

    def start(self) -> None:
        self._set_mode()
        self.restyle()
        self.tick(now_sec(), force=True)
        self.place()
        if self._embed is None:
            self.show()
            self.raiser.start()
        self.guard.start()
        if self._fg_watch is None:
            self._fg_watch = winshell.ForegroundWatch(self._foreground_changed)
        QTimer.singleShot(50, self._guard)

    def stop(self) -> None:
        self.guard.stop()
        self.raiser.stop()
        self._drop_embed()
        if self._fg_watch is not None:
            self._fg_watch.close()
            self._fg_watch = None
        self.hide()

    def _foreground_changed(self) -> None:
        # The taskbar (or its flyout) raises itself right away and again after its animation.
        # _guard also hides us at once if the new foreground window is a fullscreen game.
        if self._embed is not None:
            QTimer.singleShot(150, self._guard)     # inside the taskbar: one look is enough
            return
        for ms in (0, 150, 500):
            QTimer.singleShot(ms, self._guard)

    @staticmethod
    def _menu_open() -> bool:
        # Never jump over an open menu (our tray/pill menu or another app's).
        return QApplication.activePopupWidget() is not None or winshell.menu_is_open()

    def _raise(self) -> None:
        if self._embed is not None:
            self._embed.keep_on_top()
            return
        if self.isVisible() and self._press is None and not self._menu_open():
            winshell.keep_topmost(int(self.winId()))

    # ── embedded mode (a child window inside the taskbar) ─────────────────────

    def is_shown(self) -> bool:
        return self._embed is not None or self.isVisible()

    @property
    def embedded(self) -> bool:
        return self._embed is not None

    def _set_mode(self) -> None:
        """Switch between floating and embedded to match the setting (falls back to floating)."""
        want = bool(getattr(self.settings, "pill_embed", False)) and winshell.IS_WIN
        if want and self._embed is None and not self._embed_failed:
            host = taskbar_embed.EmbeddedHost({
                "on_press": self._e_press, "on_move": self._e_move, "on_release": self._e_release,
                "on_double": self._e_double, "on_right": self._e_right, "on_hover": self._e_hover,
                "cursor": lambda x: "size" if self._on_edge(QPoint(round(x / self._dpr()), 0)) else "hand"})
            if host.create():
                self._embed = host
                self._embed_key = None
                self.guard.setInterval(3000)       # follows other widgets' width changes
                self.raiser.stop()
                self._fs_hidden = False
                if self.isVisible():
                    self.hide()
            else:
                self._embed_failed = True          # don't retry every 2 s; floating mode instead
        elif not want and self._embed is not None:
            self._drop_embed()
            self.guard.setInterval(2000)
            if self.settings.taskbar_pill and self.guard.isActive():
                self.show()
                self.raiser.start()

    def _drop_embed(self) -> None:
        if self._embed is not None:
            winshell.release_task_buttons(self._embed.bar, winshell._rect(self._embed.hwnd))
            self._embed.destroy()
            self._embed = None
        QToolTip.hideText()

    def _dpr(self) -> float:
        sc = QGuiApplication.primaryScreen()
        return sc.devicePixelRatio() if sc else 1.0

    def _refresh(self) -> None:
        if self._embed is not None:
            self._render_embed()
        else:
            self.update()

    def _render_embed(self, force: bool = False) -> None:
        host = self._embed
        if host is None:
            return
        g = self.geometry()
        sc = QGuiApplication.primaryScreen()
        dpr, sg = sc.devicePixelRatio(), sc.geometry()
        key = (g.x(), g.y(), g.width(), g.height(), dpr, self.top, self.bottom, self.live, self._hover,
               id(self.style_))
        if not force and key == self._embed_key:
            return
        self._embed_key = key
        img = QImage(max(1, round(g.width() * dpr)), max(1, round(g.height() * dpr)),
                     QImage.Format.Format_ARGB32_Premultiplied)
        img.setDevicePixelRatio(dpr)
        img.fill(Qt.GlobalColor.transparent)
        p = QPainter(img)
        paint_pill(p, QRectF(0, 0, g.width(), g.height()), self.style_, self.top, self.bottom, self.live,
                   self._hover, self._max_px(), self.settings.pill_autofit, self._pad())
        p.end()
        x, y = round(sg.x() + (g.x() - sg.x()) * dpr), round(sg.y() + (g.y() - sg.y()) * dpr)
        host.show_image(img, x, y)
        self._fit_buttons(x)

    def _fit_buttons(self, x: int | None = None) -> None:
        """Classic taskbar: keep the app buttons from sliding under us (Windows 11 is left alone)."""
        if self._embed is None:
            return
        if x is None:
            r = winshell._rect(self._embed.hwnd)
            if not r:
                return
            x = r[0]
        winshell.fit_task_buttons(self._embed.bar, x - 2)

    def _e_press(self, x: int) -> None:
        QToolTip.hideText()
        self._press_at(QCursor.pos(), QPoint(round(x / self._dpr()), 0))

    def _e_move(self, x: int, _dragging: bool) -> None:
        self._move_at(QCursor.pos())

    def _e_release(self, _x: int) -> None:
        self._release_at(Qt.MouseButton.LeftButton, QCursor.pos())

    def _e_double(self, x: int) -> None:
        self._double_at(QPoint(round(x / self._dpr()), 0))

    def _e_right(self) -> None:
        QToolTip.hideText()
        self.menu_requested.emit(QCursor.pos())

    def _e_hover(self, on: bool) -> None:
        self._hover = on
        self._render_embed()
        if on:
            QTimer.singleShot(700, self._embed_tooltip)
        else:
            QToolTip.hideText()

    def _embed_tooltip(self) -> None:
        if self._embed is not None and self._hover and self._press is None:
            QToolTip.showText(QCursor.pos(), self.toolTip())

    def restyle(self) -> None:
        self._set_mode()
        self.style_ = PillStyle.resolve(self.settings, self.ctx.theme)
        self.tick(now_sec(), force=True)
        self.place()
        self._refresh()

    def tick(self, now: int, force: bool = False) -> None:
        store = self.ctx.store
        state = pill_state(store.race, store.get("live"), self.settings, now, self.ctx.zone, store.get("weather"))
        if force or state != (self.top, self.bottom, self.live):
            self.top, self.bottom, self.live = state
            race = store.race
            self.setToolTip((T('{name}\n', name=race.name) if race else "") + f"{self.top}\n{self.bottom}".strip()
                            + T('\n\nClick: panel · Drag: move · Drag left edge: resize · Right-click: menu'))
            if self.settings.pill_width <= 0:
                self.place()
            self._refresh()

    # ── geometry ────────────────────────────────────────────────────────────

    def _anchor(self):
        """(taskbar rect, tray rect, vertical?) in logical pixels, or None."""
        screen = QGuiApplication.primaryScreen()
        rects = winshell.taskbar_rects()
        if rects and rects.get("bar"):
            dpr = screen.devicePixelRatio()
            sg = screen.geometry()

            def log(r):
                return QRect(round(sg.x() + (r[0] - sg.x()) / dpr), round(sg.y() + (r[1] - sg.y()) / dpr),
                             round((r[2] - r[0]) / dpr), round((r[3] - r[1]) / dpr))

            bar = log(rects["bar"])
            # "anchor" = the tray plus other apps' widgets packed beside it: we go next to them.
            edge = (rects.get("anchor") if getattr(self.settings, "pill_auto_dock", True) else None)                 or rects.get("tray")
            tray = log(edge) if edge else None
            if tray is None or not tray.isValid():
                icon = self.tray_rect_fn()
                tray = icon if icon.isValid() else QRect(bar.right() - 200, bar.top(), 200, bar.height())
            return bar, tray, bar.height() > bar.width()
        icon = self.tray_rect_fn()
        avail, full = screen.availableGeometry(), screen.geometry()
        if avail.bottom() < full.bottom():
            bar = QRect(full.left(), avail.bottom() + 1, full.width(), full.bottom() - avail.bottom())
            tray = icon if icon.isValid() else QRect(bar.right() - 260, bar.top(), 260, bar.height())
            return bar, tray, False
        return None

    def _auto_width(self, height: int) -> int:
        lines = [self.top] + ([self.bottom] if self.bottom else [])
        fonts = fit_font(self.style_.family, lines, 10_000, height - 2, self._max_px(), self.settings.pill_autofit,
                         self.style_.bold_top)
        wide = max(QFontMetricsF(f).horizontalAdvance(t) for f, t in zip(fonts, lines))
        return round(wide + 2 * self._pad() + 2)

    def _max_px(self) -> float:
        return self.settings.pill_font_px * self.ctx.theme.scale

    def _pad(self) -> float:
        return max(4, min(14, self.height() * 0.3)) if self.style_.radius > 0 else 6

    def _offset(self) -> int:
        """Distance from the tray (or the widget we dock beside). Negative values move the countdown
        onto that widget, which only makes sense when we don't dock: another app that also makes
        room would then chase us back and forth every second. So docking ignores them."""
        off = int(self.settings.taskbar_pill_offset)
        return max(0, off) if getattr(self.settings, "pill_auto_dock", True) else off

    def place(self) -> None:
        s = self.settings
        anchor = self._anchor()
        if anchor is None:
            self.resize(max(60, s.pill_width or 160), max(20, s.pill_height or 34))
            return
        bar, tray, vertical = anchor
        if vertical:
            h = s.pill_height if s.pill_height > 0 else 40
            w = max(40, bar.width() - 6)
            y = max(bar.top() + 2, min(bar.bottom() - h - 1, tray.top() - h - 6 - self._offset()))
            self.setGeometry(bar.left() + 3, y, w, h)
            self._render_embed()
            return
        if s.pill_height == -1:
            h = bar.height()
        elif s.pill_height > 0:
            h = min(s.pill_height, bar.height())
        else:
            h = max(20, min(44, bar.height() - 6))
        w = s.pill_width if s.pill_width > 0 else self._auto_width(h)
        w = max(40, min(w, bar.width() // 2))
        x = max(bar.left() + 2, min(bar.right() - w - 1, tray.left() - w - 6 - self._offset()))
        self.setGeometry(x, bar.top() + (bar.height() - h) // 2, w, h)
        self._render_embed()

    def _taskbar_under_me(self) -> bool:
        g = self.geometry()
        sc = self.screen() or QGuiApplication.primaryScreen()
        dpr, sg = sc.devicePixelRatio(), sc.geometry()
        x = round(sg.x() + (g.center().x() - sg.x()) * dpr)
        y = round(sg.y() + (g.center().y() - sg.y()) * dpr)
        return winshell.taskbar_is_visible_at(x, y)

    def _guard(self) -> None:
        if not self.settings.taskbar_pill or self._press is not None:
            return
        if self._embed is not None:
            # Inside the taskbar Windows hides us with it (fullscreen, auto-hide); just stay
            # alive across Explorer restarts, follow the tray's width and stay above siblings.
            if not self._embed.alive():
                self._embed.destroy()
                if not self._embed.create():
                    self._embed = None
                    self._embed_failed = True
                    self.show()
                    self.raiser.start()
                    return
                self._embed_key = None
            self.place()
            self._fit_buttons()                    # Explorer re-widens them on its own layout passes
            self._embed.keep_on_top()
            return
        covered = winshell.foreground_is_fullscreen() or not self._taskbar_under_me()
        if covered:
            if self.isVisible():
                self._fs_hidden = True
                self.hide()
            return
        if self._fs_hidden:
            self._fs_hidden = False
            self.show()
        if self.isVisible():
            self.place()
            self._raise()

    # ── input ───────────────────────────────────────────────────────────────

    def _on_edge(self, pos) -> bool:
        return pos.x() <= EDGE

    def enterEvent(self, _e) -> None:
        self._hover = True
        self.update()

    def leaveEvent(self, _e) -> None:
        self._hover = False
        self.unsetCursor()
        self.update()

    def mousePressEvent(self, e) -> None:
        if e.button() == Qt.MouseButton.LeftButton:
            self._press_at(e.globalPosition().toPoint(), e.position().toPoint())

    def mouseMoveEvent(self, e) -> None:
        if self._press is None:
            self.setCursor(Qt.CursorShape.SizeHorCursor if self._on_edge(e.position().toPoint())
                           else Qt.CursorShape.PointingHandCursor)
            return
        self._move_at(e.globalPosition().toPoint())

    def mouseReleaseEvent(self, e) -> None:
        self._release_at(e.button(), e.globalPosition().toPoint())

    def mouseDoubleClickEvent(self, e) -> None:
        self._double_at(e.position().toPoint())

    def _press_at(self, gpos: QPoint, local: QPoint) -> None:
        self._press = gpos
        self._press_offset = self.settings.taskbar_pill_offset
        self._press_width = self.width()
        self._mode = "resize" if self._on_edge(local) else "move"
        self._moved = False

    def _move_at(self, gpos: QPoint) -> None:
        if self._press is None:
            return
        d = gpos - self._press
        if abs(d.x()) > 3 or abs(d.y()) > 3:
            self._moved = True
        if not self._moved:
            return
        anchor = self._anchor()
        vertical = bool(anchor and anchor[2])
        if self._mode == "resize" and not vertical:
            # Left edge drag: the right edge stays put, the width follows the mouse.
            self.settings.pill_width = max(40, min(900, self._press_width - d.x()))
        else:
            delta = -d.y() if vertical else -d.x()
            low = 0 if getattr(self.settings, "pill_auto_dock", True) else -400
            self.settings.taskbar_pill_offset = max(low, min(1600, self._press_offset + delta))
        self.place()

    def _release_at(self, button, gpos: QPoint) -> None:
        if button == Qt.MouseButton.LeftButton and self._press is not None:
            if self._moved:
                self.offset_changed.emit(self.settings.taskbar_pill_offset)
            else:
                self.clicked.emit()
        elif button == Qt.MouseButton.RightButton:
            self.menu_requested.emit(gpos)
        self._press = None

    def _double_at(self, local: QPoint) -> None:
        if self._on_edge(local):
            self.settings.pill_width = 0      # back to automatic width
            self.place()
            self.offset_changed.emit(self.settings.taskbar_pill_offset)

    # ── paint ───────────────────────────────────────────────────────────────

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        paint_pill(p, QRectF(self.rect()), self.style_, self.top, self.bottom, self.live, self._hover,
                   self._max_px(), self.settings.pill_autofit, self._pad())


class PillPreview(QWidget):
    """Same painter, for the settings dialog."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.style_ = None
        self.lines = ("", "", False)
        self.max_px = 11.0
        self.autofit = True
        self.setMinimumHeight(56)

    def set_preview(self, style: PillStyle, lines, size, max_px: float, autofit: bool, bar: QColor) -> None:
        self.style_, self.lines, self.max_px, self.autofit, self.bar = style, lines, max_px, autofit, bar
        self.pill_size = size
        self.update()

    def paintEvent(self, _e) -> None:
        if self.style_ is None:
            return
        p = QPainter(self)
        p.fillRect(self.rect(), self.bar)
        w, h = self.pill_size
        r = QRectF(self.width() - w - 96, (self.height() - h) / 2, w, h)
        pad = max(4, min(14, h * 0.3)) if self.style_.radius > 0 else 6
        paint_pill(p, r, self.style_, self.lines[0], self.lines[1], self.lines[2], False, self.max_px,
                   self.autofit, pad)
        f = QFont("Segoe UI")
        f.setPixelSize(12)
        p.setFont(f)
        p.setPen(QColor("#FFFFFF"))
        p.drawText(QRectF(self.width() - 92, 0, 88, self.height()), int(Qt.AlignmentFlag.AlignCenter), "^  ENG  19:10")
