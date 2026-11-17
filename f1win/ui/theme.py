"""Noctalia palette and metrics (measured from the original screenshots)."""
from __future__ import annotations

from dataclasses import dataclass

from PySide6.QtGui import QColor, QFont, QFontDatabase

from ..paths import resource_dir

LIGHT_ON = "#E10600"
LIGHT_GO = "#1FBF5B"
LIGHT_OFF = "#3A3F44"

_font_family: str | None = None


def load_fonts() -> str:
    """Register the bundled Noto Sans (OFL). Returns the family to use."""
    global _font_family
    if _font_family:
        return _font_family
    fid = QFontDatabase.addApplicationFont(str(resource_dir() / "fonts" / "NotoSans.ttf"))
    fams = QFontDatabase.applicationFontFamilies(fid) if fid >= 0 else []
    _font_family = fams[0] if fams else "Segoe UI"
    return _font_family


def _mix(a: str, b: str, t: float) -> str:
    ca, cb = QColor(a), QColor(b)
    return QColor(
        round(ca.red() + (cb.red() - ca.red()) * t),
        round(ca.green() + (cb.green() - ca.green()) * t),
        round(ca.blue() + (cb.blue() - ca.blue()) * t),
    ).name()


@dataclass
class Theme:
    accent: str = "#BCC2FF"          # primary
    surface: str = "#1B1B29"
    on_surface: str = "#E4E1EC"
    muted: str = "#C6C5D0"           # on_surface_variant
    secondary: str = "#C4C4DD"
    outline: str = "#46464F"         # outline_variant (divider)
    error: str = "#FFB4AB"
    scale: float = 1.0               # interface scale
    font_scale: float = 1.0
    family: str = "Noto Sans"
    mono: bool = False

    @property
    def on_accent(self) -> str:
        c = QColor(self.accent)
        # Dark text on light accents (the original's on_primary), light text otherwise.
        return "#262C67" if c.lightnessF() > 0.55 else "#F4F4FF"

    @property
    def hover(self) -> str:
        return _mix(self.surface, self.on_surface, 0.08)

    def px(self, v: float) -> int:
        return max(1, round(v * self.scale))

    def font(self, size: float, bold: bool = False, mono: bool | None = None) -> QFont:
        use_mono = self.mono if mono is None else mono
        f = QFont("Cascadia Mono" if use_mono else self.family)
        if use_mono:
            f.setStyleHint(QFont.StyleHint.Monospace)
            f.setFamilies(["Cascadia Mono", "Consolas", "Courier New"])
        f.setPixelSize(max(6, round(size * self.scale * self.font_scale)))
        f.setWeight(QFont.Weight.Bold if bold else QFont.Weight.Normal)
        f.setHintingPreference(QFont.HintingPreference.PreferNoHinting)
        f.setStyleStrategy(QFont.StyleStrategy.PreferAntialias)
        return f

    @staticmethod
    def from_settings(s) -> "Theme":
        fam = load_fonts()
        accent = s.accent if QColor.isValidColorName(s.accent) else "#BCC2FF"
        return Theme(
            accent=QColor(accent).name().upper(),
            scale=s.ui_scale / 100.0,
            font_scale=s.font_scale / 100.0,
            family=fam,
            mono=s.font_family == "monospace",
        )
