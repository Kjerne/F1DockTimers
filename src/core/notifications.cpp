#include "notifications.h"

#include "i18n.h"
#include "pyjson.h"
#include "timeutil.h"

namespace f1::notify {

QList<Due> dueNotifications(const Race* race, const Settings& s, qint64 now, const QSet<QString>& sent) {
    QList<Due> out;
    if (!race) return out;
    const qint64 lead = std::max(0, std::min(120, s.notify_lead_min)) * 60LL;
    const std::pair<const char*, bool> kinds[] = {{"race", s.notify_race}, {"quali", s.notify_quali}, {"sprint", s.notify_sprint}};
    for (const auto& [sid, on] : kinds) {
        if (!on) continue;
        const auto ts = sessionTs(race->sessions, QString::fromLatin1(sid));
        if (!ts) continue;
        const QString key = race->season + u'-' + race->round + u'-' + QString::fromLatin1(sid);
        if (sent.contains(key)) continue;
        if (*ts - lead <= now && now < *ts + GRACE_S) out.append({key, QString::fromLatin1(sid), *ts});
    }
    return out;
}

std::optional<Message> resultNotification(const QJsonObject& data, const Settings& s, qint64 now, const QSet<QString>& sent) {
    if (!s.notify_results) return std::nullopt;
    const QJsonObject sess = py::obj(data.value(QStringLiteral("session")));
    const QJsonArray rows = py::arr(data.value(QStringLiteral("rows")));
    const QString kind = py::str(sess.value(QStringLiteral("kind"))).toLower();
    if (rows.isEmpty() || !(kind == u"qualifying" || kind == u"race") || !py::truthy(sess.value(QStringLiteral("key")))) return std::nullopt;
    const QString key = QStringLiteral("result-") + py::str(sess.value(QStringLiteral("key")));
    const qint64 end = py::toInt(sess.value(QStringLiteral("end"))).value_or(0);
    if (sent.contains(key) || !end || now - end > RESULT_FRESH_S) return std::nullopt;
    QStringList top;
    for (const QJsonValue& rv : rows) {
        const QJsonObject r = rv.toObject();
        const QString pos = py::str(r.value(QStringLiteral("pos")));
        if (!py::isDigits(pos)) continue;
        top << pos + QStringLiteral(". ") + (r.contains(QStringLiteral("code")) ? py::str(r.value(QStringLiteral("code"))) : QString());
        if (top.size() == 3) break;
    }
    if (top.isEmpty()) return std::nullopt;
    QString where = py::truthy(sess.value(QStringLiteral("location"))) ? py::str(sess.value(QStringLiteral("location")))
                                                                        : (py::truthy(sess.value(QStringLiteral("circuit"))) ? py::str(sess.value(QStringLiteral("circuit"))) : QString());
    const QString name = sess.contains(QStringLiteral("name")) ? py::str(sess.value(QStringLiteral("name"))) : QString();
    QString title = T(QStringLiteral("{where} · {get} results"), {{"where", where}, {"get", name}});
    // .strip(" ·")
    while (!title.isEmpty() && (title.front() == u' ' || title.front() == QChar(0x00B7))) title.remove(0, 1);
    while (!title.isEmpty() && (title.back() == u' ' || title.back() == QChar(0x00B7))) title.chop(1);
    return Message{key, title, top.join(QStringLiteral("   "))};
}

Message reminderMessage(const Race& race, const Due& due, const Settings& s, qint64 now) {
    const QString when = fmtDatetime(due.ts, zoneFor(s.timezone), s.time_format == u"12h");
    const QString name = sessionName(due.sessionId);
    const QString left = fmtLong(static_cast<double>(due.ts - now));
    const QString text = due.sessionId == u"race"
                             ? T(QStringLiteral("Lights out {fmt_long} ({when})"), {{"fmt_long", left}, {"when", when}})
                             : T(QStringLiteral("{name} starts {fmt_long} ({when})"), {{"name", name}, {"fmt_long", left}, {"when", when}});
    return Message{due.key, race.name + QStringLiteral(" · ") + name, text};
}

}  // namespace f1::notify
