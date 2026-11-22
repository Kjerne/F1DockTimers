#pragma once
// Experimental F1 live-timing stream: SignalR Core over server-sent events (port of
// services/f1stream.py). The load balancer's cookie is kept by QNetworkAccessManager's cookie jar.
//
// UNOFFICIAL AND UNDOCUMENTED. Off by default; the user opts in under Settings > Live data. It may
// stop working or be restricted by F1 at any time, and F1's terms may not allow automated use.

#include "../core/f1feed.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace f1 {

inline constexpr const char* F1_STREAM_BASE = "https://livetiming.formula1.com/signalrcore";

class F1Stream : public QObject {
    Q_OBJECT
public:
    // `base` is the SignalR endpoint (tests point it at a local server).
    explicit F1Stream(QNetworkAccessManager* nam, QString base = QString::fromLatin1(F1_STREAM_BASE), QObject* parent = nullptr);
    void ensureRunning();
    void stop();
    bool running() const { return m_running; }

    FeedState state;

Q_SIGNALS:
    void updated();                    // coalesced: at most every 500 ms
    void failed(const QString& error); // "net" when the stream could not be (re)opened

private:
    QNetworkRequest request(const QString& url) const;
    void negotiated(QNetworkReply* reply, int gen);
    void send(int gen, const QByteArray& text);
    void read(int gen);
    void streamFinished(int gen);
    void closed(int retrySeconds);
    double mono() const { return m_clock.elapsed() / 1000.0; }

    QNetworkAccessManager* m_nam;
    QString m_base;
    QPointer<QNetworkReply> m_reply;
    bool m_running = false;
    double m_retryAt = 0;
    QByteArray m_buffer;
    QString m_token;
    int m_gen = 0;
    QTimer m_emit;
    QElapsedTimer m_clock;
};

}  // namespace f1
