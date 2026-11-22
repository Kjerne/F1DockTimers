#include "spoilers.h"

#include "pyjson.h"

namespace f1::spoilers {

bool isCompetitive(const QString& id) {
    return id == u"sprint_quali" || id == u"quali" || id == u"sprint" || id == u"race";
}

qint64 latestSpoilerTs(const Race* race, const QJsonObject& results, const QJsonObject& lastSession, qint64 now) {
    qint64 newest = 0;
    if (race)
        for (const auto& s : race->sessions)
            if (isCompetitive(s.id) && s.ts <= now) newest = std::max(newest, s.ts);
    if (py::truthy(results.value(QStringLiteral("rows"))) && py::truthy(results.value(QStringLiteral("date_ts"))))
        newest = std::max(newest, py::toInt(results.value(QStringLiteral("date_ts"))).value_or(0));
    const QJsonObject last = py::obj(lastSession.value(QStringLiteral("session")));
    const QString kind = py::str(last.value(QStringLiteral("kind"))).toLower();
    const bool hasKind = last.contains(QStringLiteral("kind"));
    if (hasKind && (kind == u"qualifying" || kind == u"race") && py::truthy(last.value(QStringLiteral("start"))))
        newest = std::max(newest, py::toInt(last.value(QStringLiteral("start"))).value_or(0));
    return newest;
}

bool spoilersHidden(bool spoilerFree, double revealedAt, const Race* race, const QJsonObject& results,
                    const QJsonObject& lastSession, qint64 now) {
    if (!spoilerFree) return false;
    const qint64 newest = latestSpoilerTs(race, results, lastSession, now);
    return newest > 0 && newest > revealedAt;
}

bool sessionOver(const Race* race, qint64 now) {
    if (race)
        for (const auto& s : race->sessions)
            if (isCompetitive(s.id) && s.ts <= now && now < s.ts + durationMin(s.id) * 60LL) return false;
    return true;
}

}  // namespace f1::spoilers
