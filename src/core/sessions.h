#pragma once
// Session maths (port of f1win/core/sessions.py).

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>

namespace f1 {

// Session ids in weekend order.
const QStringList& sessionIds();
QString sessionName(const QString& id);       // "Practice 1" ... (translated)
int durationMin(const QString& id);           // nominal running time in minutes
const QMap<QString, int>& durationTable();

constexpr qint64 LIVE_BEFORE = 15 * 60;
constexpr qint64 LIVE_AFTER = 3 * 3600;

struct Session {
    QString id;
    qint64 ts = 0;
    qint64 delay = 0;                          // minutes against the published time
    QString source = QStringLiteral("published");   // published | feed | manual

    QString name() const { return sessionName(id); }
    QJsonObject toJson() const;
    static Session fromJson(const QJsonObject& d);
    bool operator==(const Session&) const = default;
};
using Sessions = QList<Session>;

struct Race {
    QString season, round, name, circuitId, circuitName, locality, country, url;
    Sessions sessions;       // effective, sorted
    Sessions rawSessions;    // as published
    std::optional<double> lat, lon;   // circuit coordinates (weather forecast)

    QJsonObject toJson() const;
    static Race fromJson(const QJsonObject& d);
    bool isSprintWeekend() const;
    QString location() const;
};

std::optional<double> coord(const QJsonValue& v, double limit);

// Sorted session list from one Jolpica race object.
Sessions buildSessions(const QJsonValue& race);
// Published times, overridden by the timing feed, overridden by manual minutes.
Sessions effectiveSessions(const Sessions& raw, const QMap<QString, qint64>& autoTs, const QJsonObject& manualMin);

std::optional<qint64> sessionTs(const Sessions& s, const QString& id);
std::optional<Session> nextSession(const Sessions& s, qint64 now);
std::optional<Session> currentSession(const Sessions& s, qint64 now);
bool nearSession(const Sessions& s, qint64 now);
std::optional<qint64> windowEnd(const Sessions& s, qint64 now);
bool gridApplicable(const Sessions& s, qint64 now);
std::optional<std::pair<qint64, qint64>> weekendWindow(const Sessions& s);
// Live only near a session AND with a fresh position feed.
bool liveActive(const QJsonObject& live, const Sessions& s, qint64 now, bool debug = false);

}  // namespace f1
