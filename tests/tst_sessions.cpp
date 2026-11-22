// Port of reference-python/tests/test_sessions.py (countdown and taskbar pill states included).
#include "common.h"
#include "core/pilltext.h"

using namespace f1;

class TestSessions : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void nextAndCurrentSession() {
        const Race r = t::nextRace();
        const qint64 fp1 = t::ts(r, "fp1");
        QCOMPARE(nextSession(r.sessions, fp1 - 10)->id, QStringLiteral("fp1"));
        QCOMPARE(nextSession(r.sessions, fp1 + 10)->id, QStringLiteral("fp2"));
        QCOMPARE(currentSession(r.sessions, fp1 + 600)->id, QStringLiteral("fp1"));
        QVERIFY(!currentSession(r.sessions, fp1 + 2 * 3600));
        QVERIFY(!nextSession(r.sessions, 2'000'000'000));
    }
    void liveWindowAndGridWindow() {
        const Race r = t::nextRace();
        const qint64 q = t::ts(r, "quali"), rc = t::ts(r, "race");
        QVERIFY(nearSession(r.sessions, q - 14 * 60));
        QVERIFY(!gridApplicable(r.sessions, q + 1800));
        QVERIFY(gridApplicable(r.sessions, q + 3600));
        QVERIFY(!gridApplicable(r.sessions, rc));
    }
    void liveRequiresFreshFeed() {
        const Race r = t::nextRace();
        const qint64 q = t::ts(r, "quali");
        QJsonObject live{{"rows", QJsonArray{QJsonObject{{"pos", 1}}}}, {"latest_sec", q + 60}};
        QVERIFY(liveActive(live, r.sessions, q + 120));
        QJsonObject stale = live;
        stale["latest_sec"_L1] = q - 3600;
        QVERIFY(!liveActive(stale, r.sessions, q + 120));
        QVERIFY(!liveActive(QJsonObject{{"rows", QJsonArray()}}, r.sessions, q + 120));
        QVERIFY(!liveActive(live, r.sessions, q + 10 * 3600));
        QVERIFY(liveActive(live, r.sessions, q + 10 * 3600, true));
    }
    void feedAndManualCorrections() {
        const Race r = t::nextRace();
        const qint64 bq = sessionTs(r.rawSessions, QStringLiteral("quali")).value();
        const QMap<QString, qint64> autoTs{{QStringLiteral("quali"), bq + 1800}};
        Sessions eff = effectiveSessions(r.rawSessions, autoTs, {});
        Session q = *std::find_if(eff.begin(), eff.end(), [](const Session& s) { return s.id == u"quali"; });
        QCOMPARE(q.delay, 30);
        QCOMPARE(q.source, QStringLiteral("feed"));
        eff = effectiveSessions(r.rawSessions, autoTs, QJsonObject{{"quali", -15}});
        q = *std::find_if(eff.begin(), eff.end(), [](const Session& s) { return s.id == u"quali"; });
        QCOMPARE(q.delay, -15);
        QCOMPARE(q.source, QStringLiteral("manual"));
        QVERIFY(std::is_sorted(eff.begin(), eff.end(), [](const Session& a, const Session& b) { return a.ts < b.ts; }));
    }
    void countdownStates() {
        const Race r = t::nextRace();
        Settings s;
        const qint64 rc = t::ts(r, "race");
        auto [label, text, live] = pill::countdownState(&r, s, rc - (4 * 86400 + 10 * 3600 + 50 * 60));
        QCOMPARE(label, QStringLiteral("LIGHTS OUT"));
        QCOMPARE(text, QStringLiteral("in 4d 10h 50m"));
        QVERIFY(!live);
        std::tie(label, text, live) = pill::countdownState(&r, s, rc + 600);
        QVERIFY(live);
        QCOMPARE(text, QStringLiteral("● LIVE"));
        QVERIFY(label.contains(QStringLiteral("RACE")));
        s.countdown_target = QStringLiteral("session");
        const qint64 fp1 = t::ts(r, "fp1");
        std::tie(label, text, live) = pill::countdownState(&r, s, fp1 - 3600);
        QCOMPARE(label, QStringLiteral("NEXT · PRACTICE 1"));
        QCOMPARE(text, QStringLiteral("in 1h 0m"));
        s.countdown_target = QStringLiteral("race");
        QCOMPARE(std::get<1>(pill::countdownState(&r, s, rc + 5 * 3600)), QStringLiteral("Race finished"));
    }
    void sprintCountdown() {
        const Race r = t::seasonRound("17");
        Settings s;
        s.countdown_target = QStringLiteral("session");
        const qint64 sq = t::ts(r, "sprint_quali");
        QCOMPARE(std::get<0>(pill::countdownState(&r, s, sq - 60)), QStringLiteral("NEXT · SPRINT QUALI"));
        QCOMPARE(std::get<0>(pill::countdownState(&r, s, sq + 60)), QStringLiteral("LIVE · SPRINT QUALI"));
    }
    void taskbarPillSequence() {
        const Race r = t::nextRace();
        const Settings s;
        const Zone cph = zoneFor(QStringLiteral("Europe/Copenhagen"));
        auto st = pill::state(&r, {}, s, t::ts(r, "fp1") - 3600, cph);
        QCOMPARE(st.top, QStringLiteral("R21 · Las Vegas · FP1"));
        QCOMPARE(st.bottom, QStringLiteral("Fri 01:30 · in 1h 0m"));
        QVERIFY(!st.live);
        st = pill::state(&r, {}, s, t::ts(r, "fp1") + 600, cph);
        QCOMPARE(st, (pill::State{QStringLiteral("R21 · Las Vegas · ● FP1"), QStringLiteral("LIVE"), true}));
        // FP1 over -> FP2 is next
        QVERIFY(pill::state(&r, {}, s, t::ts(r, "fp1") + 3700, cph).top.endsWith(QStringLiteral("FP2")));
        QVERIFY(pill::state(&r, {}, s, t::ts(r, "quali") - 60, cph).top.endsWith(QStringLiteral("Quali")));
        QVERIFY(pill::state(&r, {}, s, t::ts(r, "race") - 60, cph).top.endsWith(QStringLiteral("GP")));
        // Qualifying part from the live feed
        const QJsonObject live{{"session", QJsonObject{{"part", 2}}}, {"rows", QJsonArray()}};
        QCOMPARE(pill::state(&r, live, s, t::ts(r, "quali") + 1500, cph).top, QStringLiteral("R21 · Las Vegas · ● Q2"));
        QCOMPARE(pill::state(&r, {}, s, t::ts(r, "race") + 4 * 3600, cph).bottom, QStringLiteral("Weekend over"));
        QCOMPARE(pill::state(nullptr, {}, s, 0).bottom, QStringLiteral("No upcoming race"));
    }
    void templateRendering() {
        const pill::Fields f{{QStringLiteral("a"), QStringLiteral("x")}, {QStringLiteral("b"), QString()}};
        QCOMPARE(pill::renderTemplate(QStringLiteral("{a} · {b} · {a}"), f), QStringLiteral("x · x"));
        QCOMPARE(pill::renderTemplate(QStringLiteral("{nope} {a:>5}"), f), QStringLiteral("{nope} x"));
        QCOMPARE(pill::renderTemplate(QStringLiteral("{{a}} · {b}"), f), QStringLiteral("{a}"));
    }
    void raceRoundtripKeepsSessions() {
        const Race r = t::nextRace();
        const Race again = Race::fromJson(r.toJson());
        QCOMPARE(again.name, r.name);
        QCOMPARE(again.rawSessions.size(), r.rawSessions.size());
        for (qsizetype i = 0; i < r.rawSessions.size(); ++i) {
            QCOMPARE(again.rawSessions[i].id, r.rawSessions[i].id);
            QCOMPARE(again.rawSessions[i].ts, r.rawSessions[i].ts);
        }
    }
};

QTEST_GUILESS_MAIN(TestSessions)
#include "tst_sessions.moc"
