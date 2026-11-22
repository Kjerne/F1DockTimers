#include "http.h"

#include "../core/sanitize.h"
#include "version.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <algorithm>
#include <optional>

Q_LOGGING_CATEGORY(lcHttp, "f1.http")

namespace f1 {

HttpClient::HttpClient(QObject* parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)) {
    m_clock.start();
    m_pump.setSingleShot(true);
    connect(&m_pump, &QTimer::timeout, this, &HttpClient::drain);
}

void HttpClient::getJson(const QString& url, Ok ok, Fail fail, const Headers& headers, int timeoutMs) {
    m_queue.push_back(Item{false, url, {}, headers, timeoutMs, std::move(ok), std::move(fail)});
    drain();
}

void HttpClient::postForm(const QString& url, const QByteArray& body, Ok ok, Fail fail, int timeoutMs) {
    const Headers h{{"Content-Type", "application/x-www-form-urlencoded"}};
    m_queue.push_back(Item{true, url, body, h, timeoutMs, std::move(ok), std::move(fail)});
    drain();
}

void HttpClient::setOpenf1Plan(const QString& plan) { m_limiter.limits[QStringLiteral("api.openf1.org")] = openf1Plan(plan); }

void HttpClient::drain() {
    // Start every queued request whose host is allowed now; a throttled host never holds up
    // requests to another one, and order is kept per host.
    const double now = mono();
    std::optional<double> soonest;
    std::deque<Item> kept;
    while (!m_queue.empty()) {
        Item item = std::move(m_queue.front());
        m_queue.pop_front();
        const QString host = QUrl(item.url).host();
        const double wait = m_limiter.wait(host, now);
        const bool hostQueued = std::any_of(kept.begin(), kept.end(), [&](const Item& k) { return QUrl(k.url).host() == host; });
        if (wait > 0 || hostQueued) {
            if (wait > 0) soonest = soonest ? std::min(*soonest, wait) : wait;
            kept.push_back(std::move(item));
            continue;
        }
        m_limiter.note(host, now);
        start(item);
    }
    m_queue = std::move(kept);
    if (!m_queue.empty()) {
        const double delay = soonest.value_or(0.05);
        if (!m_pump.isActive() || m_pump.remainingTime() > delay * 1000) m_pump.start(static_cast<int>(delay * 1000) + 5);
    }
}

void HttpClient::start(const Item& item) {
    QNetworkRequest req{QUrl(item.url)};
    req.setRawHeader("User-Agent", F1_USER_AGENT);
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(item.timeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto& [k, v] : item.headers) req.setRawHeader(k, v);   // never logged (may hold a token)
    QNetworkReply* reply = item.post ? m_nam->post(req, item.body) : m_nam->get(req);
    ++inflight;
    qCDebug(lcHttp) << (item.post ? "POST" : "GET") << item.url.section(u'?', 0, 0);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > MAX_BODY_BYTES) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, item] { done(reply, item); });
}

void HttpClient::done(QNetworkReply* reply, const Item& item) {
    --inflight;
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto err = reply->error();
    const QByteArray raw = reply->readAll();
    const QString safeUrl = item.url.section(u'?', 0, 0);   // never log query strings
    if (err != QNetworkReply::NoError && status == 0) {
        const QString reason = err == QNetworkReply::OperationCanceledError ? QStringLiteral("timeout") : QStringLiteral("net");
        offline = err == QNetworkReply::HostNotFoundError || err == QNetworkReply::UnknownNetworkError ||
                  err == QNetworkReply::TemporaryNetworkFailureError || err == QNetworkReply::NetworkSessionFailedError;
        qCInfo(lcHttp) << "request failed" << reason << safeUrl;
        item.fail(reason, 0);
        return;
    }
    offline = false;
    if (status == 429) {
        bool ok = false;
        double retry = QString::fromLatin1(reply->rawHeader("Retry-After")).toDouble(&ok);
        if (!ok || retry <= 0) retry = 10.0;
        m_limiter.block(QUrl(item.url).host(), mono() + std::clamp(retry, 1.0, 600.0));
    }
    if (status != 200) {
        qCInfo(lcHttp) << "request failed http" << status << safeUrl;
        item.fail(QStringLiteral("http %1").arg(status), status);
        return;
    }
    QByteArray data = raw;
    if (data.isEmpty() || data.size() > MAX_BODY_BYTES) {
        item.fail(QStringLiteral("size"), status);
        return;
    }
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError || doc.isNull()) {
        item.fail(QStringLiteral("parse"), status);
        return;
    }
    item.ok(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()));
}

}  // namespace f1
