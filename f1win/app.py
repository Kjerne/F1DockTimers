"""Application entry: tray icon, popup, desktop widget, single instance."""
from __future__ import annotations

import argparse
import getpass
import hashlib
import logging
import os
import sys
from logging.handlers import RotatingFileHandler
from pathlib import Path

from PySide6.QtCore import QRect, Qt, QTimer
from PySide6.QtGui import QAction, QDesktopServices, QGuiApplication
from PySide6.QtNetwork import QLocalServer, QLocalSocket
from PySide6.QtWidgets import QApplication, QMenu, QSystemTrayIcon

from .core.sanitize import safe_link
from .core.sessions import live_active, next_session, session_ts
from .core.timeutil import fmt_datetime, fmt_long, fmt_short, now_sec
from .core.tracks import TrackLibrary
from .paths import cache_dir, resource_dir
from .services import autostart, packaging
from .services.notifier import Notifier
from .services.poller import Poller
from .services.store import DataStore
from .services.updater import RELEASES_PAGE, Updater
from .settings import Settings
from .ui.desktop_widget import DesktopWidget
from .ui.popup import TrayPopup
from .ui.settings_dialog import SettingsDialog
from .ui.taskbar_pill import TaskbarPill
from .ui.tabs.common import Ctx
from .ui.theme import Theme
from .ui.widgets import app_icon
from .version import APP_TITLE, VERSION
from .i18n import T

log = logging.getLogger("f1win")


def server_name() -> str:
    key = getpass.getuser()
    if os.environ.get("NOCTALIA_F1_HOME"):          # a separate test home runs side by side
        key += "|" + os.environ["NOCTALIA_F1_HOME"]
    user = hashlib.sha1(key.encode("utf-8", "replace")).hexdigest()[:10]
    return f"F1DockTimers-{user}"


def apply_dark_palette(qapp: QApplication) -> None:
    """Fusion + the Noctalia palette: native Windows styling ignores parts of a dark
    stylesheet (white combo pop-ups, odd arrows), Fusion follows it consistently."""
    from PySide6.QtGui import QColor, QPalette
    qapp.setStyle("Fusion")
    pal = QPalette()
    colors = {
        QPalette.ColorRole.Window: "#1B1B29", QPalette.ColorRole.WindowText: "#E4E1EC",
        QPalette.ColorRole.Base: "#24243A", QPalette.ColorRole.AlternateBase: "#2A2A40",
        QPalette.ColorRole.Text: "#E4E1EC", QPalette.ColorRole.Button: "#2A2A40",
        QPalette.ColorRole.ButtonText: "#E4E1EC", QPalette.ColorRole.Highlight: "#BCC2FF",
        QPalette.ColorRole.HighlightedText: "#262C67", QPalette.ColorRole.ToolTipBase: "#24243A",
        QPalette.ColorRole.ToolTipText: "#E4E1EC", QPalette.ColorRole.PlaceholderText: "#8F8F9E",
        QPalette.ColorRole.Link: "#BCC2FF", QPalette.ColorRole.Mid: "#46464F", QPalette.ColorRole.Dark: "#141420",
    }
    for role, c in colors.items():
        pal.setColor(role, QColor(c))
    pal.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Text, QColor("#6E6E7A"))
    pal.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.ButtonText, QColor("#6E6E7A"))
    pal.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.WindowText, QColor("#6E6E7A"))
    qapp.setPalette(pal)


def setup_logging(debug: bool) -> None:
    handler = RotatingFileHandler(cache_dir() / "f1.log", maxBytes=512_000, backupCount=2, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s"))
    root = logging.getLogger()
    root.addHandler(handler)
    root.setLevel(logging.DEBUG if debug else logging.INFO)
    if debug and sys.stderr is not None:
        root.addHandler(logging.StreamHandler())

    def hook(exc_type, exc, tb):
        # In the windowed exe there is no console: errors in Qt slots would vanish.
        logging.getLogger("f1win").error("uncaught error", exc_info=(exc_type, exc, tb))

    sys.excepthook = hook


class F1App:
    def __init__(self, qapp: QApplication, args) -> None:
        self.qapp = qapp
        self.args = args
        # First launch ever (no settings file yet): open the panel once, so a new user sees the app
        # is running instead of only a tray icon.
        self.first_run = not Settings.file().exists()
        self.settings = Settings.load()
        if self.first_run:
            self.settings.save()          # so this happens only once
        self.store = DataStore()
        if self.store.load_cache():
            log.info("restored cached data")
        self.tracks = TrackLibrary(resource_dir() / "tracks.txt")
        self.theme = Theme.from_settings(self.settings)
        self.poller = Poller(self.store, self.settings)
        self.settings_dialog: SettingsDialog | None = None

        self.popup = TrayPopup(self._ctx(), self.settings)
        self._wire_panel(self.popup.panel)
        self.widget: DesktopWidget | None = None
        self.pill: TaskbarPill | None = None

        self.tray = QSystemTrayIcon(app_icon(self.theme.accent))
        self.tray.setToolTip(APP_TITLE)
        self.tray.activated.connect(self._tray_activated)
        self._build_menu()
        self.tray.show()
        self._live_icon = False

        self.notifier = Notifier(self.store, self.settings, self._notify)
        self.poller.tick.connect(self._tick)
        self.poller.start()
        self.notifier.start()

        # Hand idle memory back to Windows: 20 s after start, shortly after the panel, a menu or
        # Settings closes, and every 15 minutes while nothing is open (Task Manager ~55 MB -> <10 MB).
        self._ui_seen, self._trimmed_at = True, 0.0
        self._trim_timer = QTimer()          # F1App is not a QObject; kept alive by this reference
        self._trim_timer.setInterval(20_000)
        self._trim_timer.timeout.connect(self._maybe_trim)
        self._trim_timer.start()

        self.updater = Updater(self.settings)
        self.updater.available.connect(self._update_available)
        self.updater.checked.connect(self._update_checked)
        self.updater.failed.connect(self._update_failed)
        self.updater.ready_to_quit.connect(self.quit)
        self.tray.messageClicked.connect(self._toast_clicked)
        self._progress = None
        self.updater.start()

        if self.settings.taskbar_pill:
            QTimer.singleShot(800, lambda: self.show_pill(True))
        if self.settings.widget_enabled or args.widget:
            self.show_widget(True)
        autostart_on = autostart.is_enabled()
        if autostart_on != self.settings.start_with_windows:
            if self.settings.start_with_windows:
                autostart.set_enabled(True)  # e.g. after the exe moved
            else:
                self.settings.start_with_windows = autostart_on
        if autostart_on:
            autostart.set_enabled(True)      # refresh the path
        if args.show or self.first_run or not (self.settings.start_minimized or args.minimized):
            QTimer.singleShot(400, self.open_panel)

    # ── plumbing ────────────────────────────────────────────────────────────

    def _ctx(self) -> Ctx:
        return Ctx(self.store, self.settings, self.theme, self.tracks, self.open_link)

    def _wire_panel(self, panel) -> None:
        panel.update_clicked.connect(self.prompt_update)
        if getattr(self, "updater", None) is not None and self.updater.latest:
            panel.set_update(self.updater.latest["version"])
        panel.refresh_requested.connect(lambda: self.poller.refresh_all())
        panel.settings_requested.connect(self.open_settings)

    def open_link(self, url: str) -> None:
        link = safe_link(url)
        if link:
            from PySide6.QtCore import QUrl
            QDesktopServices.openUrl(QUrl(link))

    # ── updates ─────────────────────────────────────────────────────────────

    def _panels(self):
        return [self.popup.panel] + ([self.widget.panel] if self.widget else [])

    def _update_available(self, rel: dict) -> None:
        v = rel["version"]
        if v == self.settings.skipped_version:
            return
        for panel in self._panels():
            panel.set_update(v)
        self.act_update.setText(T('Install update {version}…', version=v))
        self.act_update.setVisible(True)
        if self.settings.update_notified != v:
            self.settings.update_notified = v
            self.settings.save()
            self._toast_is_update = True
            self.tray.showMessage(T('{app_title} {version} is available', app_title=APP_TITLE, version=v),
                                  T('Click here, or use "Install update" in the tray menu.'),
                                  app_icon(self.theme.accent), 15000)

    def _toast_clicked(self) -> None:
        if getattr(self, "_toast_is_update", False):
            self._toast_is_update = False
            self.prompt_update()

    def _update_checked(self, newer: bool, message: str) -> None:
        if newer:
            self.prompt_update()
        else:
            from PySide6.QtWidgets import QMessageBox
            QMessageBox.information(None, APP_TITLE, message)

    def prompt_update(self) -> None:
        from PySide6.QtCore import QUrl
        from PySide6.QtWidgets import QMessageBox
        rel = self.updater.latest
        if rel is None:
            self.updater.check(manual=True)
            return
        box = QMessageBox()
        box.setWindowTitle(T('{app_title} update', app_title=APP_TITLE))
        box.setWindowIcon(app_icon(self.theme.accent))
        box.setText(T('<b>Version {version}</b> is available. You have {current}.', version=rel['version'], current=VERSION))
        notes = rel["notes"].strip()
        box.setInformativeText((notes[:600] + ("…" if len(notes) > 600 else "")) if notes else "")
        can = self.updater.can_install()
        install = box.addButton(T('Install and restart') if can else T('Open download page'),
                                QMessageBox.ButtonRole.AcceptRole)
        page = box.addButton(T('Release page'), QMessageBox.ButtonRole.HelpRole) if can else None
        skip = box.addButton(T('Skip this version'), QMessageBox.ButtonRole.DestructiveRole)
        box.addButton(T('Later'), QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(install)
        self._bring_to_front(box)
        box.exec()
        clicked = box.clickedButton()
        if clicked is install:
            if can:
                self._start_install()
            else:
                QDesktopServices.openUrl(QUrl(rel["url"] or RELEASES_PAGE))
        elif page is not None and clicked is page:
            QDesktopServices.openUrl(QUrl(rel["url"] or RELEASES_PAGE))
        elif clicked is skip:
            self.settings.skipped_version = rel["version"]
            self.settings.save()
            for panel in self._panels():
                panel.set_update("")
            self.act_update.setVisible(False)

    def _start_install(self) -> None:
        from PySide6.QtWidgets import QProgressDialog
        dlg = QProgressDialog(T('Downloading the update…'), T('Cancel'), 0, 100)
        dlg.setWindowTitle(T('{app_title} update', app_title=APP_TITLE))
        dlg.setMinimumDuration(0)
        dlg.setAutoClose(False)
        dlg.setAutoReset(False)
        dlg.canceled.connect(self.updater.cancel)
        self.updater.progress.connect(lambda a, b: dlg.setValue(int(a * 100 / b)) if b else None)
        self.updater.failed.connect(dlg.close)
        self.updater.ready_to_quit.connect(dlg.close)
        self._progress = dlg
        dlg.show()
        self.updater.install()

    def _update_failed(self, message: str) -> None:
        from PySide6.QtCore import QUrl
        from PySide6.QtWidgets import QMessageBox
        log.warning("update failed: %s", message)
        QMessageBox.warning(None, T('{app_title} update', app_title=APP_TITLE), message + T('\n\nYou can download it from the release page.'))
        if self.updater.latest:
            QDesktopServices.openUrl(QUrl(self.updater.latest["url"] or RELEASES_PAGE))

    def _notify(self, title: str, message: str) -> None:
        self.tray.showMessage(title, message, app_icon(self.theme.accent), 15000)

    # ── tray ────────────────────────────────────────────────────────────────

    def _build_menu(self) -> None:
        m = QMenu()
        m.addAction(T('Open Formula 1 panel'), self.open_panel)
        m.addAction(T('Refresh data'), lambda: self.poller.refresh_all(force=True))
        m.addSeparator()
        self.act_widget = QAction(T('Desktop widget'), m, checkable=True)
        self.act_widget.setChecked(self.settings.widget_enabled)
        self.act_widget.toggled.connect(self.show_widget)
        m.addAction(self.act_widget)
        self.act_pill = QAction(T('Taskbar countdown'), m, checkable=True)
        self.act_pill.setChecked(self.settings.taskbar_pill)
        self.act_pill.toggled.connect(self.show_pill)
        m.addAction(self.act_pill)
        self.act_click = QAction(T('Widget click-through'), m, checkable=True)
        self.act_click.setChecked(self.settings.widget_click_through)
        self.act_click.toggled.connect(self._set_click_through)
        m.addAction(self.act_click)
        self.act_top = QAction(T('Widget always on top'), m, checkable=True)
        self.act_top.setChecked(self.settings.widget_always_on_top)
        self.act_top.toggled.connect(self._set_on_top)
        m.addAction(self.act_top)
        m.addSeparator()
        self.act_update = QAction(T('Install update…'), m)
        self.act_update.triggered.connect(self.prompt_update)
        self.act_update.setVisible(False)
        m.addAction(self.act_update)
        if not packaging.is_packaged():          # the Microsoft Store updates its copy itself
            m.addAction(T('Check for updates'), lambda: self.updater.check(manual=True))
        m.addAction(T('Settings…'), self.open_settings)
        self.act_start = QAction(T('Start with Windows'), m, checkable=True)
        self.act_start.setChecked(autostart.is_enabled())
        self.act_start.toggled.connect(self._set_autostart)
        m.addAction(self.act_start)
        m.addSeparator()
        m.addAction(T('Exit'), self.quit)
        self.menu = m
        self.tray.setContextMenu(m)

    def _sync_menu(self) -> None:
        for act, val in ((self.act_widget, self.settings.widget_enabled),
                         (self.act_pill, self.settings.taskbar_pill),
                         (self.act_click, self.settings.widget_click_through),
                         (self.act_top, self.settings.widget_always_on_top),
                         (self.act_start, autostart.is_enabled())):
            act.blockSignals(True)
            act.setChecked(val)
            act.blockSignals(False)

    def _tray_activated(self, reason) -> None:
        if reason in (QSystemTrayIcon.ActivationReason.Trigger, QSystemTrayIcon.ActivationReason.DoubleClick):
            self.popup.toggle(self.tray.geometry())
        elif reason == QSystemTrayIcon.ActivationReason.MiddleClick:
            self.poller.refresh_all()

    def open_panel(self) -> None:
        if not self.popup.isVisible():
            self.popup.show_at(self.tray.geometry())
        else:
            self.popup.raise_()
            self.popup.activateWindow()

    def _tick(self, now: int) -> None:
        self.popup.panel.tick(now) if self.popup.isVisible() else None
        if self.widget is not None and self.widget.isVisible():
            self.widget.panel.tick(now)
        if self.pill is not None and self.pill.is_shown():
            self.pill.tick(now)
        if now % 5 == 0:
            self._update_tray(now)

    def _update_tray(self, now: int) -> None:
        race = self.store.race
        if race is None:
            self.tray.setToolTip(APP_TITLE)
            return
        live = live_active(self.store.get("live"), race.sessions, now, self.settings.debug_force_tabs)
        if live != self._live_icon:
            self._live_icon = live
            self.tray.setIcon(app_icon(self.theme.accent, live))
        z, twelve = self.Ctx_zone(), self.settings.time_format == "12h"
        race_ts = session_ts(race.sessions, "race")
        if live:
            rows = self.store.get("live").get("rows") or []
            leader = rows[0].get("code", "") if rows else ""
            tip = f"LIVE · {race.name}" + (f"\nLeader: {leader}" if leader else "")
        else:
            target = race_ts
            label = "Race"
            if self.settings.countdown_target == "session":
                nxt = next_session(race.sessions, now)
                if nxt:
                    target, label = nxt.ts, nxt.name
            short = fmt_short(target - now) if target else ""
            tip = f"F1 · {race.name}\n{label} {fmt_long(target - now) if target else ''} ({short})"
            if race_ts:
                tip += f"\n{fmt_datetime(race_ts, z, twelve, long=True)}"
        self.tray.setToolTip(tip[:127])

    def Ctx_zone(self):
        from .core.timeutil import zone_for
        return zone_for(self.settings.timezone)

    # ── taskbar pill ────────────────────────────────────────────────────────

    def show_pill(self, on: bool) -> None:
        self.settings.taskbar_pill = on
        if on:
            if self.pill is None:
                self.pill = TaskbarPill(self._ctx(), self.settings, self.tray.geometry)
                self.pill.clicked.connect(self._pill_clicked)
                self.pill.menu_requested.connect(lambda pos: self.menu.popup(pos))
                self.pill.offset_changed.connect(lambda _o: self.settings.save())
            self.pill.start()
        elif self.pill is not None:
            self.pill.stop()
        self.settings.save()
        self._sync_menu()

    def _pill_clicked(self) -> None:
        self.popup.toggle(self.pill.geometry())

    # ── widget ──────────────────────────────────────────────────────────────

    def show_widget(self, on: bool) -> None:
        self.settings.widget_enabled = on
        if on:
            if self.widget is None:
                self.widget = DesktopWidget(self._ctx(), self.settings)
                self._wire_panel(self.widget.panel)
                self.widget.action.connect(self._widget_action)
            self.widget.show()
            self.widget.panel.set_view(self.widget.panel.view)
        elif self.widget is not None:
            self.widget.hide()
        self.settings.save()
        self._sync_menu()

    def _widget_action(self, act: str) -> None:
        s = self.settings
        if act == "refresh":
            self.poller.refresh_all()
        elif act == "panel":
            self.open_panel()
        elif act == "settings":
            self.open_settings()
        elif act == "hide":
            self.show_widget(False)
        elif act == "toggle_lock":
            s.widget_locked = not s.widget_locked
            s.save()
        elif act == "toggle_top":
            self._set_on_top(not s.widget_always_on_top)
        elif act == "toggle_click":
            self._set_click_through(not s.widget_click_through)
            if s.widget_click_through:
                self.tray.showMessage(APP_TITLE, T('Widget is click-through. Turn it off from the tray icon menu.'),
                                      app_icon(self.theme.accent), 6000)

    def _set_click_through(self, on: bool) -> None:
        self.settings.widget_click_through = on
        self.settings.save()
        if self.widget is not None:
            self.widget.apply_settings()
        self._sync_menu()

    def _set_on_top(self, on: bool) -> None:
        self.settings.widget_always_on_top = on
        self.settings.save()
        if self.widget is not None:
            self.widget.apply_settings()
        self._sync_menu()

    def _set_autostart(self, on: bool) -> None:
        autostart.set_enabled(on, interactive=True)
        self.settings.start_with_windows = autostart.is_enabled()
        self.settings.save()
        self._sync_menu()

    # ── settings ────────────────────────────────────────────────────────────

    def _maybe_trim(self) -> None:
        import time as _time
        from .services import memory
        busy = (self.popup.isVisible() or self.settings_dialog is not None
                or QApplication.activePopupWidget() is not None or QApplication.activeModalWidget() is not None)
        if busy:
            self._ui_seen = True
            return
        now = _time.monotonic()
        if self._ui_seen or now - self._trimmed_at > 15 * 60:
            memory.trim()
            self._ui_seen, self._trimmed_at = False, now

    def _settings_closed(self) -> None:
        self.settings_dialog = None

    def open_settings(self) -> None:
        d = self.settings_dialog
        if d is not None:
            try:
                if d.isVisible():
                    self._bring_to_front(d)
                    return
            except RuntimeError:  # already deleted by Qt (closed earlier)
                pass
            self.settings_dialog = None
        if not self.popup.panel.pinned:
            self.popup.hide_animated()
        d = SettingsDialog(self.settings, self.store, self.theme)
        d.setWindowIcon(app_icon(self.theme.accent))
        d.applied.connect(self.apply_settings)
        d.refresh_requested.connect(lambda: self.poller.refresh_all(force=True))
        d.clear_cache_requested.connect(self._clear_cache)
        d.reopen_requested.connect(lambda: QTimer.singleShot(150, self.open_settings))
        d.check_updates_requested.connect(lambda: self.updater.check(manual=True))
        d.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        d.finished.connect(self._settings_closed)
        d.destroyed.connect(self._settings_closed)
        self.settings_dialog = d
        d.show()
        self._bring_to_front(d)

    def _bring_to_front(self, w) -> None:
        """Windows may refuse focus to an app opened from the tray or taskbar pill;
        a brief topmost toggle puts the window in front anyway."""
        from .services import winshell
        if w.isMinimized():
            w.showNormal()
        w.raise_()
        w.activateWindow()
        winshell.bring_to_front(int(w.winId()))

    def _clear_cache(self) -> None:
        self.store.clear_cache()
        self.poller.refresh_all(force=True)

    def apply_settings(self) -> None:
        s = self.settings
        if autostart.is_enabled() != s.start_with_windows:
            autostart.set_enabled(s.start_with_windows, interactive=True)
        self.theme = Theme.from_settings(s)
        self.tray.setIcon(app_icon(self.theme.accent, self._live_icon))
        for panel in [self.popup.panel] + ([self.widget.panel] if self.widget else []):
            panel.ctx.theme = self.theme
            panel.build_chrome()
        self.popup.panel.bg_opacity = s.popup_opacity / 100.0
        self.popup.apply_size()
        self.poller.settings_changed()
        if self.pill is not None:
            self.pill.ctx.theme = self.theme
        if s.taskbar_pill != bool(self.pill and self.pill.is_shown()):
            self.show_pill(s.taskbar_pill)
        elif self.pill is not None:
            self.pill.restyle()
        if s.widget_enabled != bool(self.widget and self.widget.isVisible()):
            self.show_widget(s.widget_enabled)
        if self.widget is not None:
            self.widget.apply_settings()
        self._sync_menu()

    # ── lifecycle ───────────────────────────────────────────────────────────

    def handle_message(self, msg: str) -> None:
        if msg == "show":
            self.open_panel()

    def quit(self) -> None:
        log.info("exiting")
        self.poller.stop()
        if self.pill is not None:
            self.pill.stop()          # embedded: leave the taskbar as we found it
        self.settings.save()
        self.tray.hide()
        self.qapp.quit()


def screenshots(app: F1App, out: Path, tabs: list[str]) -> None:
    """Dev aid for visual verification: render each tab to PNG, then exit."""
    out.mkdir(parents=True, exist_ok=True)
    panel = app.popup.panel
    panel.pinned = True
    app.popup.place(QRect())
    app.popup.show()

    def shoot(i=0):
        if i >= len(tabs):
            app.quit()
            return
        panel.set_view(tabs[i], animate=False)
        QTimer.singleShot(400, lambda: (app.popup.grab().save(str(out / f"{tabs[i]}.png")), shoot(i + 1)))

    def wait(n=0):
        loaded = all(app.store.get(k).get("loaded") for k in ("drivers", "constructors", "results"))
        if (app.store.race is not None and loaded) or n > 60:
            QTimer.singleShot(1500, shoot)
        else:
            QTimer.singleShot(500, lambda: wait(n + 1))

    wait()


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="F1DockTimers", description=APP_TITLE)
    p.add_argument("--minimized", action="store_true", help="start in the tray without opening the panel")
    p.add_argument("--show", action="store_true", help="open the panel at start")
    p.add_argument("--widget", action="store_true", help="show the desktop widget")
    p.add_argument("--debug", action="store_true")
    p.add_argument("--screenshots", metavar="DIR", help=argparse.SUPPRESS)
    p.add_argument("--tabs", default="schedule,grid,live,results,drivers,constructors", help=argparse.SUPPRESS)
    p.add_argument("--version", action="version", version=VERSION)
    p.add_argument("--wait-pid", type=int, default=0, help=argparse.SUPPRESS)
    p.add_argument("--replaces", default="", help=argparse.SUPPRESS)
    args = p.parse_args(argv)

    os.environ.setdefault("QT_ENABLE_HIGHDPI_SCALING", "1")
    QGuiApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    qapp = QApplication(sys.argv[:1])
    qapp.setApplicationName("F1DockTimers")
    qapp.setApplicationDisplayName(APP_TITLE)
    qapp.setApplicationVersion(VERSION)
    qapp.setQuitOnLastWindowClosed(False)
    setup_logging(args.debug)

    if args.wait_pid:
        # Started by the in-app updater: let the old version exit first.
        from .services.updater import wait_for_pid
        wait_for_pid(args.wait_pid)
    name = server_name()
    if not args.screenshots:
        sock = QLocalSocket()
        sock.connectToServer(name)
        if sock.waitForConnected(400):
            sock.write(b"show")
            sock.flush()
            sock.waitForBytesWritten(400)
            sock.disconnectFromServer()
            return 0
    if not QSystemTrayIcon.isSystemTrayAvailable():
        log.warning("system tray not available")

    apply_dark_palette(qapp)
    app = F1App(qapp, args)
    qapp.setWindowIcon(app_icon(app.theme.accent))

    server = None
    if not args.screenshots:
        QLocalServer.removeServer(name)
        server = QLocalServer()
        server.setSocketOptions(QLocalServer.SocketOption.UserAccessOption)
        server.listen(name)

        def on_conn():
            c = server.nextPendingConnection()
            c.readyRead.connect(lambda: app.handle_message(bytes(c.readAll()).decode(errors="ignore")))
            c.disconnected.connect(c.deleteLater)

        server.newConnection.connect(on_conn)
    else:
        if args.debug:
            app.settings.debug_force_tabs = True
        screenshots(app, Path(args.screenshots), args.tabs.split(","))

    qapp.aboutToQuit.connect(app.poller.stop)
    if args.replaces:
        import threading
        from .services.updater import remove_replaced
        threading.Thread(target=remove_replaced, args=(args.replaces,), daemon=True).start()
    log.info("%s %s started", APP_TITLE, VERSION)
    return qapp.exec()
