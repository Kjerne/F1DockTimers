"""Render the Microsoft Store logos (build/store/Assets/*.png) from the app icon.

    python tools/make_store_assets.py

Sizes follow the MSIX requirements: Square44x44Logo (taskbar, Start list), Square150x150Logo
(Start tile), Wide310x150Logo, StoreLogo (50x50), each with a scale-200 version for HiDPI screens.
"""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("F1DT_NO_I18N", "1")

from PySide6.QtCore import QRect, QSize, Qt  # noqa: E402
from PySide6.QtGui import QColor, QImage, QPainter  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

from f1win.ui.widgets import app_icon  # noqa: E402

SURFACE = "#1B1B29"          # tile background, same as the app's panel
LOGOS = {                    # name: (width, height, icon share of the height)
    "Square44x44Logo": (44, 44, 1.0),
    "Square150x150Logo": (150, 150, 0.6),
    "Wide310x150Logo": (310, 150, 0.6),
    "StoreLogo": (50, 50, 1.0),
}


def render(w: int, h: int, share: float, out: Path) -> None:
    img = QImage(w, h, QImage.Format.Format_ARGB32_Premultiplied)
    full_bleed = share >= 1.0
    img.fill(Qt.GlobalColor.transparent if full_bleed else QColor(SURFACE))
    side = int(h * share)
    p = QPainter(img)
    p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
    pm = app_icon("#BCC2FF").pixmap(QSize(side, side))
    p.drawPixmap(QRect((w - side) // 2, (h - side) // 2, side, side), pm)
    p.end()
    img.save(str(out))


def main() -> None:
    QApplication([])
    out = ROOT / "build" / "store" / "Assets"
    out.mkdir(parents=True, exist_ok=True)
    for name, (w, h, share) in LOGOS.items():
        render(w, h, share, out / f"{name}.png")
        render(w * 2, h * 2, share, out / f"{name}.scale-200.png")
    print("wrote", out)


if __name__ == "__main__":
    main()
