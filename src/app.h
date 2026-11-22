#pragma once
// Application: tray icon, popup, desktop widget, taskbar countdown, notifications, poller
// (port of f1win/app.py).

#include "services/http.h"
#include "services/notifier.h"
#include "services/poller.h"
#include "services/updater.h"
#include "services/store.h"
#include "settings.h"
#include "ui/desktop_widget.h"
#include "ui/popup.h"
#include "ui/taskbar_pill.h"
#include "ui/theme.h"

#include <QDialog>
#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <memory>

class QAction;
class QProgressDialog;
class QMenu;

namespace f1 {

class App : public QObject {
    Q_OBJECT
public:
    explicit App(QObject* parent = nullptr);
    ~App() override;
    void start();
    void quit();
    void openPanel();
    void saveSettings();
    void openSettings();
    void applySettings();
    void screenshots(const QString& dir, const QStringList& tabs);
    void showWidget(bool on);
    void showPill(bool on);

private:
    void onTick(qint64 now);
    void trayActivated(QSystemTrayIcon::ActivationReason reason);
    void showMenu(const QPoint& pos);
    void syncMenu();
    void maybeTrim();
    void wirePanel(ui::F1Panel* panel);
    void startPill();
    void widgetAction(const QString& act);
    void setClickThrough(bool on);
    void setOnTop(bool on);
    void setAutostart(bool on);
    void bringToFront(QWidget* w);
    void updateTray(qint64 now);
    void notify(const QString& title, const QString& message);
    void updateAvailable(const QString& version);
    void updateChecked(bool newer, const QString& message);
    void promptUpdate();
    void startInstall();
    void updateFailed(const QString& message);
    void showUpdate(const QString& version);   // footer links + tray menu entry

    Settings m_settings;
    TrackLibrary m_tracks;
    ui::Ctx m_ctx{};
    ui::TrayPopup* m_popup = nullptr;
    ui::DesktopWidget* m_widget = nullptr;
    QPointer<QDialog> m_settingsDialog;
    ui::Theme m_theme;
    Zone m_zone;
    DataStore* m_store;
    HttpClient* m_http;
    Poller* m_poller;
    Notifier* m_notifier;
    Updater* m_updater;
    QPointer<QProgressDialog> m_progress;
    QString m_updateVersion;
    bool m_toastIsUpdate = false;
    QSystemTrayIcon* m_tray;
    QMenu* m_menu;
    QAction* m_actWidget;
    QAction* m_actPill;
    QAction* m_actClick;
    QAction* m_actTop;
    QAction* m_actUpdate;
    QAction* m_actStart;
    std::unique_ptr<ui::TaskbarPill> m_pill;
    QTimer m_trimTimer;
    double m_trimmedAt = 0;
    bool m_uiSeen = true;
    bool m_liveIcon = false;
    bool m_firstRun = false;
    bool m_readOnly = false;   // --screenshots: never writes settings.json
};

}  // namespace f1
