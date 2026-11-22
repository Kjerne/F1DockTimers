#pragma once
// Pure parsers: API JSON -> plain JSON rows, cacheable and UI-ready (port of core/parsers.py).
// Nothing here invents data: missing fields become empty strings.

#include "sessions.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <optional>
#include <utility>

namespace f1::parsers {

QString driverCode(const QJsonValue& driver);
QString driverName(const QJsonValue& driver);

// ── Jolpica ──
std::optional<Race> parseRace(const QJsonValue& r);
std::optional<Race> parseNextRace(const QJsonValue& data);
// From a season calendar, the first race whose start is still ahead (or live).
std::optional<Race> firstFutureRace(const QJsonValue& data, qint64 now);
QJsonObject parseDriverStandings(const QJsonValue& data);
QJsonObject parseConstructorStandings(const QJsonValue& data);
// `entries` is "Results" for a race, "SprintResults" for a sprint.
QJsonObject parseResults(const QJsonValue& data, const char* entries = "Results");
QJsonObject parseQualifying(const QJsonValue& data);
// Official starting grid from a finished race's `grid` field (0 = pit lane).
QJsonArray gridFromResults(const QJsonObject& results);

// ── F1 timing feed ──
inline constexpr qint64 MAX_FEED_SHIFT = 12 * 3600;
// "Practice 1" -> "fp1" ... ("" when unknown)
QString feedSessionId(const QString& name);
// SessionInfo.json -> (session id, start ts) if it belongs to `race`.
std::optional<std::pair<QString, qint64>> parseSessionInfo(const QJsonValue& info, const Race* race);

// ── OpenF1 ──
// driver number -> {"acr", "color", "team"}
using Drivers = QMap<QString, QJsonObject>;
Drivers parseOpenf1Drivers(const QJsonValue& arr);
// (rows, newest position timestamp)
std::pair<QJsonArray, qint64> parseOpenf1Positions(const QJsonValue& arr, const Drivers& drivers);
void applyOpenf1Intervals(QJsonArray& rows, const QJsonValue& arr);
QString parseOpenf1Flag(const QJsonValue& arr);
QStringList parseOpenf1RaceControl(const QJsonValue& arr, int limit = 4);
std::optional<QJsonObject> parseOpenf1Session(const QJsonValue& arr);
// Every session in an OpenF1 sessions answer (same fields as above), by start time; at most 40.
QJsonArray parseOpenf1Sessions(const QJsonValue& arr);
QJsonArray parseOpenf1StartingGrid(const QJsonValue& arr, const Drivers& drivers, const QJsonArray& provisional);
std::optional<QJsonObject> summariseOpenf1Weather(const QJsonValue& arr);
// `kind` is the OpenF1 session_type ("Practice", "Qualifying", "Race").
QJsonArray parseOpenf1SessionResult(const QJsonValue& arr, const Drivers& drivers, const QString& kind);

// 71.717 -> "1:11.717"; 5723.504 -> "1:35:23.504"; "" for nothing.
QString fmtLap(const QJsonValue& seconds);

}  // namespace f1::parsers
