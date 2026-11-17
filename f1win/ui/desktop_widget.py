"""Persistent desktop widget: draggable, resizable, remembers its place."""
from __future__ import annotations

from PySide6.QtCore import QEvent, QRect, Qt, QTimer, Signal
from PySide6.QtGui import QAction, QGuiApplication
from PySide6.QtWidgets import QMenu, QScrollBar, QVBoxLayout, QWidget

from .panel import F1Panel
from .widgets import IconButton, PillButton, Row

EDGE = 7
MIN_W, MIN_H = 300, 260


def restore_rect(saved, screens: list[QRect], default_size) -> QRect | None:
    """Saved [x, y, w, h] if at least 40x40 px of it is on a current screen. Pure."""
    if not (isinstance(saved, list) and len(saved) == 4 and all(isinstance(v, (int, float)) for v in saved)):
        return None
    r = QRect(int(saved[0]), int(saved[1]), max(MIN_W, int(saved[2])), max(MIN_H, int(saved[3])))
    for s in screens:
        inter = r.intersected(s)
        if inter.width() >= 40 and inter.height() >= 40:
            return r
    return None


class DesktopWidget(QWidget):
    geometry_changed = Signal()
    action = Signal(str)  # refresh | settings | panel | hide | toggle_top | toggle_click | toggle_lock

    def __init__(self, ctx, settings, parent=None) -> None:
        super().__init__(parent, Qt.WindowType.FramelessWindowHint | Qt.WindowType.Tool
                         | Qt.WindowType.NoDropShadowWindowHint)
        self.settings = settings
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setWindowTitle("Formula 1 widget")
        self.setAccessibleName("Formula 1 desktop widget")
        self.setMinimumSize(MIN_W, MIN_H)
        self.setMouseTracking(True)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        self.panel = F1Panel(ctx, "widget", self)
        self.panel.setMouseTracking(True)
        self.panel.close_requested.connect(lambda: None)  # Esc does not hide a desktop widget
        lay.addWidget(self.panel)
        self.panel.installEventFilter(self)
        self._save = QTimer(self)
        self._save.setSingleShot(True)
        self._save.setInterval(600)
        self._save.timeout.connect(self._persist)
        self.apply_settings(first=True)

    # ── settings ────────────────────────────────────────────────────────────

    def apply_settings(self, first: bool = False) -> None:
        s = self.settings
        self.panel.bg_opacity = s.widget_opacity / 100.0
        was_visible = self.isVisible()
        flags = (Qt.WindowType.FramelessWindowHint | Qt.WindowType.Tool | Qt.WindowType.NoDropShadowWindowHint)
        if s.widget_always_on_top:
            flags |= Qt.WindowType.WindowStaysOnTopHint
        if s.widget_click_through:
            flags |= Qt.WindowType.WindowTransparentForInput
        if flags != self.windowFlags():
            geo = self.geometry()
            self.setWindowFlags(flags)
            if not first:
                self.setGeometry(geo)
        if first:
            self.restore_geometry()
        if was_visible and not self.isVisible():
            self.show()
        self.panel.update()

    def restore_geometry(self) -> None:
        screens = [sc.availableGeometry() for sc in QGuiApplication.screens()]
        scale = self.panel.ctx.theme.scale
        rect = restore_rect(self.settings.widget_geometry, screens, None) if self.settings.widget_remember_position else None
        if rect is None:
            avail = QGuiApplication.primaryScreen().availableGeometry()
            w, h = round(420 * scale), round(560 * scale)
            rect = QRect(avail.right() - w - 24, avail.top() + 24, w, h)
        self.setGeometry(rect)

    def _persist(self) -> None:
        if self.settings.widget_remember_position:
            g = self.geometry()
            self.settings.widget_geometry = [g.x(), g.y(), g.width(), g.height()]
            self.settings.save()

    def moveEvent(self, e) -> None:
        super().moveEvent(e)
        self._save.start()

    def resizeEvent(self, e) -> None:
        super().resizeEvent(e)
        self._save.start()

    # ── drag / resize via the native window manager ─────────────────────────

    def _edges(self, pos) -> Qt.Edge:
        edges = Qt.Edge(0)
        r = self.rect()
        if pos.x() <= EDGE:
            edges |= Qt.Edge.LeftEdge
        if pos.x() >= r.width() - EDGE:
            edges |= Qt.Edge.RightEdge
        if pos.y() <= EDGE:
            edges |= Qt.Edge.TopEdge
        if pos.y() >= r.height() - EDGE:
            edges |= Qt.Edge.BottomEdge
        return edges

    def _interactive(self, pos) -> bool:
        w = self.childAt(pos)
        while w is not None and w is not self:
            if isinstance(w, (PillButton, IconButton, QScrollBar)) or (isinstance(w, Row) and w.url):
                return True
            w = w.parentWidget()
        return False

    def eventFilter(self, obj, e) -> bool:
        et = e.type()
        if et == QEvent.Type.MouseMove and not self.settings.widget_locked:
            edges = self._edges(e.position().toPoint())
            left, right = bool(edges & Qt.Edge.LeftEdge), bool(edges & Qt.Edge.RightEdge)
            top, bottom = bool(edges & Qt.Edge.TopEdge), bool(edges & Qt.Edge.BottomEdge)
            cursor = None
            if (left and top) or (right and bottom):
                cursor = Qt.CursorShape.SizeFDiagCursor
            elif (right and top) or (left and bottom):
                cursor = Qt.CursorShape.SizeBDiagCursor
            elif left or right:
                cursor = Qt.CursorShape.SizeHorCursor
            elif top or bottom:
                cursor = Qt.CursorShape.SizeVerCursor
            if cursor is not None:
                self.panel.setCursor(cursor)
            else:
                self.panel.unsetCursor()
        elif et == QEvent.Type.MouseButtonPress and e.button() == Qt.MouseButton.LeftButton:
            pos = e.position().toPoint()
            if self.settings.widget_locked:
                return False
            edges = self._edges(pos)
            handle = self.windowHandle()
            if edges and handle is not None:
                handle.startSystemResize(edges)
                return True
            if not self._interactive(pos) and handle is not None:
                handle.startSystemMove()
                return True
        elif et == QEvent.Type.MouseButtonDblClick and e.button() == Qt.MouseButton.LeftButton:
            if not self._interactive(e.position().toPoint()):
                act = self.settings.widget_double_click
                if act != "none":
                    self.action.emit(act)
                return True
        elif et == QEvent.Type.ContextMenu:
            self._menu(e.globalPos())
            return True
        return super().eventFilter(obj, e)

    def _menu(self, gpos) -> None:
        s = self.settings
        m = QMenu(self)
        m.addAction("Refresh", lambda: self.action.emit("refresh"))
        m.addAction("Open panel", lambda: self.action.emit("panel"))
        m.addSeparator()
        for label, attr, act in (("Lock position and size", "widget_locked", "toggle_lock"),
                                 ("Always on top", "widget_always_on_top", "toggle_top"),
                                 ("Click-through (turn off from the tray menu)", "widget_click_through", "toggle_click")):
            a = QAction(label, m)
            a.setCheckable(True)
            a.setChecked(getattr(s, attr))
            a.triggered.connect(lambda _c=False, act=act: self.action.emit(act))
            m.addAction(a)
        m.addSeparator()
        m.addAction("Settings…", lambda: self.action.emit("settings"))
        m.addAction("Hide widget", lambda: self.action.emit("hide"))
        m.exec(gpos)

    def keyPressEvent(self, e) -> None:
        self.panel.keyPressEvent(e)
