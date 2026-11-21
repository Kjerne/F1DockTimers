"""Embedded taskbar mode: the pill becomes a child window of Shell_TrayWnd (Windows only)."""
import sys

import pytest
from PySide6.QtCore import QRect

pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="Windows taskbar only")


def make_pill(tmp_path, **kw):
    from f1win.services.store import DataStore
    from f1win.settings import Settings
    from f1win.ui.tabs.common import Ctx
    from f1win.ui.taskbar_pill import TaskbarPill
    from f1win.ui.theme import Theme
    s = Settings(taskbar_pill=True, check_updates=False, **kw)
    ctx = Ctx(DataStore(tmp_path / "c.json"), s, Theme(), None, lambda *_: None)
    return TaskbarPill(ctx, s, lambda: QRect()), s


def has_taskbar() -> bool:
    from f1win.services import taskbar_embed
    return bool(taskbar_embed.find_taskbar())


def test_embedded_mode_lives_in_the_taskbar_and_falls_back(qapp, tmp_path):
    if not has_taskbar():
        pytest.skip("no taskbar in this session")
    import ctypes
    from ctypes import wintypes
    u = ctypes.windll.user32
    u.GetParent.restype = wintypes.HWND
    pill, s = make_pill(tmp_path, pill_embed=True)
    try:
        pill.start()
        if pill._embed_failed:          # e.g. a CI desktop whose taskbar refuses child windows
            assert pill.isVisible()     # ...then the floating window takes over
            pytest.skip("taskbar refused the child window")
        assert pill.embedded and not pill.isVisible() and pill.is_shown()
        hwnd = pill._embed.hwnd
        assert int(u.GetParent(hwnd) or 0) == pill._embed.bar
        assert u.IsWindowVisible(hwnd)
        # Explorer restart: our window disappears; the guard must create a new one.
        ctypes.windll.user32.DestroyWindow(hwnd)
        assert not pill._embed.alive()
        pill._guard()
        assert pill.embedded and pill._embed.alive() and pill._embed.hwnd != hwnd
        # Turning the option off goes back to the floating window.
        s.pill_embed = False
        pill.restyle()
        assert not pill.embedded and pill.isVisible()
        s.pill_embed = True
        pill.restyle()
        assert pill.embedded and not pill.isVisible()
    finally:
        pill.stop()
    assert not pill.embedded


def test_floating_mode_unchanged(qapp, tmp_path):
    pill, _ = make_pill(tmp_path, pill_embed=False)
    try:
        pill.start()
        assert not pill.embedded and pill.isVisible()
    finally:
        pill.stop()
