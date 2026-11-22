#include "weekend.h"

#include "pyjson.h"

#include <cstdlib>

namespace f1::weekend {

namespace {
qint64 num(const QJsonObject& o, const char* k) { return py::toInt(o.value(QLatin1StringView(k))).value_or(0); }
QString text(const QJsonObject& o, const char* k) {
    const QJsonValue v = o.value(QLatin1StringView(k));
    return v.isString() ? v.toString() : QString();
}
}  // namespace

QString sessionKind(const QJsonObject& s) {
    const QString name = text(s, "name").toLower(), type = text(s, "kind").toLower();
    if (name.contains(QStringLiteral("sprint")) && (name.contains(QStringLiteral("qualifying")) || name.contains(QStringLiteral("shootout"))))
        return QStringLiteral("sprint_quali");
    if (name == u"sprint") return QStringLiteral("sprint");
    if (name == u"qualifying" || type == u"qualifying") return QStringLiteral("quali");
    if (name == u"race") return QStringLiteral("race");
    if (type == u"race" && name.contains(QStringLiteral("sprint"))) return QStringLiteral("sprint");
    if (type == u"race") return QStringLiteral("race");
    return QStringLiteral("practice");
}

QString pillLabel(const QJsonObject& s) {
    const QString kind = sessionKind(s);
    if (kind == u"sprint_quali") return QStringLiteral("SQ");
    if (kind == u"sprint") return QStringLiteral("Sprint");
    if (kind == u"quali") return QStringLiteral("Quali");
    if (kind == u"race") return QStringLiteral("Race");
    const QString name = text(s, "name");
    if (name.startsWith(QStringLiteral("Practice "))) return QStringLiteral("FP") + name.mid(9).trimmed();
    return name.left(10);
}

std::optional<qint64> meetingForRace(const QJsonArray& raceSessions, qint64 raceTs) {
    for (const QJsonValue& v : raceSessions) {
        const QJsonObject s = v.toObject();
        if (sessionKind(s) != u"race") continue;
        const qint64 start = num(s, "start"), meeting = num(s, "meeting");
        if (meeting && start && std::llabs(start - raceTs) <= 36 * 3600) return meeting;
    }
    return std::nullopt;
}

QJsonArray startedSessions(const QJsonObject& meeting, qint64 now) {
    QJsonArray out;
    for (const QJsonValue& v : meeting.value(QStringLiteral("sessions")).toArray()) {
        const QJsonObject s = v.toObject();
        if (py::truthy(s.value(QStringLiteral("cancelled")))) continue;
        if (num(s, "start") && num(s, "start") <= now) out.append(s);
    }
    return out;
}

std::optional<qint64> nextResultToFetch(const QJsonObject& meeting, qint64 now) {
    const QJsonObject results = meeting.value(QStringLiteral("results")).toObject();
    for (const QJsonValue& v : startedSessions(meeting, now)) {
        const QJsonObject s = v.toObject();
        const qint64 key = num(s, "key"), end = num(s, "end");
        if (!key || !end || now < end + RESULTS_AFTER_S) continue;
        const QJsonObject have = results.value(QString::number(key)).toObject();
        if (have.isEmpty()) return key;
        const qint64 fetched = num(have, "fetched");
        if (fetched < end + FINAL_AFTER_S && now - fetched >= REFETCH_S) return key;
    }
    return std::nullopt;
}

bool needsListing(const QJsonObject& meeting, qint64 now) {
    const QJsonArray sessions = meeting.value(QStringLiteral("sessions")).toArray();
    if (sessions.isEmpty()) return true;
    qint64 lastEnd = 0;
    for (const QJsonValue& v : sessions) lastEnd = std::max(lastEnd, num(v.toObject(), "end"));
    return now < lastEnd + FINAL_AFTER_S && now - num(meeting, "listed") > 3600;
}

QJsonObject prune(const QJsonObject& meetings, const QList<qint64>& wanted) {
    QJsonObject out;
    for (qint64 k : wanted) {
        if (out.size() >= KEEP_MEETINGS) break;
        const QString key = QString::number(k);
        if (meetings.contains(key)) out.insert(key, meetings.value(key));
    }
    return out;
}

}  // namespace f1::weekend
