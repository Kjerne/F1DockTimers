#pragma once
// Time parsing and formatting (port of f1win/core/timeutil.py).
//
// All instants are integer Unix seconds (UTC). Conversion to the user's zone happens only at
// display time. A Zone of nullopt means "the Windows zone", resolved per instant for DST.

#include <QDateTime>
#include <QJsonValue>
#include <QString>
#include <QTimeZone>
#include <optional>

namespace f1 {

using Zone = std::optional<QTimeZone>;

qint64 nowSec();

QString weekdayName(int isoDay);   // 1 = Mon
QString monthName(int month);      // 1 = Jan

// Jolpica "2026-10-11" + "12:00:00Z" -> Unix seconds. Missing time = midnight UTC.
std::optional<qint64> parseUtc(const QJsonValue& date, const QJsonValue& time = QJsonValue());
// OpenF1 "2026-10-10T08:17:42.627000+00:00" -> Unix seconds.
std::optional<qint64> parseIso(const QJsonValue& text);
// F1 timing feed: local wall clock + circuit GMT offset -> Unix seconds.
std::optional<qint64> parseLocal(const QJsonValue& dateTime, const QJsonValue& gmtOffset);

QString fmtShort(double seconds);   // 4d, 3h, 12m, <1m, live
QString fmtLong(double seconds);    // "in 4d 21h 36m" / "live now"
QString fmtClock(double seconds);   // "07:45", "1:02:05"

QString systemZoneName();
// nullopt for "", "auto" and unknown names.
Zone zoneFor(const QString& name);
QDateTime toLocal(qint64 ts, const Zone& zone);

// "Fri 10:30" / "Fri 10:30 AM"; long adds the date: "Fri 20 Nov, 10:30".
QString fmtDatetime(qint64 ts, const Zone& zone, bool twelve, bool longForm = false);
QString fmtClockTime(qint64 ts, const Zone& zone, bool twelve);   // "14:00" / "2:00 PM"
QString fmtDate(qint64 ts, const Zone& zone);
QString fmtStamp(double ts, const Zone& zone, bool twelve);
QString utcOffsetLabel(qint64 ts, const Zone& zone);

}  // namespace f1
