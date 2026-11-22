#pragma once
// Every session of a race weekend with its classification, for the session pills in the Live and
// Last tabs. Pure rules; the poller fetches (OpenF1 session lists and results, Jolpica sprint and
// qualifying) and the tabs show.
//
// Store key "weekends": {
//   "meetings": { "<meeting_key>": { "location": str, "listed": ts,
//                                    "sessions": [ OpenF1 session objects, by start ],
//                                    "drivers": { "<number>": {acr, color, team, ...} },
//                                    "results": { "<session_key>": {"rows": [...], "fetched": ts} } } },
//   "rounds": { "<season>-<round>": <meeting_key> }      (Jolpica's last race -> OpenF1 meeting)
// }

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace f1::weekend {

inline constexpr qint64 RESULTS_AFTER_S = 20 * 60;    // OpenF1 publishes a classification ~20 min after the end
inline constexpr qint64 FINAL_AFTER_S = 3 * 3600;     // fetched this long after the end: penalties are in, keep it
inline constexpr qint64 REFETCH_S = 3600;             // an early classification is fetched again after this
inline constexpr int KEEP_MEETINGS = 3;

// Short label for a session pill: FP1, FP2, FP3, SQ, Sprint, Quali, Race.
QString pillLabel(const QJsonObject& session);
// "race" | "quali" | "sprint" | "sprint_quali" | "practice" from OpenF1's session name and type.
QString sessionKind(const QJsonObject& session);

// The meeting whose race starts within a day and a half of `raceTs` (from an OpenF1
// "sessions?year=Y&session_name=Race" answer, already parsed), if any.
std::optional<qint64> meetingForRace(const QJsonArray& raceSessions, qint64 raceTs);

// Sessions of a meeting that started by `now`, not cancelled, in order.
QJsonArray startedSessions(const QJsonObject& meeting, qint64 now);

// The session key whose classification should be fetched next, if any: finished more than
// RESULTS_AFTER_S ago and either never fetched, or fetched early (before FINAL_AFTER_S) more than
// REFETCH_S ago.
std::optional<qint64> nextResultToFetch(const QJsonObject& meeting, qint64 now);

// Whether the meeting's session list should be fetched (again): never listed, or the weekend is
// still running and the list is more than an hour old (times and cancellations change).
bool needsListing(const QJsonObject& meeting, qint64 now);

// Keeps only the wanted meetings (newest first), at most KEEP_MEETINGS.
QJsonObject prune(const QJsonObject& meetings, const QList<qint64>& wanted);

}  // namespace f1::weekend
