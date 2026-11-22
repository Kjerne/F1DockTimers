// The F1 live-timing stream parser (core/f1feed). The Python suite has no direct test for it;
// these cover the snapshot + patch flow, qualifying gaps and the clock.
#include "common.h"
#include "core/f1feed.h"

using namespace f1;

namespace {
QString frame(const QJsonObject& o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }
}  // namespace

class TestFeed : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void snapshotThenPatch() {
        FeedState st;
        QVERIFY(!st.handleLine(QStringLiteral("event: ping"), 100));
        QVERIFY(!st.view(100));
        const QJsonObject snapshot{
            {"type", 3},
            {"result",
             QJsonObject{
                 {"DriverList", QJsonObject{{"1", QJsonObject{{"Tla", "VER"}, {"TeamColour", "3671C6"}, {"TeamName", "Red Bull"}}},
                                            {"4", QJsonObject{{"Tla", "NOR"}, {"TeamColour", "FF8000"}}}}},
                 {"TimingData", QJsonObject{{"Lines", QJsonObject{{"1", QJsonObject{{"Position", "2"}, {"Line", 2}, {"GapToLeader", "+1.2"},
                                                                                    {"IntervalToPositionAhead", QJsonObject{{"Value", "+1.2"}}}}},
                                                                   {"4", QJsonObject{{"Position", "1"}, {"Line", 1}, {"GapToLeader", ""},
                                                                                    {"BestLapTime", QJsonObject{{"Value", "1:31.000"}}}}}}}}},
                 {"TrackStatus", QJsonObject{{"Message", "SCDeployed"}}},
                 {"ExtrapolatedClock", QJsonObject{{"Remaining", "0:45:10"}, {"Utc", "2026-11-22T04:10:00Z"}, {"Extrapolating", true}}},
                 {"WeatherData", QJsonObject{{"AirTemp", "18.0"}, {"TrackTemp", "25.5"}, {"Rainfall", "0"}}},
             }}};
        QVERIFY(st.handleLine(QStringLiteral("data:") + frame(snapshot), 1000));
        auto v = st.view(1000);
        QVERIFY(v);
        QJsonArray rows = (*v)["rows"_L1].toArray();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0]["code"_L1].toString(), QStringLiteral("NOR"));
        QCOMPARE(rows[0]["pos"_L1].toInteger(), 1);
        QCOMPARE(rows[1]["best"_L1].toString(), QStringLiteral("+1.2"));
        QCOMPARE(rows[1]["color"_L1].toString(), QStringLiteral("#3671C6"));
        QCOMPARE((*v)["flag"_L1].toString(), QStringLiteral("SAFETY CAR"));
        QCOMPARE((*v)["clock"_L1]["remaining"_L1].toInteger(), 45 * 60 + 10);
        QCOMPARE((*v)["weather"_L1]["track"_L1].toDouble(), 25.5);
        QCOMPARE((*v)["latest_sec"_L1].toInteger(), 1000);
        // A patch moves VER to the front and keeps everything else.
        const QJsonObject patch{{"type", 1},
                                {"target", "feed"},
                                {"arguments", QJsonArray{"TimingData", QJsonObject{{"Lines", QJsonObject{{"1", QJsonObject{{"Line", 0}}}}}}}}};
        QVERIFY(st.handleLine(QStringLiteral("data:") + frame(patch) + QChar(0x1E), 1005));
        rows = (*st.view(1005))["rows"_L1].toArray();
        QCOMPARE(rows[0]["code"_L1].toString(), QStringLiteral("VER"));
        QCOMPARE(rows[0]["team"_L1].toString(), QStringLiteral("Red Bull"));
    }
    void qualifyingGapsToThePartLeader() {
        FeedState st;
        const QJsonObject snapshot{
            {"type", 3},
            {"result",
             QJsonObject{{"SessionInfo", QJsonObject{{"Type", "Qualifying"}, {"Name", "Qualifying"}}},
                         {"TimingData",
                          QJsonObject{{"SessionPart", 2},
                                      {"Lines", QJsonObject{{"16", QJsonObject{{"Line", 1}, {"BestLapTimes", QJsonArray{QJsonObject{{"Value", "1:30.000"}}, QJsonObject{{"Value", "1:29.500"}}}}}},
                                                            {"44", QJsonObject{{"Line", 2}, {"Cutoff", true}, {"BestLapTimes", QJsonArray{QJsonObject{{"Value", "1:30.100"}}, QJsonObject{{"Value", "1:29.750"}}}}}},
                                                            {"81", QJsonObject{{"Line", 3}, {"KnockedOut", true}, {"BestLapTimes", QJsonArray{QJsonObject{{"Value", "1:31.000"}}}}}}}}}}}}};
        QVERIFY(st.handleLine(QStringLiteral("data:") + frame(snapshot), 10));
        const QJsonObject v = *st.view(10);
        QCOMPARE(v["session"_L1]["part"_L1].toInteger(), 2);
        const QJsonArray rows = v["rows"_L1].toArray();
        QCOMPARE(rows[0]["best"_L1].toString(), QStringLiteral("1:29.500"));
        QCOMPARE(rows[1]["gap"_L1].toString(), QStringLiteral("+0.250"));
        QCOMPARE(rows[1]["state"_L1].toString(), QStringLiteral("danger"));
        QCOMPARE(rows[2]["state"_L1].toString(), QStringLiteral("out"));
        QCOMPARE(rows[2]["out_part"_L1].toInteger(), 1);
    }
};

QTEST_GUILESS_MAIN(TestFeed)
#include "tst_feed.moc"
