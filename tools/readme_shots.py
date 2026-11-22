"""Render README screenshots of the panel tabs, taskbar countdown styles and Settings.

Everything is drawn by the app itself (no desktop capture), so no wallpaper or
personal windows can end up in the images.

    set NOCTALIA_F1_HOME=<folder with config/ and cache/>   (optional: a copy of your data)
    python tools/readme_shots.py docs/screenshots
"""
import dataclasses
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
os.environ.setdefault("QT_SCALE_FACTOR", "1.5")

from PySide6.QtCore import QRectF, Qt, QTimer  # noqa: E402
from PySide6.QtGui import QColor, QFont, QImage, QPainter  # noqa: E402
from PySide6.QtWidgets import QApplication, QTabWidget  # noqa: E402

app = QApplication(sys.argv[:1])

from f1win.app import apply_dark_palette  # noqa: E402
from f1win.core.timeutil import now_sec, zone_for  # noqa: E402
from f1win.core.tracks import TrackLibrary  # noqa: E402
from f1win.paths import resource_dir  # noqa: E402
from f1win.services.store import DataStore  # noqa: E402
from f1win.settings import Settings  # noqa: E402
from f1win.ui.panel import F1Panel  # noqa: E402
from f1win.ui.settings_dialog import SettingsDialog  # noqa: E402
from f1win.ui.tabs.common import Ctx  # noqa: E402
from f1win.ui.taskbar_pill import PillStyle, paint_pill, pill_state  # noqa: E402
from f1win.ui.theme import Theme, load_fonts  # noqa: E402

apply_dark_palette(app)
out = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "docs" / "screenshots")
out.mkdir(parents=True, exist_ok=True)

settings = Settings.load()
settings.popup_opacity = 100
settings.animations = False
settings.spoiler_free = False            # show the real tabs in the README
settings.grid_official = True
store = DataStore()
store.load_cache()
theme = Theme.from_settings(settings)
tracks = TrackLibrary(resource_dir() / "tracks.txt")


def panel_shots():
    ctx = Ctx(store, settings, theme, tracks, lambda u: None)
    p = F1Panel(ctx, "popup")
    p.bg_opacity = 1.0
    p.resize(500, 690)
    p.show()
    for tab in ("schedule", "weather", "grid", "live", "results", "drivers", "constructors"):
        if tab not in p.valid_views(now_sec()):
            continue                     # e.g. no grid outside a race weekend
        p.set_view(tab, animate=False)
        app.processEvents()
        p.grab().save(str(out / f"panel-{tab}.png"))
    p.close()


def pill_shots():
    """Three styles on a neutral, Windows-11-like taskbar strip."""
    dpr = 2.0
    w, h = 520, 3 * 52
    img = QImage(int(w * dpr), int(h * dpr), QImage.Format.Format_ARGB32_Premultiplied)
    img.setDevicePixelRatio(dpr)
    img.fill(QColor("#202020"))
    p = QPainter(img)
    load_fonts()
    styles = [
        ("Noctalia", dict(pill_style="noctalia", pill_radius=7)),
        ("Match taskbar", dict(pill_style="taskbar", pill_radius=4)),
        ("Custom box", dict(pill_style="custom", pill_radius=0, pill_bg="#E10600", pill_bg_opacity=100,
                            pill_text="#FFFFFF", pill_text2="#FFFFFF", pill_border=False)),
    ]
    tray = QFont("Segoe UI")
    tray.setPixelSize(12)
    for i, (label, over) in enumerate(styles):
        s = dataclasses.replace(settings, **over)
        y = i * 52
        p.fillRect(QRectF(0, y, w, 48), QColor("#202020"))
        p.fillRect(QRectF(0, y + 48, w, 4), QColor("#151515"))
        top, bottom, live = pill_state(store.race, store.get("live"), s, now_sec(), zone_for(s.timezone))
        style = PillStyle.resolve(s, theme)
        r = QRectF(w - 330, y + 9, 200, 30)
        paint_pill(p, r, style, top, bottom, live, False, 11, True, 9 if s.pill_radius else 6)
        p.setFont(tray)
        p.setPen(QColor("#FFFFFF"))
        p.drawText(QRectF(w - 120, y, 110, 48), int(Qt.AlignmentFlag.AlignCenter), "^  ENG   19:10")
        p.setPen(QColor("#8F8F9E"))
        p.drawText(QRectF(12, y, 150, 48), int(Qt.AlignmentFlag.AlignVCenter), label)
    p.end()
    img.save(str(out / "taskbar-styles.png"))


def settings_shot():
    d = SettingsDialog(settings, store, theme)
    d.resize(560, 700)
    d.show()
    tabs = d.findChild(QTabWidget)
    tabs.setCurrentIndex(2)
    app.processEvents()
    d.grab().save(str(out / "settings-taskbar.png"))
    tabs.setCurrentIndex(3)
    app.processEvents()
    d.grab().save(str(out / "settings-f1.png"))
    tabs.setCurrentIndex(4)
    app.processEvents()
    d.grab().save(str(out / "settings-live.png"))
    d.close()


def run():
    panel_shots()
    pill_shots()
    settings_shot()
    print("\n".join(sorted(f.name for f in out.glob("*.png"))))
    app.quit()


QTimer.singleShot(300, run)
app.exec()
