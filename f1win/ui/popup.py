"""Tray popup: frameless panel anchored above the tray icon, animated, dismiss on outside click."""
from __future__ import annotations

import logging
import time

from PySide6.QtCore import QEasingCurve, QEvent, QParallelAnimationGroup, QPoint, QPropertyAnimation, QRect, Qt
from PySide6.QtGui import QCursor, QGuiApplication
from PySide6.QtWidgets import QApplication, QVBoxLayout, QWidget

from .panel import F1Panel

log = logging.getLogger(__name__)
BASE_W, BASE_H = 500, 640
GAP = 10


def anchor_position(icon: QRect, avail: QRect, full: QRect, size, gap: int = GAP) -> tuple[QPoint, str]:
    """Where the popup goes for a tray icon rect, and the taskbar edge. Pure, for tests.

    The taskbar edge is where the screen's available area was trimmed. The
    result is clamped inside the available area (never under the taskbar).
    """
    w, h = size.width(), size.height()
    if avail.bottom() < full.bottom():
        edge = "bottom"
    elif avail.top() > full.top():
        edge = "top"
    elif avail.left() > full.left():
        edge = "left"
    elif avail.right() < full.right():
        edge = "right"
    else:
        edge = "bottom"  # auto-hidden taskbar: assume the default
    cx = icon.center().x() if icon.isValid() else avail.right()
    cy = icon.center().y() if icon.isValid() else avail.bottom()
    if edge == "bottom":
        x, y = cx - w // 2, avail.bottom() - h - gap + 1
    elif edge == "top":
        x, y = cx - w // 2, avail.top() + gap
    elif edge == "left":
        x, y = avail.left() + gap, cy - h // 2
    else:
        x, y = avail.right() - w - gap + 1, cy - h // 2
    x = max(avail.left() + gap, min(x, avail.right() - w - gap + 1))
    y = max(avail.top() + gap, min(y, avail.bottom() - h - gap + 1))
    return QPoint(x, y), edge


class TrayPopup(QWidget):
    def __init__(self, ctx, settings, parent=None) -> None:
        super().__init__(parent, Qt.WindowType.FramelessWindowHint | Qt.WindowType.Tool
                         | Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.NoDropShadowWindowHint)
        self.settings = settings
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setWindowTitle("Formula 1")
        self.setAccessibleName("Formula 1 panel")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        self.panel = F1Panel(ctx, "popup", self)
        self.panel.bg_opacity = settings.popup_opacity / 100.0
        self.panel.set_pinned(settings.popup_pinned)
        self.panel.close_requested.connect(self.hide_animated)
        self.panel.pin_toggled.connect(self._pinned)
        lay.addWidget(self.panel)
        self.hidden_at = 0.0
        self._anim: QParallelAnimationGroup | None = None
        self.apply_size()

    def apply_size(self) -> None:
        s = self.panel.ctx.theme.scale
        self.resize(round(BASE_W * s), round(BASE_H * s))

    def _pinned(self, on: bool) -> None:
        self.settings.popup_pinned = on
        self.settings.save()

    def place(self, icon_rect: QRect) -> str:
        screen = QGuiApplication.screenAt(icon_rect.center()) if icon_rect.isValid() else None
        screen = screen or QGuiApplication.screenAt(QCursor.pos()) or QGuiApplication.primaryScreen()
        avail = screen.availableGeometry()
        # Never larger than the monitor (high scaling on small screens).
        self.apply_size()
        max_w, max_h = avail.width() - 2 * GAP, avail.height() - 2 * GAP
        if self.width() > max_w or self.height() > max_h:
            self.resize(min(self.width(), max_w), min(self.height(), max_h))
        pos, edge = anchor_position(icon_rect, avail, screen.geometry(), self.size())
        self.move(pos)
        log.debug("popup at %s size %s on %s (taskbar %s, icon %s)", pos, self.size(), screen.name(), edge, icon_rect)
        return edge

    def toggle(self, icon_rect: QRect) -> None:
        if self.isVisible():
            self.hide_animated()
            return
        # The click that deactivated (and hid) us is the same click on the tray icon.
        if time.monotonic() - self.hidden_at < 0.35:
            return
        self.show_at(icon_rect)

    def show_at(self, icon_rect: QRect) -> None:
        edge = self.place(icon_rect)
        self.panel.open_fresh()
        end = self.pos()
        if self.settings.animations:
            dx, dy = {"bottom": (0, 14), "top": (0, -14), "left": (-14, 0), "right": (14, 0)}[edge]
            self.setWindowOpacity(0.0)
            self.move(end + QPoint(dx, dy))
            self.show()
            self._animate(end + QPoint(dx, dy), end, 0.0, 1.0, 170)
        else:
            self.setWindowOpacity(1.0)
            self.show()
        self.raise_()
        self.activateWindow()
        self.panel.setFocus()

    def _animate(self, start: QPoint, end: QPoint, o0: float, o1: float, ms: int, done=None) -> None:
        if self._anim is not None:
            self._anim.stop()
        g = QParallelAnimationGroup(self)
        a = QPropertyAnimation(self, b"pos", g)
        a.setStartValue(start)
        a.setEndValue(end)
        a.setDuration(ms)
        a.setEasingCurve(QEasingCurve.Type.OutCubic)
        b = QPropertyAnimation(self, b"windowOpacity", g)
        b.setStartValue(o0)
        b.setEndValue(o1)
        b.setDuration(ms)
        g.addAnimation(a)
        g.addAnimation(b)
        if done:
            g.finished.connect(done)
        self._anim = g
        g.start()

    def hide_animated(self) -> None:
        if not self.isVisible():
            return
        self.hidden_at = time.monotonic()
        if self.settings.animations:
            start = self.pos()

            def finish():
                self.hide()
                self.move(start)
                self.setWindowOpacity(1.0)

            self._animate(start, start, self.windowOpacity(), 0.0, 120, finish)
        else:
            self.hide()

    def changeEvent(self, e) -> None:
        if e.type() == QEvent.Type.ActivationChange and not self.isActiveWindow():
            if self.isVisible() and not self.panel.pinned and QApplication.activeModalWidget() is None:
                self.hide_animated()
        super().changeEvent(e)
