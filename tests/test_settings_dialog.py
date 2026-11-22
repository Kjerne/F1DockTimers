"""Regression: Settings must open again after it was closed (dialog deleted by Qt)."""
from types import SimpleNamespace

from PySide6.QtCore import QCoreApplication, QEvent


def test_settings_reopens_after_close(qapp):
    from f1win.app import F1App
    from f1win.services.store import DataStore
    from f1win.settings import Settings
    from f1win.ui.theme import Theme

    s = Settings()
    app = SimpleNamespace(settings_dialog=None, settings=s, store=DataStore(), theme=Theme.from_settings(s),
                          popup=SimpleNamespace(panel=SimpleNamespace(pinned=True)),
                          poller=SimpleNamespace(refresh_all=lambda force=False: None),
                          apply_settings=lambda: None, _clear_cache=lambda: None)
    app._settings_closed = lambda: F1App._settings_closed(app)
    app._bring_to_front = lambda w: None

    for _ in range(3):
        F1App.open_settings(app)
        d = app.settings_dialog
        assert d is not None and d.isVisible()
        F1App.open_settings(app)            # second click while open: same window
        assert app.settings_dialog is d
        d.close()
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
        qapp.processEvents()
        assert app.settings_dialog is None

    # Even with a stale reference left behind, opening must still work.
    F1App.open_settings(app)
    d = app.settings_dialog
    d.destroyed.disconnect()
    d.finished.disconnect()
    d.close()
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
    qapp.processEvents()
    F1App.open_settings(app)
    assert app.settings_dialog.isVisible()
    app.settings_dialog.close()


def test_live_data_choice_maps_to_settings(qapp, monkeypatch):
    from f1win.services.store import DataStore
    from f1win.settings import Settings
    from f1win.ui.settings_dialog import TIPS, SettingsDialog
    from f1win.ui.theme import Theme

    for mode, source, plan in (("free", "openf1", "free"), ("combo", "f1_feed", "free"),
                               ("sponsor", "openf1", "sponsor")):
        s = Settings()
        monkeypatch.setattr(s, "save", lambda *a, **k: None)
        d = SettingsDialog(s, DataStore(), Theme.from_settings(s))
        d.live_mode.setCurrentIndex(d.live_mode.findData(mode))
        assert d.user.isEnabled() == (mode == "sponsor")
        d._save()
        assert (s.live_source, s.openf1_plan) == (source, plan)
        # and it reopens on the same choice
        d2 = SettingsDialog(s, DataStore(), Theme.from_settings(s))
        assert d2.live_mode.currentData() == mode
    # every tooltip points at a real control
    assert all(getattr(d2, k, None) is not None for k in TIPS)


def test_long_tooltips_wrap(qapp):
    from PySide6.QtWidgets import QLabel
    from f1win.ui.widgets import wrap_tooltips
    wrap_tooltips(qapp)
    w = QLabel("x")
    w.setToolTip("Short line\n" + "word " * 40)
    lines = w.toolTip().split("\n")
    assert lines[0] == "Short line" and max(len(x) for x in lines) <= 70 and len(lines) > 2
