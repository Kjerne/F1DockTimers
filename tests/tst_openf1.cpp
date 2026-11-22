// Port of reference-python/tests/test_openf1_plans.py (the parts below the poller).
#include "common.h"
#include "services/ratelimit.h"
#include "settings.h"

using namespace f1;
using namespace f1::parsers;

class TestOpenf1 : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void rateLimiterFreePlanLimits() {
        const QString host = QStringLiteral("api.openf1.org");
        RateLimiter lim({{host, openf1Plan(QStringLiteral("free"))}});
        double t = 1000.0;
        QList<double> starts;
        for (int i = 0; i < 40; ++i) {
            t += lim.wait(host, t);
            lim.note(host, t);
            starts << t;
            t += 0.001;
        }
        for (qsizetype i = 0; i + 3 < starts.size(); ++i) QVERIFY(starts[i + 3] - starts[i] >= 1.0);     // <= 3/s
        for (qsizetype i = 0; i + 30 < starts.size(); ++i) QVERIFY(starts[i + 30] - starts[i] >= 60.0);  // <= 30/min
    }
    void rateLimiterSponsorIsFasterAndRetryAfterBlocks() {
        const QString h = QStringLiteral("h");
        RateLimiter free({{h, openf1Plan(QStringLiteral("free"))}});
        RateLimiter sponsor({{h, openf1Plan(QStringLiteral("sponsor"))}});
        free.note(h, 0);
        sponsor.note(h, 0);
        QVERIFY(sponsor.wait(h, 0) < free.wait(h, 0));
        sponsor.block(h, 30.0);
        QVERIFY(sponsor.wait(h, 10.0) >= 20.0);
        QCOMPARE(RateLimiter().wait(QStringLiteral("other.host"), 0), 0.0);
    }
    void sessionResultsAllKinds() {
        const Drivers drivers = parseOpenf1Drivers(t::fx("openf1_drivers.json"));
        const QJsonArray race = parseOpenf1SessionResult(t::fx("openf1_result_race.json"), drivers, QStringLiteral("Race"));
        QCOMPARE(race[0]["pos"_L1].toString(), QStringLiteral("1"));
        QCOMPARE(race[0]["detail"_L1].toString(), QStringLiteral("1:35:23.504"));
        QCOMPARE(race[0]["pts"_L1].toString(), QStringLiteral("25"));
        QCOMPARE(race[1]["detail"_L1].toString(), QStringLiteral("+14.602"));
        bool lapped = false, out = false;
        for (const QJsonValue& r : race) {
            lapped |= r["detail"_L1].toString() == u"+1:15.402";
            out |= r["status"_L1].toString() == u"DNF" || r["status"_L1].toString() == u"DNS";
        }
        QVERIFY(lapped);
        QVERIFY(out);
        const QJsonArray fp1 = parseOpenf1SessionResult(t::fx("openf1_result_fp1.json"), drivers, QStringLiteral("Practice"));
        QCOMPARE(fp1[0]["detail"_L1].toString(), QStringLiteral("1:11.717"));
        QCOMPARE(fp1[0]["gap"_L1].toString(), QString());
        QCOMPARE(fp1[1]["gap"_L1].toString(), QStringLiteral("+0.082"));
        const QJsonArray q = parseOpenf1SessionResult(t::fx("openf1_result_quali.json"), drivers, QStringLiteral("Qualifying"));
        QCOMPARE(q[0]["detail"_L1].toString(), QStringLiteral("1:27.470"));
        QVERIFY(q[0]["gap"_L1].toString().endsWith(QStringLiteral("Q3")));
        QVERIFY(q.last()["gap"_L1].toString().endsWith(QStringLiteral("Q1")) || !q.last()["status"_L1].toString().isEmpty());
        qint64 prev = 0;
        for (const QJsonValue& r : q) {
            const QString p = r["pos"_L1].toString();
            if (!py::isDigits(p)) continue;
            QVERIFY(p.toLongLong() >= prev);
            prev = p.toLongLong();
        }
    }
    void lapFormatting() {
        QCOMPARE(fmtLap(71.717), QStringLiteral("1:11.717"));
        QCOMPARE(fmtLap(59.5), QStringLiteral("59.500"));
        QCOMPARE(fmtLap(5723.504), QStringLiteral("1:35:23.504"));
        QCOMPARE(fmtLap(QJsonValue()), QString());
    }
    void freePlanSettingsDefault() {
        Settings s;
        QCOMPARE(s.openf1_plan, QStringLiteral("free"));
        s.openf1_plan = QStringLiteral("gold");
        s.clamp();
        QCOMPARE(s.openf1_plan, QStringLiteral("free"));
    }
};

QTEST_GUILESS_MAIN(TestOpenf1)
#include "tst_openf1.moc"
