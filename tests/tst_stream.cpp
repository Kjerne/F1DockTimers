// The experimental F1 live-timing stream client (services/f1stream) against a local fake SignalR
// server: negotiate, the event stream, the handshake and subscribe posts, reconnect rules, and the
// poller wiring for live_source = f1_feed. The Python suite has no test for this client.
#include "common.h"
#include "core/timeutil.h"
#include "services/f1stream.h"
#include "services/http.h"
#include "services/poller.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

using namespace f1;

namespace {

// Just enough HTTP/1.1 for the stream: one request per connection.
class FakeSignalR : public QObject {
public:
    QTcpServer server;
    QString token = QStringLiteral("abc-123_XYZ");
    QList<QByteArray> posts;          // bodies sent to ?id=<token>
    QList<QByteArray> requests;       // request lines
    QPointer<QTcpSocket> stream;      // the open event-stream connection
    bool failNegotiate = false;

    FakeSignalR() {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = server.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
                    *buf += s->readAll();
                    const qsizetype end = buf->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray head = buf->left(end);
                    qsizetype length = 0;
                    for (const QByteArray& line : head.split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
                    if (buf->size() < end + 4 + length) return;
                    handle(s, head, buf->mid(end + 4, length));
                    buf->clear();
                });
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/signalrcore").arg(server.serverPort()); }

    void handle(QTcpSocket* s, const QByteArray& head, const QByteArray& body) {
        const QByteArray line = head.left(head.indexOf('\r'));
        requests << line;
        if (line.startsWith("POST") && line.contains("/negotiate")) {
            const QByteArray json = failNegotiate ? QByteArray("{}") : "{\"connectionToken\":\"" + token.toUtf8() + "\"}";
            s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(json.size()) +
                     "\r\nConnection: close\r\n\r\n" + json);
            s->disconnectFromHost();
        } else if (line.startsWith("GET")) {
            s->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n");
            stream = s;
        } else {
            posts << body;
            s->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            s->disconnectFromHost();
        }
    }
    void push(const QJsonObject& msg) {
        if (stream) stream->write("data: " + QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\x1e\r\n");
    }
};

QJsonObject snapshot() {
    return {{"type", 3},
            {"result",
             QJsonObject{{"DriverList", QJsonObject{{"1", QJsonObject{{"Tla", "VER"}, {"TeamName", "Red Bull"}}},
                                                    {"4", QJsonObject{{"Tla", "NOR"}, {"TeamName", "McLaren"}}}}},
                         {"TimingData", QJsonObject{{"Lines", QJsonObject{{"1", QJsonObject{{"Line", 1}, {"GapToLeader", ""}}},
                                                                          {"4", QJsonObject{{"Line", 2}, {"GapToLeader", "+0.5"}}}}}}},
                         {"SessionInfo", QJsonObject{{"Name", "Race"}, {"Type", "Race"}}},
                         {"TrackStatus", QJsonObject{{"Message", "Yellow"}}},
                         {"WeatherData", QJsonObject{{"AirTemp", "17.0"}, {"TrackTemp", "24.0"}, {"Rainfall", "0"}}}}}};
}

// A network for the poller that has a real QNetworkAccessManager but answers JSON calls itself.
class StreamHttp : public HttpApi {
public:
    QNetworkAccessManager nam;
    void getJson(const QString&, Ok, Fail fail, const Headers&, int) override { fail(QStringLiteral("net"), 0); }
    void postForm(const QString&, const QByteArray&, Ok, Fail fail, int) override { fail(QStringLiteral("net"), 0); }
    void setOpenf1Plan(const QString&) override {}
    bool isOffline() const override { return false; }
    QNetworkAccessManager* networkManager() const override { return const_cast<QNetworkAccessManager*>(&nam); }
};

}  // namespace

class TestStream : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // Port of test_live_names.py: rows the feed only knows by number ("#6") get the code, colour
    // and team the app already has; a name the feed has is never overwritten.
    void numbersGetNamesFromKnownDrivers() {
        QJsonArray rows{QJsonObject{{"num", "6"}, {"code", "#6"}, {"color", ""}, {"team", ""}},
                        QJsonObject{{"num", "1"}, {"code", "VER"}, {"color", "#3671C6"}, {"team", "Red Bull Racing"}},
                        QJsonObject{{"num", "99"}, {"code", "#99"}, {"color", ""}, {"team", ""}}};
        const QMap<QString, QJsonObject> known{
            {QStringLiteral("6"), {{"code", "HAD"}, {"color", "#6692FF"}, {"team", "Racing Bulls"}}},
            {QStringLiteral("1"), {{"code", "XXX"}, {"color", "#000000"}, {"team", "Other"}}}};
        fillDriverNames(rows, known);
        QCOMPARE(rows[0].toObject(), (QJsonObject{{"num", "6"}, {"code", "HAD"}, {"color", "#6692FF"}, {"team", "Racing Bulls"}}));
        QCOMPARE(rows[1].toObject().value("code"_L1).toString(), QStringLiteral("VER"));   // the feed's own name wins
        QCOMPARE(rows[1].toObject().value("team"_L1).toString(), QStringLiteral("Red Bull Racing"));
        QCOMPARE(rows[2].toObject().value("code"_L1).toString(), QStringLiteral("#99"));   // unknown number stays
    }
    void knownPlaceholderIsNotUsed() {
        QJsonArray rows{QJsonObject{{"num", "6"}, {"code", "#6"}, {"color", ""}, {"team", ""}}};
        fillDriverNames(rows, {{QStringLiteral("6"), {{"code", "#6"}}}});
        QCOMPARE(rows[0].toObject().value("code"_L1).toString(), QStringLiteral("#6"));
    }
    void connectsSubscribesAndParses() {
        FakeSignalR srv;
        QNetworkAccessManager nam;
        F1Stream stream(&nam, srv.base());
        QSignalSpy updated(&stream, &F1Stream::updated);
        stream.ensureRunning();
        QVERIFY(stream.running());
        QTRY_VERIFY_WITH_TIMEOUT(srv.stream, 5000);
        QVERIFY(srv.requests.last().contains("?id=abc-123_XYZ"));
        // Handshake after 1 s, subscribe after 2 s, each ending in the record separator.
        QTRY_COMPARE_WITH_TIMEOUT(srv.posts.size(), 2, 5000);
        QCOMPARE(srv.posts[0], QByteArray("{\"protocol\":\"json\",\"version\":1}\x1e"));
        const QJsonObject sub = QJsonDocument::fromJson(srv.posts[1].chopped(1)).object();
        QCOMPARE(sub["target"_L1].toString(), QStringLiteral("Subscribe"));
        const QJsonArray feeds = sub["arguments"_L1][0].toArray();
        QCOMPARE(feeds.size(), 8);
        QVERIFY(feeds.contains(QJsonValue(QStringLiteral("TimingData"))) && feeds.contains(QJsonValue(QStringLiteral("WeatherData"))));
        srv.push(snapshot());
        QTRY_VERIFY_WITH_TIMEOUT(updated.count() >= 1, 5000);   // coalesced update
        const auto view = stream.state.view(nowSec());
        QVERIFY(view);
        QCOMPARE((*view)["rows"_L1][0]["code"_L1].toString(), QStringLiteral("VER"));
        QCOMPARE((*view)["flag"_L1].toString(), QStringLiteral("YELLOW"));
        QCOMPARE((*view)["weather"_L1]["air"_L1].toDouble(), 17.0);
        stream.stop();
        QVERIFY(!stream.running());
        QVERIFY(!stream.state.view(nowSec()));   // stop forgets the session
    }

    void unsafeOrMissingTokenFailsAndWaits() {
        FakeSignalR srv;
        srv.failNegotiate = true;
        QNetworkAccessManager nam;
        F1Stream stream(&nam, srv.base());
        QSignalSpy failed(&stream, &F1Stream::failed);
        stream.ensureRunning();
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("net"));
        QVERIFY(!stream.running());
        const int before = static_cast<int>(srv.requests.size());
        stream.ensureRunning();   // within the 60 s back-off: no new attempt
        QTest::qWait(200);
        QCOMPARE(static_cast<int>(srv.requests.size()), before);
        srv.token = QStringLiteral("bad token!");   // characters outside [A-Za-z0-9-_] are refused too
    }

    void streamEndReconnectsQuietly() {
        FakeSignalR srv;
        QNetworkAccessManager nam;
        F1Stream stream(&nam, srv.base());
        QSignalSpy failed(&stream, &F1Stream::failed);
        stream.ensureRunning();
        QTRY_VERIFY_WITH_TIMEOUT(srv.stream, 5000);
        srv.stream->disconnectFromHost();   // the server closes the stream
        QTRY_VERIFY_WITH_TIMEOUT(!stream.running(), 5000);
        QCOMPARE(failed.count(), 0);         // a normal close retries after 15 s without an error
    }

    void pollerRunsTheStreamForF1Feed() {
        FakeSignalR srv;
        QTemporaryDir dir;
        DataStore store(dir.filePath(QStringLiteral("c.json")), QString());
        store.setRace(t::nextRace());
        Settings settings;
        settings.live_source = QStringLiteral("f1_feed");
        settings.debug_force_tabs = true;   // "near a session" without waiting for one
        StreamHttp http;
        Poller poller(&store, &settings, &http);
        poller.streamBase = srv.base();
        poller.update();
        QVERIFY(poller.stream() && poller.stream()->running());
        QTRY_VERIFY_WITH_TIMEOUT(srv.stream, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(srv.posts.size(), 2, 5000);
        srv.push(snapshot());
        QTRY_VERIFY_WITH_TIMEOUT(!store.get(QStringLiteral("live"))["rows"_L1].toArray().isEmpty(), 5000);
        const QJsonObject live = store.get(QStringLiteral("live"));
        QCOMPARE(live["source"_L1].toString(), QStringLiteral("f1_feed"));
        QCOMPARE(live["rows"_L1][1]["best"_L1].toString(), QStringLiteral("+0.5"));
        QCOMPARE(live["weather"_L1]["track"_L1].toDouble(), 24.0);
        // Switching back to OpenF1 stops the stream.
        settings.live_source = QStringLiteral("openf1");
        poller.settingsChanged();
        QVERIFY(!poller.stream()->running());
    }
};

QTEST_GUILESS_MAIN(TestStream)
#include "tst_stream.moc"
