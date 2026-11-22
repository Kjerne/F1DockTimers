#pragma once
// Shared helpers for the Qt Test suites (the conftest.py equivalent).

#include "core/parsers.h"
#include "core/pyjson.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QTest>

using namespace Qt::Literals::StringLiterals;

namespace t {

inline QJsonValue fx(const char* name) {
    QFile f(QStringLiteral(F1_FIXTURES) + QLatin1StringView(name));
    if (!f.open(QIODevice::ReadOnly)) qFatal("missing fixture %s", name);
    QByteArray data = f.readAll();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

inline qint64 utc(int y, int mo, int d, int h = 0, int mi = 0, int s = 0) {
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s), QTimeZone::UTC).toSecsSinceEpoch();
}

inline f1::Race nextRace() {
    auto r = f1::parsers::parseNextRace(fx("next.json"));
    if (!r) qFatal("next.json did not parse");
    return *r;
}

inline f1::Race seasonRound(const char* round) {
    for (const QJsonValue& v : fx("season.json")[QStringLiteral("MRData")][QStringLiteral("RaceTable")][QStringLiteral("Races")].toArray())
        if (v[QStringLiteral("round")].toString() == QLatin1StringView(round)) return *f1::parsers::parseRace(v);
    qFatal("no round %s", round);
    return {};
}

inline qint64 ts(const f1::Race& r, const char* id) { return f1::sessionTs(r.sessions, QLatin1StringView(id)).value_or(-1); }

}  // namespace t
