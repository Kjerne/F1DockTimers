"""UI geometry: popup placement on any taskbar edge / monitor, widget restore, scaling."""
import pytest
from PySide6.QtCore import QRect, QSize

from f1win.ui.desktop_widget import restore_rect
from f1win.ui.popup import anchor_position

SIZE = QSize(500, 640)


def inside(pos, size, avail):
    return QRect(pos, size).intersected(avail) == QRect(pos, size)


@pytest.mark.parametrize("scale", [1.0, 1.25, 1.5])
def test_bottom_taskbar_at_each_scale(scale):
    # Logical geometry of a 2560x1440 panel at the given Windows scaling.
    full = QRect(0, 0, round(2560 / scale), round(1440 / scale))
    avail = QRect(0, 0, full.width(), full.height() - round(48 / scale))
    icon = QRect(full.width() - 200, avail.bottom() + 8, 24, 24)
    pos, edge = anchor_position(icon, avail, full, SIZE)
    assert edge == "bottom"
    assert inside(pos, SIZE, avail)
    assert pos.y() + SIZE.height() <= avail.bottom()


def test_top_left_right_taskbars():
    full = QRect(0, 0, 1920, 1080)
    for avail, icon, want in (
        (QRect(0, 48, 1920, 1032), QRect(1800, 10, 24, 24), "top"),
        (QRect(60, 0, 1860, 1080), QRect(10, 900, 24, 24), "left"),
        (QRect(0, 0, 1860, 1080), QRect(1880, 900, 24, 24), "right"),
    ):
        pos, edge = anchor_position(icon, avail, full, SIZE)
        assert edge == want
        assert inside(pos, SIZE, avail)


def test_second_monitor_with_negative_coordinates():
    full = QRect(-1920, 0, 1920, 1080)
    avail = QRect(-1920, 0, 1920, 1032)
    icon = QRect(-30, 1040, 24, 24)
    pos, _ = anchor_position(icon, avail, full, SIZE)
    assert inside(pos, SIZE, avail)
    assert pos.x() < 0


def test_icon_at_screen_corner_is_clamped():
    full = QRect(0, 0, 1366, 768)
    avail = QRect(0, 0, 1366, 728)
    pos, _ = anchor_position(QRect(1360, 740, 24, 24), avail, full, SIZE)
    assert inside(pos, SIZE, avail)
    pos, _ = anchor_position(QRect(), avail, full, SIZE)       # unknown icon rect
    assert inside(pos, SIZE, avail)


def test_widget_restore():
    screens = [QRect(0, 0, 1920, 1040), QRect(1920, 0, 2560, 1400)]
    assert restore_rect([2000, 100, 420, 560], screens, None) == QRect(2000, 100, 420, 560)
    assert restore_rect([9000, 100, 420, 560], screens, None) is None   # monitor unplugged
    assert restore_rect([10, 10, 50, 50], screens, None).width() >= 300  # minimum size
    assert restore_rect("junk", screens, None) is None


@pytest.mark.parametrize("ui_scale", [100, 125, 150])
def test_panel_renders_all_tabs_without_errors(qapp, fx, ui_scale):
    from f1win.core import parsers
    from f1win.core.tracks import TrackLibrary
    from f1win.paths import resource_dir
    from f1win.services.store import DataStore
    from f1win.settings import Settings
    from f1win.ui.panel import F1Panel
    from f1win.ui.tabs.common import Ctx
    from f1win.ui.theme import Theme

    s = Settings(ui_scale=ui_scale, debug_force_tabs=True, animations=False)
    store = DataStore()
    store.race = parsers.parse_next_race(fx("next.json"))
    store.set("drivers", {**parsers.parse_driver_standings(fx("driver_standings.json")), "loaded": True})
    store.set("constructors", {**parsers.parse_constructor_standings(fx("constructor_standings.json")), "loaded": True})
    store.set("results", {**parsers.parse_results(fx("results.json")), "loaded": True})
    store.set("grid", {**parsers.parse_qualifying(fx("qualifying.json")), "loaded": True, "provisional": True})
    ctx = Ctx(store, s, Theme.from_settings(s), TrackLibrary(resource_dir() / "tracks.txt"), lambda u: None)
    p = F1Panel(ctx, "popup")
    p.resize(round(500 * ui_scale / 100), round(640 * ui_scale / 100))
    p.show()
    for tab in ("schedule", "grid", "live", "results", "drivers", "constructors"):
        p.set_view(tab, animate=False)
        qapp.processEvents()
        assert p.view == tab
        img = p.grab()
        assert not img.isNull()
        # Content must fit the panel width: no horizontal scrolling.
        assert p.scroll.widget().minimumSizeHint().width() <= p.scroll.viewport().width() + 1
    p.cycle(1)
    assert p.view == "schedule"
    p.close()


def test_popup_hides_when_focus_leaves(qapp, fx):
    from PySide6.QtCore import QEvent
    from f1win.core.tracks import TrackLibrary
    from f1win.paths import resource_dir
    from f1win.services.store import DataStore
    from f1win.settings import Settings
    from f1win.ui.popup import TrayPopup
    from f1win.ui.tabs.common import Ctx
    from f1win.ui.theme import Theme

    s = Settings(animations=False, popup_pinned=False)
    ctx = Ctx(DataStore(), s, Theme.from_settings(s), TrackLibrary(resource_dir() / "tracks.txt"), lambda u: None)
    popup = TrayPopup(ctx, s)
    popup.show()
    popup.changeEvent(QEvent(QEvent.Type.ActivationChange))   # not the active window -> must hide, not raise
    assert not popup.isVisible()
    popup.panel.set_pinned(True)
    popup.show()
    popup.changeEvent(QEvent(QEvent.Type.ActivationChange))
    assert popup.isVisible()
    popup.close()
