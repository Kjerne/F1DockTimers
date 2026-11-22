#include "f1stream.h"

#include "../core/timeutil.h"
#include "version.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

Q_LOGGING_CATEGORY(lcStream, "f1.stream")

namespace f1 {

namespace {
constexpr qsizetype MAX_BUFFER = 4 * 1024 * 1024;
const char RS = '\x1e';

QJsonArray feeds() {
    return {QStringLiteral("SessionInfo"), QStringLiteral("SessionData"), QStringLiteral("TimingData"),
            QStringLiteral("DriverList"),  QStringLiteral("TrackStatus"), QStringLiteral("LapCount"),
            QStringLiteral("ExtrapolatedClock"), QStringLiteral("WeatherData")};
}
}  // namespace

F1Stream::F1Stream(QNetworkAccessManager* nam, QString base, QObject* parent)
    : QObject(parent), m_nam(nam), m_base(std::move(base)) {
    m_clock.start();
    m_emit.setSingleShot(true);
    m_emit.setInterval(500);   // coalesce bursts of updates
    connect(&m_emit, &QTimer::timeout, this, &F1Stream::updated);
}

QNetworkRequest F1Stream::request(const QString& url) const {
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", F1_USER_AGENT);
    return req;
}

void F1Stream::ensureRunning() {
    if (m_running || mono() < m_retryAt) return;
    m_running = true;
    const int gen = ++m_gen;
    QNetworkRequest req = request(m_base + QStringLiteral("/negotiate?negotiateVersion=1"));
    req.setTransferTimeout(15000);
    // Same header Qt picks by default (and the Python client sends), set explicitly.
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/x-www-form-urlencoded"));
    QNetworkReply* reply = m_nam->post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen] { negotiated(reply, gen); });
    qCInfo(lcStream) << "connecting";
}

void F1Stream::negotiated(QNetworkReply* reply, int gen) {
    const QByteArray data = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    if (gen != m_gen) return;
    QString token;
    if (ok) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) token = doc.object().value(QStringLiteral("connectionToken")).toString();
    }
    bool safe = !token.isEmpty();
    for (QChar c : token) safe &= c.isLetterOrNumber() || c == u'-' || c == u'_';
    if (!safe) {
        closed(60);
        return;
    }
    m_token = token;
    QNetworkRequest req = request(m_base + QStringLiteral("?id=") + token);
    req.setRawHeader("Accept", "text/event-stream");
    req.setTransferTimeout(90000);
    m_buffer.clear();
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this, gen] { read(gen); });
    connect(m_reply, &QNetworkReply::finished, this, [this, gen] { streamFinished(gen); });
    QTimer::singleShot(1000, this, [this, gen] { send(gen, R"({"protocol":"json","version":1})"); });
    QJsonArray args;
    args.append(feeds());   // [[...feeds]]: a nested list (QJsonArray{array} would copy, not nest)
    const QJsonObject sub{{QStringLiteral("arguments"), args},
                          {QStringLiteral("invocationId"), QStringLiteral("1")},
                          {QStringLiteral("target"), QStringLiteral("Subscribe")},
                          {QStringLiteral("type"), 1}};
    const QByteArray subText = QJsonDocument(sub).toJson(QJsonDocument::Compact);
    QTimer::singleShot(2000, this, [this, gen, subText] { send(gen, subText); });
}

void F1Stream::send(int gen, const QByteArray& text) {
    if (gen != m_gen || m_token.isEmpty()) return;
    QNetworkRequest req = request(m_base + QStringLiteral("?id=") + m_token);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("text/plain;charset=UTF-8"));
    req.setTransferTimeout(15000);
    QNetworkReply* r = m_nam->post(req, text + RS);
    connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
}

void F1Stream::read(int gen) {
    if (gen != m_gen || !m_reply) return;
    m_buffer += m_reply->readAll();
    if (m_buffer.size() > MAX_BUFFER) {
        stop();
        closed(60);
        return;
    }
    bool changed = false;
    qsizetype nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (state.handleLine(QString::fromUtf8(line), nowSec())) changed = true;
    }
    if (changed && !m_emit.isActive()) m_emit.start();
}

void F1Stream::streamFinished(int gen) {
    if (gen != m_gen) return;
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    closed(15);
}

void F1Stream::closed(int retrySeconds) {
    m_running = false;
    m_retryAt = mono() + retrySeconds;
    if (retrySeconds >= 60) {
        qCInfo(lcStream) << "stream unavailable; retrying in" << retrySeconds << "s";
        Q_EMIT failed(QStringLiteral("net"));
    }
}

void F1Stream::stop() {
    if (!m_running && !m_reply) return;
    ++m_gen;
    m_running = false;
    m_retryAt = 0;
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
    state.reset();
}

}  // namespace f1
