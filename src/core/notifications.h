#pragma once
// Which Windows notifications are due (port of the pure parts of services/notifier.py): before
// race, qualifying and sprint, and the top 3 afterwards. Each is sent at most once.

#include "../settings.h"
#include "sessions.h"

#include <QJsonObject>
#include <QSet>
#include <optional>
#include <tuple>

namespace f1::notify {

inline constexpr qint64 GRACE_S = 300;
inline constexpr qint64 RESULT_FRESH_S = 6 * 3600;   // only results of sessions that ended in the last 6 hours

struct Due {
    QString key, sessionId;
    qint64 ts;
};
// Reminders that should fire now (key "season-round-session").
QList<Due> dueNotifications(const Race* race, const Settings& s, qint64 now, const QSet<QString>& sent);

struct Message {
    QString key, title, text;
};
// A fresh qualifying/sprint/race classification from the store's "last_session", else nullopt.
std::optional<Message> resultNotification(const QJsonObject& lastSession, const Settings& s, qint64 now,
                                          const QSet<QString>& sent);
// The text of a reminder, e.g. "Lights out in 9m (Sun 05:00)".
Message reminderMessage(const Race& race, const Due& due, const Settings& s, qint64 now);

}  // namespace f1::notify
