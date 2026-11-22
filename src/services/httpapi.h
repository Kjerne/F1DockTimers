#pragma once
// What the poller needs from the network, so tests can hand it a fake (the Python tests do the
// same with duck-typed Http classes).

#include <QByteArray>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <functional>
#include <utility>

class QNetworkAccessManager;

namespace f1 {

using Headers = QList<std::pair<QByteArray, QByteArray>>;

class HttpApi {
public:
    using Ok = std::function<void(const QJsonValue&)>;
    using Fail = std::function<void(const QString& reason, int status)>;
    virtual ~HttpApi() = default;
    virtual void getJson(const QString& url, Ok ok, Fail fail, const Headers& headers = {}, int timeoutMs = 15000) = 0;
    virtual void postForm(const QString& url, const QByteArray& body, Ok ok, Fail fail, int timeoutMs = 15000) = 0;
    virtual void setOpenf1Plan(const QString& plan) = 0;
    virtual bool isOffline() const = 0;
    // The shared network manager (cookie jar) for the F1 stream; null in fakes without one.
    virtual QNetworkAccessManager* networkManager() const { return nullptr; }
};

}  // namespace f1
