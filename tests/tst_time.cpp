// Port of reference-python/tests/test_time.py
#include "common.h"
#include "core/timeutil.h"

using namespace f1;
using t::utc;

class TestTime : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parseUtcJolpica() {
        QCOMPARE(parseUtc(QStringLiteral("2026-11-22"), QStringLiteral("04:00:00Z")), utc(2026, 11, 22, 4));
        QCOMPARE(parseUtc(QStringLiteral("2026-11-22"), QJsonValue()), utc(2026, 11, 22));
        QVERIFY(!parseUtc(QStringLiteral("bad"), QStringLiteral("04:00:00Z")));
        QVERIFY(!parseUtc(QStringLiteral("2026-13-01"), QStringLiteral("04:00:00Z")));   // ValueError in Python
    }
    void parseIsoWithOffset() {
        QCOMPARE(parseIso(QStringLiteral("2026-10-10T08:17:42.627000+00:00")), utc(2026, 10, 10, 8, 17, 42));
        QCOMPARE(parseIso(QStringLiteral("2026-10-10T10:00:00+02:00")), utc(2026, 10, 10, 8));
        QCOMPARE(parseIso(QStringLiteral("2026-10-10T03:00:00-05:00")), utc(2026, 10, 10, 8));
    }
    void parseLocalFeed() {
        // Sao Paulo race: 14:00 local at GMT-3 is 17:00 UTC.
        QCOMPARE(parseLocal(QStringLiteral("2026-11-08T14:00:00"), QStringLiteral("-03:00:00")), utc(2026, 11, 8, 17));
        QCOMPARE(parseLocal(QStringLiteral("2026-10-10T21:30:00"), QStringLiteral("08:00:00")), utc(2026, 10, 10, 13, 30));
        QVERIFY(!parseLocal(QStringLiteral("2026-10-10T21:30:00"), QStringLiteral("garbage")));
    }
    void countdownFormats() {
        QCOMPARE(fmtLong(4 * 86400 + 10 * 3600 + 50 * 60 + 30), QStringLiteral("in 4d 10h 50m"));
        QCOMPARE(fmtLong(4 * 3600), QStringLiteral("in 4h 0m"));
        QCOMPARE(fmtLong(59), QStringLiteral("in 0m"));
        QCOMPARE(fmtLong(0), QStringLiteral("live now"));
        QCOMPARE(fmtShort(3 * 86400), QStringLiteral("3d"));
        QCOMPARE(fmtShort(5 * 3600 + 1), QStringLiteral("5h"));
        QCOMPARE(fmtShort(30), QStringLiteral("<1m"));
        QCOMPARE(fmtShort(-5), QStringLiteral("live"));
    }
    void copenhagenConversionAndDst() {
        const Zone cph = zoneFor(QStringLiteral("Europe/Copenhagen"));
        QVERIFY(cph.has_value());
        // Winter (CET, UTC+1): Las Vegas FP1 00:30Z -> Fri 01:30
        QCOMPARE(fmtDatetime(utc(2026, 11, 20, 0, 30), cph, false), QStringLiteral("Fri 01:30"));
        // Summer (CEST, UTC+2): 13:00Z -> 15:00
        QCOMPARE(fmtDatetime(utc(2026, 7, 5, 13), cph, false), QStringLiteral("Sun 15:00"));
        // Across the DST change on 2026-10-25 01:00Z
        QCOMPARE(fmtDatetime(utc(2026, 10, 25, 0, 30), cph, false), QStringLiteral("Sun 02:30"));
        QCOMPARE(fmtDatetime(utc(2026, 10, 25, 1, 30), cph, false), QStringLiteral("Sun 02:30"));
        QCOMPARE(utcOffsetLabel(utc(2026, 7, 1), cph), QStringLiteral("UTC+02:00"));
        QCOMPARE(utcOffsetLabel(utc(2026, 12, 1), cph), QStringLiteral("UTC+01:00"));
    }
    void twelveHourAndLong() {
        const Zone cph = zoneFor(QStringLiteral("Europe/Copenhagen"));
        QCOMPARE(fmtDatetime(utc(2026, 11, 22, 4), cph, true), QStringLiteral("Sun 05:00 AM"));
        QCOMPARE(fmtDatetime(utc(2026, 11, 22, 13), cph, true, true), QStringLiteral("Sun 22 Nov, 02:00 PM"));
        QCOMPARE(fmtClockTime(utc(2026, 11, 22, 13), cph, true), QStringLiteral("2:00 PM"));
    }
    void otherZonesAndFallback() {
        const Zone ny = zoneFor(QStringLiteral("America/New_York"));
        QCOMPARE(fmtDatetime(utc(2026, 11, 22, 4), ny, false), QStringLiteral("Sat 23:00"));
        QVERIFY(!zoneFor(QStringLiteral("auto")));
        QVERIFY(!zoneFor(QStringLiteral("Not/AZone")));
        QVERIFY(!fmtDatetime(utc(2026, 11, 22, 4), std::nullopt, false).isEmpty());   // system zone path
    }
    void clock() {
        QCOMPARE(fmtClock(7 * 60 + 45), QStringLiteral("07:45"));
        QCOMPARE(fmtClock(3725), QStringLiteral("1:02:05"));
        QCOMPARE(fmtClock(-3), QStringLiteral("00:00"));
    }
};

QTEST_GUILESS_MAIN(TestTime)
#include "tst_time.moc"
