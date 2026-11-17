"""Render the app icon (flag glyph on the Noctalia surface) to build/app.ico."""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PIL import Image  # noqa: E402
from PySide6.QtCore import QSize  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

from f1win.ui.widgets import app_icon  # noqa: E402

app = QApplication([])
icon = app_icon("#BCC2FF")
out = ROOT / "build"
out.mkdir(exist_ok=True)
sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
images = []
for s in sizes:
    pm = icon.pixmap(QSize(s, s))
    f = out / f"icon_{s}.png"
    pm.save(str(f))
    images.append(Image.open(f).convert("RGBA"))
images[-1].save(out / "app.ico", sizes=[(s, s) for s in sizes], append_images=images[:-1])
for s in sizes:
    (out / f"icon_{s}.png").unlink()
print("wrote", out / "app.ico")
