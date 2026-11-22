#pragma once
// Windows notifications before race, qualifying and sprint, and the top 3 afterwards (port of
// services/notifier.py). Delivered as native toasts through QSystemTrayIcon::showMessage by the
// app. Each (season, round, session) is notified at most once, also across restarts, in the same
// notified.json the Python app keeps (so the two builds never announce the same session twice).

#include "../core/notifications.h"
#include "store.h"

#include <QObject>
#include <QTimer>
#include <functional>

namespace f1 {

class Notifier : public QObject {
    Q_OBJECT
public:
    using Show = std::function<void(const QString& title, const QString& message)>;
    Notifier(DataStore* store, Settings* settings, Show show, QString file, QObject* parent = nullptr);
    void start();
    void check(qint64 now);
    const QSet<QString>& sent() const { return m_sent; }

private:
    void dataChanged(const QString& key);
    void save() const;

    DataStore* m_store;
    Settings* m_settings;
    Show m_show;
    QString m_file;
    QSet<QString> m_sent;
    QTimer m_timer;
};

}  // namespace f1
