#pragma once
// Team colours and flag names (port of f1win/core/teams.py).

#include <QString>

namespace f1 {

inline constexpr const char* TEAM_FALLBACK = "#7A7D85";

QString teamColor(const QString& constructorId, const QString& fallback = QString::fromLatin1(TEAM_FALLBACK));
QString friendlyFlag(const QString& flag);

}  // namespace f1
