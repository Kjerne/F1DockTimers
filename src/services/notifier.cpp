#include "notifier.h"

#include "../core/pyjson.h"
#include "../core/spoilers.h"
#include "../core/timeutil.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSaveFile>
#include <algorithm>

Q_LOGGING_CATEGORY(lcNotify, "f1.notify")

namespace f1 {

Notifier::Notifier(DataStore* store, Settings* settings, Show show, QString file, QObject* parent)
    : QObject(parent), m_store(store), m_settings(settings), m_show(std::move(show)), m_file(std::move(file)) {
    QFile f(m_file);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray())
            for (const QJsonValue& v : doc.array()) m_sent.insert(py::str(v));   // str(x) as in Python
    }
    m_timer.setInterval(15000);
    connect(&m_timer, &QTimer::timeout, this, [this] { check(nowSec()); });
    connect(store, &DataStore::changed, this, &Notifier::dataChanged);
}

void Notifier::start() {
    m_timer.start();
    QTimer::singleShot(3000, this, [this] { check(nowSec()); });
}

void Notifier::check(qint64 now) {
    const Race* race = m_store->race();
    for (const auto& due : notify::dueNotifications(race, *m_settings, now, m_sent)) {
        m_sent.insert(due.key);
        const auto msg = notify::reminderMessage(*race, due, *m_settings, now);
        m_show(msg.title, msg.text);
        save();
    }
}

void Notifier::dataChanged(const QString& key) {
    if (key != u"last_session") return;
    const qint64 now = nowSec();
    if (spoilers::spoilersHidden(m_settings->spoiler_free, m_settings->spoiler_revealed_at, m_store->race(),
                                 m_store->get(QStringLiteral("results")), m_store->get(QStringLiteral("last_session")), now))
        return;   // spoiler-free: never announce who won
    const auto hit = notify::resultNotification(m_store->get(QStringLiteral("last_session")), *m_settings, now, m_sent);
    if (!hit) return;
    m_sent.insert(hit->key);
    m_show(hit->title, hit->text);
    save();
}

void Notifier::save() const {
    QStringList keep(m_sent.begin(), m_sent.end());
    std::sort(keep.begin(), keep.end());
    while (keep.size() > 100) keep.removeFirst();
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcNotify) << "could not save notification state";
        return;
    }
    f.write(QJsonDocument(QJsonArray::fromStringList(keep)).toJson(QJsonDocument::Compact));
    f.commit();
}

}  // namespace f1
