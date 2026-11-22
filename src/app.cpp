#include "app.h"

#include "core/i18n.h"
#include "core/pyjson.h"
#include "core/sanitize.h"
#include "ui/settings_dialog.h"
#include "core/sessions.h"
#include "core/timeutil.h"
#include "paths.h"
#include "services/autostart.h"
#include "services/winshell.h"
#include "services/wintrust.h"
#include "version.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalBlocker>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTabWidget>

Q_LOGGING_CATEGORY(lcApp, "f1.app")

namespace f1 {

namespace {


}  // namespace

App::App(QObject* parent) : QObject(parent) {
    // First launch ever (no settings file yet): open the panel once, so a new user sees the app is
    // running instead of only a tray icon.
    m_firstRun = !QFile::exists(paths::settingsFile());
    m_settings = Settings::load(paths::settingsFile());
    if (m_firstRun) saveSettings();   // so this happens only once
    i18n::load(m_settings.language, {paths::builtinLangDir(), paths::userLangDir()});
    m_theme = ui::Theme::fromSettings(m_settings);
    m_zone = zoneFor(m_settings.timezone);

    // The same cache.json as the Python app: an upgrade keeps everything it had fetched.
    const QString cache = QDir(paths::cacheDir()).filePath(QStringLiteral("cache.json"));
    m_store = new DataStore(cache, cache, this);
    if (m_store->loadCache()) qCInfo(lcApp) << "restored cached data from" << m_store->readFile;

    m_http = new HttpClient(this);
    m_poller = new Poller(m_store, &m_settings, m_http, this);

    m_tracks.load(QDir(paths::resourceDir()).filePath(QStringLiteral("tracks.txt")));
    m_ctx = ui::Ctx{m_store, &m_settings, &m_theme, &m_tracks, [](const QString& url) {
                        if (const auto link = safeLink(url)) QDesktopServices::openUrl(QUrl(*link));
                    }};
    m_ctx.saveSettings = [this] { saveSettings(); };
    m_popup = new ui::TrayPopup(&m_ctx, &m_settings);
    wirePanel(m_popup->panel);

    m_tray = new QSystemTrayIcon(ui::appIcon(m_theme.accent), this);
    m_tray->setToolTip(QStringLiteral(F1_APP_TITLE));
    m_menu = new QMenu();
    m_menu->addAction(T(QStringLiteral("Open Formula 1 panel")), this, [this] { openPanel(); });
    m_menu->addAction(T(QStringLiteral("Refresh data")), this, [this] { m_poller->refreshAll(true); });
    m_menu->addSeparator();
    auto checkable = [this](const char* label, bool on, void (App::*slot)(bool)) {
        auto* a = m_menu->addAction(T(QString::fromUtf8(label)));
        a->setCheckable(true);
        a->setChecked(on);
        connect(a, &QAction::toggled, this, slot);
        return a;
    };
    m_actWidget = checkable(F1_TR_NOOP("Desktop widget"), m_settings.widget_enabled, &App::showWidget);
    m_actPill = checkable(F1_TR_NOOP("Taskbar countdown"), m_settings.taskbar_pill, &App::showPill);
    m_actClick = checkable(F1_TR_NOOP("Widget click-through"), m_settings.widget_click_through, &App::setClickThrough);
    m_actTop = checkable(F1_TR_NOOP("Widget always on top"), m_settings.widget_always_on_top, &App::setOnTop);
    m_menu->addSeparator();
    m_actUpdate = m_menu->addAction(T(QStringLiteral("Install update…")), this, &App::promptUpdate);
    m_actUpdate->setVisible(false);
    if (!wintrust::isPackaged())   // the Microsoft Store updates its copy itself
        m_menu->addAction(T(QStringLiteral("Check for updates")), this, [this] { m_updater->check(true); });
    m_menu->addAction(T(QStringLiteral("Settings…")), this, &App::openSettings);
    m_actStart = checkable(F1_TR_NOOP("Start with Windows"), autostart::isEnabled(), &App::setAutostart);
    m_menu->addSeparator();
    m_menu->addAction(T(QStringLiteral("Exit")), this, &App::quit);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &App::trayActivated);
    connect(m_poller, &Poller::tick, this, &App::onTick);
    connect(m_menu, &QMenu::aboutToHide, this, [this] { m_uiSeen = true; });

    // Windows notifications, in the same notified.json as the Python app (never twice per session).
    m_notifier = new Notifier(m_store, &m_settings, [this](const QString& t, const QString& m) { notify(t, m); },
                              QDir(paths::cacheDir()).filePath(QStringLiteral("notified.json")), this);

    m_updater = new Updater(&m_settings, m_http->networkManager(), [this] { saveSettings(); }, this);
    connect(m_updater, &Updater::available, this, &App::updateAvailable);
    connect(m_updater, &Updater::checked, this, &App::updateChecked);
    connect(m_updater, &Updater::failed, this, &App::updateFailed);
    connect(m_updater, &Updater::readyToQuit, this, &App::quit);
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (!m_toastIsUpdate) return;
        m_toastIsUpdate = false;
        promptUpdate();
    });

    // Hand idle memory back to Windows, like the Python build: 20 s after start, after a menu
    // closes, and every 15 minutes while nothing is open.
    m_trimTimer.setInterval(20'000);
    connect(&m_trimTimer, &QTimer::timeout, this, &App::maybeTrim);
}

App::~App() {
    delete m_menu;
    delete m_popup;
    delete m_widget;
}

void App::notify(const QString& title, const QString& message) {
    m_toastIsUpdate = false;
    m_tray->showMessage(title, message, ui::appIcon(m_theme.accent), 15000);
}

void App::showUpdate(const QString& version) {
    m_updateVersion = version;
    m_popup->panel->setUpdate(version);
    if (m_widget) m_widget->panel->setUpdate(version);
    m_actUpdate->setText(T(QStringLiteral("Install update {version}…"), {{"version", version}}));
    m_actUpdate->setVisible(!version.isEmpty());
}

void App::updateAvailable(const QString& version) {
    if (version == m_settings.skipped_version) return;
    showUpdate(version);
    if (m_settings.update_notified != version) {
        m_settings.update_notified = version;
        saveSettings();
        m_toastIsUpdate = true;
        m_tray->showMessage(T(QStringLiteral("{app_title} {version} is available"),
                              {{"app_title", QStringLiteral(F1_APP_TITLE)}, {"version", version}}),
                            T(QStringLiteral("Click here, or use \"Install update\" in the tray menu.")),
                            ui::appIcon(m_theme.accent), 15000);
    }
}

void App::updateChecked(bool newer, const QString& message) {
    if (newer) promptUpdate();
    else QMessageBox::information(nullptr, QStringLiteral(F1_APP_TITLE), message);
}

void App::promptUpdate() {
    if (!m_updater->latest) {
        m_updater->check(true);
        return;
    }
    const update::Release rel = *m_updater->latest;
    const QUrl page(rel.url.isEmpty() ? QString::fromLatin1(update::RELEASES_PAGE) : rel.url);
    QMessageBox box;
    box.setWindowTitle(T(QStringLiteral("{app_title} update"), {{"app_title", QStringLiteral(F1_APP_TITLE)}}));
    box.setWindowIcon(ui::appIcon(m_theme.accent));
    box.setText(T(QStringLiteral("<b>Version {version}</b> is available. You have {current}."),
                  {{"version", rel.version}, {"current", QStringLiteral(F1_VERSION)}}));
    const QString notes = rel.notes.trimmed();
    box.setInformativeText(notes.isEmpty() ? QString() : notes.left(600) + (notes.size() > 600 ? QStringLiteral("…") : QString()));
    const bool can = m_updater->canInstall();
    if (!can && m_updater->kind == u"installed")   // shown, but not signed with the release key
        box.setInformativeText(box.informativeText() + (box.informativeText().isEmpty() ? QString() : QStringLiteral("\n\n")) +
                               T(QStringLiteral("This release is not signed with the F1DockTimers release key, so it is not "
                                                "installed automatically. You can download it from the release page.")));
    QPushButton* install = box.addButton(can ? T(QStringLiteral("Install and restart")) : T(QStringLiteral("Open download page")),
                                         QMessageBox::AcceptRole);
    QPushButton* pageBtn = can ? box.addButton(T(QStringLiteral("Release page")), QMessageBox::HelpRole) : nullptr;
    QPushButton* skip = box.addButton(T(QStringLiteral("Skip this version")), QMessageBox::DestructiveRole);
    box.addButton(T(QStringLiteral("Later")), QMessageBox::RejectRole);
    box.setDefaultButton(install);
    bringToFront(&box);
    box.exec();
    QAbstractButton* clicked = box.clickedButton();
    if (clicked == install) {
        if (can) startInstall();
        else QDesktopServices::openUrl(page);
    } else if (pageBtn && clicked == pageBtn) {
        QDesktopServices::openUrl(page);
    } else if (clicked == skip) {
        m_settings.skipped_version = rel.version;
        saveSettings();
        showUpdate(QString());
    }
}

void App::startInstall() {
    auto* dlg = new QProgressDialog(T(QStringLiteral("Downloading the update…")), T(QStringLiteral("Cancel")), 0, 100);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(T(QStringLiteral("{app_title} update"), {{"app_title", QStringLiteral(F1_APP_TITLE)}}));
    dlg->setMinimumDuration(0);
    dlg->setAutoClose(false);
    dlg->setAutoReset(false);
    connect(dlg, &QProgressDialog::canceled, m_updater, &Updater::cancel);
    connect(m_updater, &Updater::progress, dlg, [dlg](qint64 a, qint64 b) {
        if (b > 0) dlg->setValue(static_cast<int>(a * 100 / b));
    });
    connect(m_updater, &Updater::failed, dlg, &QWidget::close);
    connect(m_updater, &Updater::readyToQuit, dlg, &QWidget::close);
    m_progress = dlg;
    dlg->show();
    m_updater->install();
}

void App::updateFailed(const QString& message) {
    qCWarning(lcApp) << "update failed:" << message;
    QMessageBox::warning(nullptr, T(QStringLiteral("{app_title} update"), {{"app_title", QStringLiteral(F1_APP_TITLE)}}),
                         message + T(QStringLiteral("\n\nYou can download it from the release page.")));
    if (m_updater->latest)
        QDesktopServices::openUrl(QUrl(m_updater->latest->url.isEmpty() ? QString::fromLatin1(update::RELEASES_PAGE)
                                                                         : m_updater->latest->url));
}

void App::screenshots(const QString& dir, const QStringList& tabs) {
    // Dev aid for visual verification (as in the Python app): render each tab to PNG, then exit.
    m_readOnly = true;
    QDir().mkpath(dir);
    m_popup->panel->pinned = true;
    m_popup->showAt(QRect());
    auto shoot = std::make_shared<std::function<void(int)>>();
    *shoot = [this, dir, tabs, shoot](int i) {
        if (i >= tabs.size()) {
            quit();
            return;
        }
        if (tabs[i] == u"widget") {   // the desktop widget on its current tab
            showWidget(true);
            QTimer::singleShot(600, this, [this, dir, shoot, i] {
                m_widget->grab().save(QDir(dir).filePath(QStringLiteral("widget.png")));
                (*shoot)(i + 1);
            });
            return;
        }
        if (tabs[i].startsWith(QStringLiteral("settings"))) {   // "settings" or "settings:3" (page index)
            auto* d = new ui::SettingsDialog(&m_settings, m_store, m_theme, [] {});   // never saves
            d->setAttribute(Qt::WA_DeleteOnClose);
            if (auto* tw = d->findChild<QTabWidget*>()) tw->setCurrentIndex(tabs[i].section(u':', 1).toInt());
            d->show();
            QTimer::singleShot(400, this, [d, dir, tabs, shoot, i] {
                d->grab().save(QDir(dir).filePath(QString(tabs[i]).replace(u':', u'-') + QStringLiteral(".png")));
                d->close();
                (*shoot)(i + 1);
            });
            return;
        }
        m_popup->panel->setView(tabs[i], false);
        QTimer::singleShot(400, this, [this, dir, tabs, shoot, i] {
            m_popup->grab().save(QDir(dir).filePath(tabs[i] + QStringLiteral(".png")));
            (*shoot)(i + 1);
        });
    };
    auto wait = std::make_shared<std::function<void(int)>>();
    *wait = [this, shoot, wait](int n) {
        bool loaded = true;
        for (const char* k : {"drivers", "constructors", "results"})
            loaded &= m_store->get(QString::fromLatin1(k)).value(QStringLiteral("loaded")).toBool();
        if ((m_store->race() && loaded) || n > 60) QTimer::singleShot(1500, this, [shoot] { (*shoot)(0); });
        else QTimer::singleShot(500, this, [wait, n] { (*wait)(n + 1); });
    };
    (*wait)(0);
}

void App::wirePanel(ui::F1Panel* panel) {
    connect(panel, &ui::F1Panel::refreshRequested, this, [this] { m_poller->refreshAll(); });
    connect(panel, &ui::F1Panel::settingsRequested, this, &App::openSettings);
    connect(panel, &ui::F1Panel::updateClicked, this, &App::promptUpdate);
    panel->setUpdate(m_updateVersion);
}

void App::openSettings() {
    if (m_settingsDialog) {   // second click while open: the same window, in front
        bringToFront(m_settingsDialog);
        return;
    }
    if (!m_popup->panel->pinned) m_popup->hideAnimated();
    auto* d = new ui::SettingsDialog(&m_settings, m_store, m_theme, [this] { saveSettings(); });
    d->setWindowIcon(ui::appIcon(m_theme.accent));
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &ui::SettingsDialog::applied, this, &App::applySettings);
    connect(d, &ui::SettingsDialog::refreshRequested, this, [this] { m_poller->refreshAll(true); });
    connect(d, &ui::SettingsDialog::clearCacheRequested, this, [this] {
        m_store->clearCache();
        m_poller->refreshAll(true);
    });
    connect(d, &ui::SettingsDialog::checkUpdatesRequested, this, [this] { m_updater->check(true); });
    connect(d, &ui::SettingsDialog::reopenRequested, this, [this] { QTimer::singleShot(150, this, &App::openSettings); });
    m_settingsDialog = d;   // QPointer: null again once Qt deletes it
    d->show();
    bringToFront(d);
}

void App::bringToFront(QWidget* w) {
    // Windows may refuse focus to an app opened from the tray; a brief topmost toggle helps.
    if (w->isMinimized()) w->showNormal();
    w->raise();
    w->activateWindow();
    winshell::bringToFront(static_cast<quintptr>(w->winId()));
}

void App::applySettings() {
    m_theme = ui::Theme::fromSettings(m_settings);
    m_zone = zoneFor(m_settings.timezone);
    m_tray->setIcon(ui::appIcon(m_theme.accent, m_liveIcon));
    // The panel caches fonts and sizes: build a fresh one with the new theme.
    const bool wasVisible = m_popup->isVisible();
    m_popup->hide();
    m_popup->deleteLater();
    m_popup = new ui::TrayPopup(&m_ctx, &m_settings);
    wirePanel(m_popup->panel);
    if (wasVisible) openPanel();
    if (m_widget) {   // same for the desktop widget; it comes back where it was
        if (m_settings.widget_remember_position) {
            const QRect g = m_widget->geometry();
            m_settings.widget_geometry = QJsonArray{g.x(), g.y(), g.width(), g.height()};
        }
        m_widget->hide();
        m_widget->deleteLater();
        m_widget = nullptr;
    }
    if (m_settings.widget_enabled) showWidget(true);
    if (autostart::isEnabled() != m_settings.start_with_windows) autostart::setEnabled(m_settings.start_with_windows);
    m_poller->settingsChanged();
    if (m_settings.taskbar_pill != (m_pill != nullptr)) showPill(m_settings.taskbar_pill);
    else if (m_pill) m_pill->restyle();
    syncMenu();
}

void App::showPill(bool on) {
    m_settings.taskbar_pill = on;
    if (on) startPill();
    else if (m_pill) {
        m_pill->stop();
        m_pill.reset();
    }
    saveSettings();
    syncMenu();
}

void App::showWidget(bool on) {
    m_settings.widget_enabled = on;
    if (on) {
        if (!m_widget) {
            m_widget = new ui::DesktopWidget(&m_ctx);
            m_widget->setWindowIcon(ui::appIcon(m_theme.accent));
            wirePanel(m_widget->panel);
            connect(m_widget, &ui::DesktopWidget::action, this, &App::widgetAction);
        }
        m_widget->show();
        m_widget->panel->setView(m_widget->panel->view);
    } else if (m_widget) {
        m_widget->hide();
    }
    saveSettings();
    syncMenu();
}

void App::widgetAction(const QString& act) {
    Settings& s = m_settings;
    if (act == u"refresh") m_poller->refreshAll();
    else if (act == u"panel") openPanel();
    else if (act == u"settings") openSettings();
    else if (act == u"hide") showWidget(false);
    else if (act == u"toggle_lock") {
        s.widget_locked = !s.widget_locked;
        saveSettings();
    } else if (act == u"toggle_top") setOnTop(!s.widget_always_on_top);
    else if (act == u"toggle_click") {
        setClickThrough(!s.widget_click_through);
        if (s.widget_click_through)
            m_tray->showMessage(QStringLiteral(F1_APP_TITLE),
                                T(QStringLiteral("Widget is click-through. Turn it off from the tray icon menu.")),
                                ui::appIcon(m_theme.accent), 6000);
    }
}

void App::setClickThrough(bool on) {
    m_settings.widget_click_through = on;
    saveSettings();
    if (m_widget) m_widget->applySettings();
    syncMenu();
}

void App::setOnTop(bool on) {
    m_settings.widget_always_on_top = on;
    saveSettings();
    if (m_widget) m_widget->applySettings();
    syncMenu();
}

void App::setAutostart(bool on) {
    autostart::setEnabled(on);
    m_settings.start_with_windows = autostart::isEnabled();
    saveSettings();
    syncMenu();
}

void App::syncMenu() {
    const std::pair<QAction*, bool> items[] = {{m_actStart, autostart::isEnabled()},
                                               {m_actWidget, m_settings.widget_enabled},
                                               {m_actPill, m_settings.taskbar_pill},
                                               {m_actClick, m_settings.widget_click_through},
                                               {m_actTop, m_settings.widget_always_on_top}};
    for (const auto& [a, on] : items) {
        const QSignalBlocker block(a);
        a->setChecked(on);
    }
}

void App::openPanel() {
    if (!m_popup->isVisible()) m_popup->showAt(m_tray->geometry());
}

void App::start() {
    m_tray->show();
    const QStringList args = QApplication::arguments();
    const bool show = args.contains(QStringLiteral("--show"));
    const bool minimized = args.contains(QStringLiteral("--minimized"));
    if (m_settings.widget_enabled || args.contains(QStringLiteral("--widget"))) showWidget(true);
    // Start with Windows follows the setting, as in the Python app. Only an installed copy writes
    // the Run key on its own (a build folder never takes over the user's start-up entry).
    const bool autostartOn = autostart::isEnabled();
    const bool installed = m_updater->kind == u"installed";
    if (autostartOn != m_settings.start_with_windows) {
        if (m_settings.start_with_windows && installed) autostart::setEnabled(true);   // e.g. after the exe moved
        else m_settings.start_with_windows = autostartOn;
    }
    if (autostartOn && installed && autostart::currentCommand() != autostart::command(QApplication::applicationFilePath()))
        autostart::setEnabled(true);   // refresh the path (e.g. the Python build's entry)
    syncMenu();
    if (show || m_firstRun || !(m_settings.start_minimized || minimized))
        QTimer::singleShot(400, this, [this] { openPanel(); });
    m_poller->start();
    m_notifier->start();
    m_updater->start();
    m_trimTimer.start();
    if (m_settings.taskbar_pill) QTimer::singleShot(800, this, &App::startPill);
}

void App::startPill() {
    if (m_pill || !m_settings.taskbar_pill) return;
    {
        m_pill = std::make_unique<ui::TaskbarPill>(m_store, &m_settings, &m_theme, &m_zone,
                                                   [this] { return m_tray->geometry(); });
        connect(m_pill.get(), &ui::TaskbarPill::menuRequested, this, &App::showMenu);
        connect(m_pill.get(), &ui::TaskbarPill::offsetChanged, this, &App::saveSettings);
        connect(m_pill.get(), &ui::TaskbarPill::clicked, this, [this] {
            m_popup->toggle(m_pill->geometry());
        });
        m_pill->start();
    }
}

void App::saveSettings() {
    // Same file and keys as the Python app (atomic replace). Never holds the OpenF1 password.
    if (m_readOnly) return;
    if (!m_settings.save(paths::settingsFile())) qCWarning(lcApp) << "could not save settings";
}

void App::quit() {
    qCInfo(lcApp) << "exiting";
    m_poller->stop();
    saveSettings();
    if (m_pill) m_pill->stop();
    m_tray->hide();
    QApplication::quit();
}

void App::updateTray(qint64 now) {
    const Race* race = m_store->race();
    if (!race) {
        m_tray->setToolTip(QStringLiteral(F1_APP_TITLE));
        return;
    }
    const QJsonObject liveData = m_store->get(QStringLiteral("live"));
    const bool live = liveActive(liveData, race->sessions, now, m_settings.debug_force_tabs);
    if (live != m_liveIcon) {
        m_liveIcon = live;
        m_tray->setIcon(ui::appIcon(m_theme.accent, live));
    }
    const auto raceTs = sessionTs(race->sessions, QStringLiteral("race"));
    QString tip;
    if (live) {
        const QJsonArray rows = py::arr(liveData.value(QStringLiteral("rows")));
        const QString leader = rows.isEmpty() ? QString() : py::str(rows.first().toObject().value(QStringLiteral("code")));
        tip = QStringLiteral("LIVE · ") + race->name + (leader.isEmpty() ? QString() : QStringLiteral("\nLeader: ") + leader);
    } else {
        std::optional<qint64> target = raceTs;
        QString label = QStringLiteral("Race");
        if (m_settings.countdown_target == u"session")
            if (const auto next = nextSession(race->sessions, now)) {
                target = next->ts;
                label = next->name();
            }
        const QString shortText = target ? fmtShort(static_cast<double>(*target - now)) : QString();
        tip = QStringLiteral("F1 · ") + race->name + u'\n' + label + u' ' +
              (target ? fmtLong(static_cast<double>(*target - now)) : QString()) + QStringLiteral(" (") + shortText + u')';
        if (raceTs) tip += u'\n' + fmtDatetime(*raceTs, m_zone, m_settings.time_format == u"12h", true);
    }
    m_tray->setToolTip(tip.left(127));
}

void App::onTick(qint64 now) {
    if (m_popup->isVisible()) {
        m_popup->panel->tick(now);
        m_uiSeen = true;
    }
    if (m_widget && m_widget->isVisible()) m_widget->panel->tick(now);
    if (m_pill) m_pill->tick(now);
    if (now % 5 == 0) updateTray(now);
}

void App::trayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) m_popup->toggle(m_tray->geometry());
    else if (reason == QSystemTrayIcon::MiddleClick) m_poller->refreshAll();
}

void App::showMenu(const QPoint& pos) { m_menu->popup(pos); }

void App::maybeTrim() {
    if (QApplication::activePopupWidget() || QApplication::activeModalWidget() || m_popup->isVisible() || m_settingsDialog) {
        m_uiSeen = true;
        return;
    }
    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    if (m_uiSeen || now - m_trimmedAt > 15 * 60) {
        const double before = winshell::workingSetMb();
        winshell::trimWorkingSet();
        qCInfo(lcApp) << "memory trimmed:" << qRound(before) << "MB ->" << qRound(winshell::workingSetMb()) << "MB";
        m_uiSeen = false;
        m_trimmedAt = now;
    }
}

}  // namespace f1
