#pragma once
// Async JSON over QNetworkAccessManager: timeouts, size cap, per-host pacing
// (port of services/http.py). Callbacks run on the GUI thread; transfers never block it.

#include "httpapi.h"
#include "ratelimit.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <deque>

class QNetworkAccessManager;
class QNetworkReply;

namespace f1 {

class HttpClient : public QObject, public HttpApi {
    Q_OBJECT
public:
    explicit HttpClient(QObject* parent = nullptr);
    void getJson(const QString& url, Ok ok, Fail fail, const Headers& headers = {}, int timeoutMs = 15000) override;
    void postForm(const QString& url, const QByteArray& body, Ok ok, Fail fail, int timeoutMs = 15000) override;
    void setOpenf1Plan(const QString& plan) override;
    bool isOffline() const override { return offline; }
    QNetworkAccessManager* nam() const { return m_nam; }
    QNetworkAccessManager* networkManager() const override { return m_nam; }

    bool offline = false;
    int inflight = 0;

private:
    struct Item {
        bool post = false;
        QString url;
        QByteArray body;
        Headers headers;
        int timeoutMs;
        Ok ok;
        Fail fail;
    };
    void drain();
    void start(const Item& item);
    void done(QNetworkReply* reply, const Item& item);
    double mono() const { return m_clock.elapsed() / 1000.0; }

    QNetworkAccessManager* m_nam;
    std::deque<Item> m_queue;
    RateLimiter m_limiter;
    QTimer m_pump;
    QElapsedTimer m_clock;
};

}  // namespace f1
