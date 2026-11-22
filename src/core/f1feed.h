#pragma once
// Parser for F1's live-timing SignalR stream (port of core/f1feed.py). Experimental and off by
// default, as in the original. This only parses; the network side is a service.

#include <QJsonObject>
#include <QString>
#include <optional>

namespace f1 {

class FeedState {
public:
    void reset();
    // Feed one SSE line. Returns true when the state changed. `now` stamps last_message.
    bool handleLine(const QString& line, qint64 now);
    // The live view: {"rows", "flag", "latest_sec", "session", "clock", "weather"}, or nullopt.
    std::optional<QJsonObject> view(qint64 now) const;

    QJsonObject feeds;
    qint64 lastMessage = 0;
};

}  // namespace f1
