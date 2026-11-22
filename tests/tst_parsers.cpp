// Port of reference-python/tests/test_parsers.py
#include "common.h"
#include "core/sanitize.h"

using namespace f1;
using namespace f1::parsers;

namespace {
QStringList ids(const Sessions& s) {
    QStringList out;
    for (const auto& x : s) out << x.id;
    return out;
}
QJsonObject byCode(const QJsonArray& rows, const char* code) {
    for (const QJsonValue& r : rows)
        if (r[QStringLiteral("code")].toString() == QLatin1StringView(code)) return r.toObject();
    return {};
}
}  // namespace

class TestParsers : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void nextRace() {
        const Race race = t::nextRace();
        QCOMPARE(race.name, QStringLiteral("Las Vegas Grand Prix"));
        QCOMPARE(race.round, QStringLiteral("21"));
        QCOMPARE(race.season, QStringLiteral("2026"));
        QCOMPARE(race.circuitId, QStringLiteral("vegas"));
        QCOMPARE(race.location(), QStringLiteral("Las Vegas, USA"));
        QCOMPARE(ids(race.sessions), (QStringList{"fp1", "fp2", "fp3", "quali", "race"}));
        QVERIFY(!race.isSprintWeekend());
        QVERIFY(race.url.startsWith(QStringLiteral("https://en.wikipedia.org/")));
    }
    void sprintWeekendDetection() {
        const Race race = t::seasonRound("17");
        QVERIFY(race.isSprintWeekend());
        QCOMPARE(ids(race.sessions), (QStringList{"fp1", "sprint_quali", "sprint", "quali", "race"}));
        QVERIFY(std::is_sorted(race.sessions.begin(), race.sessions.end(),
                               [](const Session& a, const Session& b) { return a.ts < b.ts; }));
    }
    void emptyNextReturnsNone() {
        QVERIFY(!parseNextRace(QJsonObject{{"MRData", QJsonObject{{"RaceTable", QJsonObject{{"Races", QJsonArray()}}}}}}));
        QVERIFY(!parseNextRace(QJsonObject()));
        QVERIFY(!parseNextRace(QStringLiteral("garbage")));
    }
    void seasonTransitionPicksFirstFuture() {
        const QJsonValue data = t::fx("season.json");
        QVERIFY(!firstFutureRace(data, 2'000'000'000));
        QCOMPARE(firstFutureRace(data, 0)->round, QStringLiteral("1"));
    }
    void driverStandings() {
        const QJsonArray rows = parseDriverStandings(t::fx("driver_standings.json"))["rows"_L1].toArray();
        QCOMPARE(rows[0]["code"_L1].toString(), QStringLiteral("ANT"));
        QCOMPARE(rows[0]["pts"_L1].toString(), QStringLiteral("390"));
        QCOMPARE(rows[0]["wins"_L1].toInteger(), 9);
        QCOMPARE(rows[0]["gap"_L1].toInteger(), 0);
        QVERIFY(rows[1]["gap"_L1].toInteger() > 0);
        QCOMPARE(rows[0]["team_id"_L1].toString(), QStringLiteral("mercedes"));
        QVERIFY(rows.size() <= 30);
    }
    void constructorStandings() {
        const QJsonArray rows = parseConstructorStandings(t::fx("constructor_standings.json"))["rows"_L1].toArray();
        QCOMPARE(rows[0]["name"_L1].toString(), QStringLiteral("Mercedes"));
        QCOMPARE(rows[0]["wins"_L1].toInteger(), 12);
        QCOMPARE(rows.size(), 11);
    }
    void resultsStatusAndFastest() {
        const QJsonObject r = parseResults(t::fx("results.json"));
        QCOMPARE(r["race_name"_L1].toString(), QStringLiteral("Brazilian Grand Prix"));
        const QJsonArray rows = r["rows"_L1].toArray();
        QCOMPARE(byCode(rows, "NOR")["detail"_L1].toString(), QStringLiteral("1:35:23.504"));
        QCOMPARE(byCode(rows, "NOR")["pts"_L1].toString(), QStringLiteral("25"));
        QCOMPARE(byCode(rows, "HAM")["detail"_L1].toString(), QStringLiteral("+1 Lap"));   // lapped car, not "+4.427"
        QCOMPARE(byCode(rows, "PER")["detail"_L1].toString(), QStringLiteral("+2 Laps"));
        QCOMPARE(byCode(rows, "STR")["detail"_L1].toString(), QStringLiteral("Retired"));
        QCOMPARE(byCode(rows, "BOT")["pos"_L1].toString(), QStringLiteral("W"));
        QVERIFY(r["fastest"_L1].isObject() && !r["fastest"_L1]["time"_L1].toString().isEmpty());
        QVERIFY(r["date_ts"_L1].toInteger() > 0);
    }
    void qualifyingAndConfirmedGrid() {
        const QJsonObject q = parseQualifying(t::fx("qualifying.json"));
        QCOMPARE(q["rows"_L1][0]["pos"_L1].toString(), QStringLiteral("1"));
        QVERIFY(!q["rows"_L1][0]["detail"_L1].toString().isEmpty());
        const QJsonArray grid = gridFromResults(parseResults(t::fx("results.json")));
        QCOMPARE(grid[0]["pos"_L1].toString(), QStringLiteral("1"));
        for (const QJsonValue& r : grid) {
            const QString p = r["pos"_L1].toString();
            QVERIFY(p == u"PL" || py::isDigits(p));
        }
    }
    void sanitising() {
        QCOMPARE(safe(QStringLiteral("Ab‮cd") + QChar(0) + QStringLiteral("e")), QStringLiteral("Ab cd e"));
        QCOMPARE(safe(QString(100, u'x'), 10), QString(10, u'x') + QChar(0x2026));
        QCOMPARE(safeLink(QStringLiteral("http://en.wikipedia.org/wiki/Lando_Norris")),
                 QStringLiteral("https://en.wikipedia.org/wiki/Lando_Norris"));
        QVERIFY(!safeLink(QStringLiteral("https://evil.example/wiki")));
        QVERIFY(!safeLink(QStringLiteral("https://user@en.wikipedia.org/")));
        QVERIFY(!safeLink(QStringLiteral("https://en.wikipedia.org:8443/")));
        QVERIFY(!safeLink(QStringLiteral("javascript:alert(1)")));
        QCOMPARE(hexColor(QStringLiteral("27f4d2")), QStringLiteral("#27F4D2"));
        QVERIFY(!hexColor(QStringLiteral("27f4d")));
    }
    void sessionInfoFeed() {
        const Race race = t::nextRace();
        QJsonObject info{{"Meeting", QJsonObject{{"Number", 21}}}, {"Name", "Qualifying"},
                         {"StartDate", "2026-11-20T20:30:00"}, {"GmtOffset", "-08:00:00"}};
        const auto hit = parseSessionInfo(info, &race);
        QVERIFY(hit.has_value());
        QCOMPARE(hit->first, QStringLiteral("quali"));
        QCOMPARE(hit->second - sessionTs(race.rawSessions, QStringLiteral("quali")).value(), 30 * 60);
        QJsonObject other = info;
        other["Meeting"_L1] = QJsonObject{{"Number", 20}};
        QVERIFY(!parseSessionInfo(other, &race));   // another weekend in the feed is ignored
        QJsonObject far = info;
        far["StartDate"_L1] = "2026-11-25T20:30:00";
        QVERIFY(!parseSessionInfo(far, &race));     // an absurd shift is ignored
    }
    void openf1Live() {
        const Drivers drivers = parseOpenf1Drivers(t::fx("openf1_drivers.json"));
        QVERIFY(!drivers.isEmpty());
        for (const auto& d : drivers) QVERIFY(d["acr"_L1].toString().size() <= 8);
        auto [rows, newest] = parseOpenf1Positions(t::fx("openf1_position.json"), drivers);
        QCOMPARE(rows[0]["pos"_L1].toInteger(), 1);
        QVERIFY(newest > 0);
        for (qsizetype i = 1; i < rows.size(); ++i) QVERIFY(rows[i - 1]["pos"_L1].toInteger() <= rows[i]["pos"_L1].toInteger());
        applyOpenf1Intervals(rows, t::fx("openf1_intervals.json"));
        QCOMPARE(rows[0]["best"_L1].toString(), QString());
        QCOMPARE(parseOpenf1Flag(t::fx("openf1_race_control.json")), QStringLiteral("CHEQUERED"));
        QVERIFY(!parseOpenf1RaceControl(t::fx("openf1_race_control.json")).isEmpty());
    }
    void openf1GarbageIsHarmless() {
        QVERIFY(parseOpenf1Drivers(QJsonObject{{"detail", "nope"}}).isEmpty());
        auto [rows, newest] = parseOpenf1Positions(QJsonArray{QJsonObject{{"driver_number", "x"}}, QJsonValue(), 5}, {});
        QVERIFY(rows.isEmpty());
        QCOMPARE(newest, 0);
        QCOMPARE(parseOpenf1Flag(QJsonValue()), QString());
        QVERIFY(!parseOpenf1Session(QJsonArray()));
    }
};

QTEST_GUILESS_MAIN(TestParsers)
#include "tst_parsers.moc"
