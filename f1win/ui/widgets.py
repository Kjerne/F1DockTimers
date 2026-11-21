"""Small painted building blocks recreating Noctalia's ui.* primitives."""
from __future__ import annotations

from PySide6.QtCore import QByteArray, QEasingCurve, QPointF, QRectF, QSize, Qt, QVariantAnimation, Signal
from PySide6.QtGui import QColor, QFont, QFontMetrics, QIcon, QPainter, QPainterPath, QPen, QPixmap
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtWidgets import QHBoxLayout, QLabel, QSizePolicy, QWidget

from ..core.tracks import geometry
from .theme import LIGHT_GO, LIGHT_OFF, LIGHT_ON, Theme
from ..i18n import T

FLAG_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="{c}" '
    'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
    '<path d="M5 5a5 5 0 0 1 7 0a5 5 0 0 0 7 0v9a5 5 0 0 1 -7 0a5 5 0 0 0 -7 0v-9z"/>'
    '<path d="M5 21v-7"/></svg>'
)


def svg_pixmap(svg: str, size: int, dpr: float = 1.0) -> QPixmap:
    r = QSvgRenderer(QByteArray(svg.encode()))
    pm = QPixmap(round(size * dpr), round(size * dpr))
    pm.fill(Qt.GlobalColor.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    r.render(p)
    p.end()
    pm.setDevicePixelRatio(dpr)
    return pm


class Text(QWidget):
    """Single-line label that elides instead of clipping (ui.label maxLines=1)."""

    def __init__(self, text: str, theme: Theme, size: float = 13, bold: bool = False,
                 color: str | None = None, align=Qt.AlignmentFlag.AlignLeft, mono: bool | None = None,
                 tracking: float = 0.0, parent=None) -> None:
        super().__init__(parent)
        self._text = text or ""
        self.theme = theme
        self._font = theme.font(size, bold, mono)
        if tracking:
            self._font.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, tracking * theme.scale)
        self._color = QColor(color or theme.on_surface)
        self._align = align
        self.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)

    def setText(self, text: str) -> None:
        if text != self._text:
            self._text = text or ""
            self.updateGeometry()
            self.update()

    def text(self) -> str:
        return self._text

    def setColor(self, color: str) -> None:
        self._color = QColor(color)
        self.update()

    def sizeHint(self) -> QSize:
        fm = QFontMetrics(self._font)
        return QSize(fm.horizontalAdvance(self._text) + 2, fm.height())

    def minimumSizeHint(self) -> QSize:
        fm = QFontMetrics(self._font)
        return QSize(min(fm.horizontalAdvance(self._text) + 2, fm.horizontalAdvance("…") * 3), fm.height())

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.TextAntialiasing)
        p.setFont(self._font)
        p.setPen(self._color)
        fm = QFontMetrics(self._font)
        shown = fm.elidedText(self._text, Qt.TextElideMode.ElideRight, self.width())
        p.drawText(self.rect(), int(self._align | Qt.AlignmentFlag.AlignVCenter), shown)
        if shown != self._text:
            self.setToolTip(self._text)


class ClickText(Text):
    """Text that acts as a link (e.g. "Update available")."""

    clicked = Signal()

    def __init__(self, *a, **k) -> None:
        super().__init__(*a, **k)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, False)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def mouseReleaseEvent(self, e) -> None:
        if e.button() == Qt.MouseButton.LeftButton and self.rect().contains(e.position().toPoint()):
            self.clicked.emit()


class Wrap(QLabel):
    """Multi-line hint text (ui.label maxLines=2..3)."""

    def __init__(self, text: str, theme: Theme, size: float = 12, color: str | None = None,
                 bold: bool = False, parent=None) -> None:
        super().__init__(text, parent)
        self.setWordWrap(True)
        self.setFont(theme.font(size, bold))
        self.setStyleSheet(f"color:{color or theme.muted}; background:transparent;")
        self.setTextInteractionFlags(Qt.TextInteractionFlag.NoTextInteraction)


class Chip(QWidget):
    """3x16 team colour bar."""

    def __init__(self, color: str, theme: Theme, parent=None) -> None:
        super().__init__(parent)
        self._c = QColor(color)
        self.setFixedSize(theme.px(3), theme.px(16))

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(self._c)
        r = min(self.width(), self.height()) / 3
        p.drawRoundedRect(QRectF(self.rect()), r, r)


class Divider(QWidget):
    def __init__(self, theme: Theme, parent=None) -> None:
        super().__init__(parent)
        self._c = QColor(theme.outline)
        self.setFixedHeight(1)

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        p.fillRect(self.rect(), self._c)


class PillButton(QWidget):
    """Tab button: ghost, or filled with the accent when selected (controlSize sm)."""

    clicked = Signal()

    def __init__(self, text: str, theme: Theme, selected: bool = False, parent=None) -> None:
        super().__init__(parent)
        self.theme = theme
        self._text = text
        self._selected = selected
        self._hover = False
        self._font = theme.font(12.5)
        fm = QFontMetrics(self._font)
        self.setFixedHeight(theme.px(30))
        self.setFixedWidth(fm.horizontalAdvance(text) + theme.px(20))
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setAccessibleName(T('{text} tab', text=text))
        self.setToolTip(text)

    def setSelected(self, sel: bool) -> None:
        self._selected = sel
        self.update()

    def enterEvent(self, _e) -> None:
        self._hover = True
        self.update()

    def leaveEvent(self, _e) -> None:
        self._hover = False
        self.update()

    def mouseReleaseEvent(self, e) -> None:
        if e.button() == Qt.MouseButton.LeftButton and self.rect().contains(e.position().toPoint()):
            self.clicked.emit()

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        rad = self.theme.px(6)
        if self._selected:
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QColor(self.theme.accent))
            p.drawRoundedRect(r, rad, rad)
            p.setPen(QColor(self.theme.on_accent))
        else:
            if self._hover:
                p.setPen(Qt.PenStyle.NoPen)
                c = QColor(self.theme.on_surface)
                c.setAlphaF(0.08)
                p.setBrush(c)
                p.drawRoundedRect(r, rad, rad)
            p.setPen(QColor(self.theme.on_surface))
        p.setFont(self._font)
        p.drawText(self.rect(), int(Qt.AlignmentFlag.AlignCenter), self._text)


class IconButton(QWidget):
    """Tiny glyph button for the header (pin, settings, close)."""

    clicked = Signal()

    def __init__(self, svg: str, theme: Theme, tip: str, size: int = 16, parent=None) -> None:
        super().__init__(parent)
        self.theme = theme
        self._svg = svg
        self._size = theme.px(size)
        self._hover = False
        self._active = False
        self.setFixedSize(self._size + theme.px(8), self._size + theme.px(8))
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setToolTip(tip)
        self.setAccessibleName(tip)

    def setActive(self, on: bool) -> None:
        self._active = on
        self.update()

    def enterEvent(self, _e) -> None:
        self._hover = True
        self.update()

    def leaveEvent(self, _e) -> None:
        self._hover = False
        self.update()

    def mouseReleaseEvent(self, e) -> None:
        if e.button() == Qt.MouseButton.LeftButton and self.rect().contains(e.position().toPoint()):
            self.clicked.emit()

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        if self._hover:
            c = QColor(self.theme.on_surface)
            c.setAlphaF(0.08)
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(c)
            p.drawRoundedRect(QRectF(self.rect()), self.theme.px(5), self.theme.px(5))
        color = self.theme.accent if (self._active or self._hover) else self.theme.muted
        pm = svg_pixmap(self._svg.format(c=color), self._size, self.devicePixelRatioF())
        off = (self.width() - self._size) // 2
        p.drawPixmap(off, off, pm)


class FlagGlyph(QWidget):
    def __init__(self, theme: Theme, color: str | None = None, size: int = 16, parent=None) -> None:
        super().__init__(parent)
        self.theme = theme
        self._color = color or theme.accent
        self._size = theme.px(size)
        self.setFixedSize(self._size, self._size)

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        p.drawPixmap(0, 0, svg_pixmap(FLAG_SVG.format(c=self._color), self._size, self.devicePixelRatioF()))


class LightsGantry(QWidget):
    """Five columns of two lamps: red until lights out, green once the race runs."""

    COLUMNS = 5

    def __init__(self, theme: Theme, parent=None) -> None:
        super().__init__(parent)
        self.theme = theme
        self.lit = self.COLUMNS
        self.go = False
        d, gap_x, gap_y = theme.px(12), theme.px(7), theme.px(3)
        self.setFixedSize(self.COLUMNS * d + (self.COLUMNS - 1) * gap_x, 2 * d + gap_y)
        self.setAccessibleName(T('Start lights'))

    def set_state(self, lit: int, go: bool) -> None:
        if (lit, go) != (self.lit, self.go):
            self.lit, self.go = lit, go
            self.update()

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(Qt.PenStyle.NoPen)
        d, gap_x, gap_y = self.theme.px(12), self.theme.px(7), self.theme.px(3)
        for col in range(self.COLUMNS):
            on = self.lit >= col + 1
            c = QColor((LIGHT_GO if self.go else LIGHT_ON) if on else LIGHT_OFF)
            if not on:
                c.setAlphaF(0.55)
            p.setBrush(c)
            for row in range(2):
                p.drawEllipse(QRectF(col * (d + gap_x), row * (d + gap_y), d, d))


class TrackView(QWidget):
    """Circuit outline painted from tracks.txt geometry, fading in like the original."""

    def __init__(self, points, theme: Theme, w: int = 170, h: int = 150, parent=None) -> None:
        super().__init__(parent)
        self.theme = theme
        self.points = points
        self.fade = 1.0
        self.reveal = 1.0
        self.setFixedSize(theme.px(w), theme.px(h))
        self.setAccessibleName(T('Circuit outline'))
        self._anim: QVariantAnimation | None = None

    def animate_in(self, ms: int = 280, draw_ms: int = 0) -> None:
        self.fade = 0.0
        self.reveal = 0.0 if draw_ms else 1.0
        total = max(ms, draw_ms)
        self._anim = QVariantAnimation(self)
        self._anim.setStartValue(0.0)
        self._anim.setEndValue(1.0)
        self._anim.setDuration(total)
        self._anim.setEasingCurve(QEasingCurve.Type.OutCubic)

        def step(v):
            t = float(v) * total
            self.fade = min(1.0, t / ms) if ms else 1.0
            self.reveal = min(1.0, t / draw_ms) if draw_ms else 1.0
            self.update()

        self._anim.valueChanged.connect(step)
        self._anim.start()

    def paintEvent(self, _e) -> None:
        if not self.points:
            return
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        (bx, by, bw, bh), stroke, ((x1, y1), (x2, y2)) = geometry(self.points)
        s = min(self.width() / bw, self.height() / bh)
        ox = (self.width() - bw * s) / 2 - bx * s
        oy = (self.height() - bh * s) / 2 - by * s

        def pt(x, y):
            return QPointF(ox + x * s, oy + y * s)

        path = QPainterPath(pt(*self.points[0]))
        for x, y in self.points[1:]:
            path.lineTo(pt(x, y))
        color = QColor(self.theme.on_surface)
        p.setOpacity(0.85 * self.fade)
        pen = QPen(color, stroke * s)
        pen.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
        pen.setCapStyle(Qt.PenCapStyle.RoundCap)
        if self.reveal < 1.0:
            length = path.length() or 1.0
            pen.setDashPattern([length / (stroke * s) * self.reveal + 0.01, length / (stroke * s) + 1])
        p.setPen(pen)
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawPath(path)
        tick = QPen(color, stroke * s * 1.8)
        tick.setCapStyle(Qt.PenCapStyle.FlatCap)
        p.setPen(tick)
        p.drawLine(pt(x1, y1), pt(x2, y2))


class Row(QWidget):
    """A table row: hover highlight; opens its Wikipedia page when it has one."""

    def __init__(self, theme: Theme, url: str = "", open_link=None, parent=None) -> None:
        super().__init__(parent)
        self.theme = theme
        self.url = url
        self.open_link = open_link
        self._hover = False
        self.lay = QHBoxLayout(self)
        self.lay.setContentsMargins(theme.px(4), theme.px(4), theme.px(4), theme.px(4))
        self.lay.setSpacing(theme.px(8))
        if url:
            self.setCursor(Qt.CursorShape.PointingHandCursor)
            self.setToolTip(T('Open on Wikipedia'))

    def cell(self, w: QWidget, width: int | None = None, stretch: int = 0) -> QWidget:
        if width is not None:
            w.setFixedWidth(self.theme.px(width))
        self.lay.addWidget(w, stretch)
        return w

    def enterEvent(self, _e) -> None:
        if self.url:
            self._hover = True
            self.update()

    def leaveEvent(self, _e) -> None:
        self._hover = False
        self.update()

    def mouseReleaseEvent(self, e) -> None:
        if self.url and self.open_link and e.button() == Qt.MouseButton.LeftButton:
            self.open_link(self.url)

    def paintEvent(self, _e) -> None:
        if self._hover:
            p = QPainter(self)
            p.setRenderHint(QPainter.RenderHint.Antialiasing)
            c = QColor(self.theme.on_surface)
            c.setAlphaF(0.06)
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(c)
            p.drawRoundedRect(QRectF(self.rect()), self.theme.px(4), self.theme.px(4))


def app_icon(accent: str = "#BCC2FF", live: bool = False) -> QIcon:
    """Tray / window icon: the flag glyph on a dark rounded square."""
    icon = QIcon()
    for size in (16, 20, 24, 32, 40, 48, 64, 128, 256):
        pm = QPixmap(size, size)
        pm.fill(Qt.GlobalColor.transparent)
        p = QPainter(pm)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor("#1B1B29"))
        p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.22, size * 0.22)
        glyph = svg_pixmap(FLAG_SVG.format(c=accent), round(size * 0.72))
        off = (size - glyph.width()) / 2
        p.drawPixmap(round(off), round(off + size * 0.02), glyph)
        if live:
            p.setBrush(QColor(LIGHT_ON))
            r = size * 0.30
            p.drawEllipse(QRectF(size - r - size * 0.04, size * 0.04, r, r))
        p.end()
        icon.addPixmap(pm)
    return icon
