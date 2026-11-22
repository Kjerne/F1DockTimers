#pragma once
// Spoiler-free mode: hide results until the user says they've watched (port of core/spoilers.py).
//
// A result becomes a spoiler the moment a competitive session *starts*. Pressing "Show results"
// stores the time of the reveal; everything that started before it is fair game, until the next
// competitive session starts. Practice results are not treated as spoilers.

#include "sessions.h"

#include <QJsonObject>

namespace f1::spoilers {

bool isCompetitive(const QString& sessionId);   // sprint_quali, quali, sprint, race

// Start time of the newest competitive session whose outcome may now be shown anywhere.
// `race` may be null; `results` and `lastSession` are the store's "results" / "last_session".
qint64 latestSpoilerTs(const Race* race, const QJsonObject& results, const QJsonObject& lastSession, qint64 now);

bool spoilersHidden(bool spoilerFree, double revealedAt, const Race* race, const QJsonObject& results,
                    const QJsonObject& lastSession, qint64 now);

// True when no competitive session is running right now.
bool sessionOver(const Race* race, qint64 now);

}  // namespace f1::spoilers
