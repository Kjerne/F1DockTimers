// Port of reference-python/tests/test_spoilers.py (spoiler logic and the results notification)
// and test_persistence.py's test_notifications_once, plus the notifier's notified.json.
#include "common.h"
#include "core/notifications.h"
#include "core/spoilers.h"
#include "core/timeutil.h"
#include "services/notifier.h"

#include <QTemporaryDir>

using namespace f1;
using namespace f1::spoilers;

namespace {
constexpr qint64 NOW = 1'800'000'000;

QJsonObject last(const QString& kind = QStringLiteral("Race"), qint64 end = NOW - 1800, int key = 9000) {
    QJsonArray rows;
    for (const char* c : {"VER", "NOR", "LEC", "HAM"})
        rows.append(QJsonObject{{"pos", QString::number(rows.size() + 1)}, {"code", QString::fromLatin1(c)}});
    return {{"session", QJsonObject{{"kind", kind}, {"name", kind}, {"location", "Las Vegas"}, {"key", key}, {"end", end}}},
            {"rows", rows}};
}

Race raceWith(Sessions s) {
    Race r;
    r.sessions = std::move(s);
    return r;
}
}  // namespace

class TestSpoilers : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void practiceIsNotASpoilerButQualiIs() {
        const Race fp = raceWith({Session{QStringLiteral("fp1"), NOW - 3600}});
        QCOMPARE(latestSpoilerTs(&fp, {}, {}, NOW), 0);
        QVERIFY(!spoilersHidden(true, 0, &fp, {}, {}, NOW));
        const Race q = raceWith({Session{QStringLiteral("fp1"), NOW - 90000}, Session{QStringLiteral("quali"), NOW - 600}});
        QVERIFY(spoilersHidden(true, 0, &q, {}, {}, NOW));
    }
    void revealLastsUntilTheNextCompetitiveSession() {
        const Race st = raceWith({Session{QStringLiteral("quali"), NOW - 7200}, Session{QStringLiteral("race"), NOW + 86400}});
        QVERIFY(!spoilersHidden(true, NOW - 60, &st, {}, {}, NOW));
        QVERIFY(spoilersHidden(true, NOW - 60, &st, {}, {}, NOW + 86400 + 10));   // race started
    }
    void offMeansNeverHiddenAndOldResultsCount() {
        const QJsonObject results{{"rows", QJsonArray{QJsonObject{{"pos", 1}}}}, {"date_ts", NOW - 3 * 86400}};
        QVERIFY(!spoilersHidden(false, 0, nullptr, results, {}, NOW));
        QVERIFY(spoilersHidden(true, NOW - 4 * 86400, nullptr, results, {}, NOW));
        QVERIFY(!spoilersHidden(true, NOW - 86400, nullptr, results, {}, NOW));
    }
    void openf1LastSessionCounts() {
        const QJsonObject last{{"session", QJsonObject{{"kind", "Race"}, {"start", NOW - 3600}}}};
        QCOMPARE(latestSpoilerTs(nullptr, {}, last, NOW), NOW - 3600);
        const QJsonObject practice{{"session", QJsonObject{{"kind", "Practice"}, {"start", NOW - 3600}}}};
        QCOMPARE(latestSpoilerTs(nullptr, {}, practice, NOW), 0);
    }
    void resultNotificationTop3Once() {
        Settings st;
        const auto hit = notify::resultNotification(last(), st, NOW, {});
        QVERIFY(hit);
        QCOMPARE(hit->key, QStringLiteral("result-9000"));
        QCOMPARE(hit->title, QStringLiteral("Las Vegas · Race results"));
        QCOMPARE(hit->text, QStringLiteral("1. VER   2. NOR   3. LEC"));
        QVERIFY(!notify::resultNotification(last(), st, NOW, {QStringLiteral("result-9000")}));
    }
    void resultNotificationSkipsPracticeOldAndOff() {
        Settings st;
        QVERIFY(!notify::resultNotification(last(QStringLiteral("Practice")), st, NOW, {}));
        QVERIFY(!notify::resultNotification(last(QStringLiteral("Race"), NOW - 7 * 3600), st, NOW, {}));
        st.notify_results = false;
        QVERIFY(!notify::resultNotification(last(), st, NOW, {}));
    }
    void notificationsOnce() {
        const Race r = t::nextRace();
        Settings s;
        s.notify_race = s.notify_quali = true;
        s.notify_lead_min = 10;
        const qint64 rc = *sessionTs(r.sessions, QStringLiteral("race")), q = *sessionTs(r.sessions, QStringLiteral("quali"));
        auto ids = [](const QList<notify::Due>& d) {
            QStringList out;
            for (const auto& x : d) out << x.sessionId;
            return out;
        };
        QVERIFY(notify::dueNotifications(&r, s, rc - 11 * 60, {}).isEmpty());
        const auto due = notify::dueNotifications(&r, s, rc - 9 * 60, {});
        QCOMPARE(ids(due), QStringList{QStringLiteral("race")});
        QVERIFY(notify::dueNotifications(&r, s, rc - 9 * 60, {due[0].key}).isEmpty());
        QCOMPARE(ids(notify::dueNotifications(&r, s, q - 60, {})), QStringList{QStringLiteral("quali")});
        s.notify_quali = false;
        QVERIFY(notify::dueNotifications(&r, s, q - 60, {}).isEmpty());
        QVERIFY(notify::dueNotifications(&r, s, rc + 3600, {}).isEmpty());   // long past: no late toast
        const auto msg = notify::reminderMessage(r, due[0], s, rc - 9 * 60);
        QVERIFY(msg.title.endsWith(QStringLiteral(" · Race")));
        QVERIFY(msg.text.startsWith(QStringLiteral("Lights out in 9m")));
    }
    void notifierRemembersAcrossRestarts() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("notified.json"));
        DataStore store{QString(), QString()};
        store.setRace(t::nextRace());
        Settings s;
        s.notify_race = true;
        s.spoiler_free = false;
        const qint64 rc = *sessionTs(store.race()->sessions, QStringLiteral("race"));
        QStringList shown;
        auto show = [&](const QString& title, const QString&) { shown << title; };
        {
            Notifier n(&store, &s, show, file);
            n.check(rc - 60);
            n.check(rc - 30);
        }
        QCOMPARE(shown.size(), 1);
        Notifier again(&store, &s, show, file);   // a restart: same file, nothing twice
        again.check(rc - 20);
        QCOMPARE(shown.size(), 1);
        QVERIFY(again.sent().contains(store.race()->season + u'-' + store.race()->round + QStringLiteral("-race")));
        QJsonObject fresh = last(QStringLiteral("Race"), nowSec() - 600, 4242);
        store.set(QStringLiteral("last_session"), fresh);
        QCOMPARE(shown.size(), 2);
        QCOMPARE(shown.last(), QStringLiteral("Las Vegas · Race results"));
        store.set(QStringLiteral("last_session"), fresh);
        QCOMPARE(shown.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestSpoilers)
#include "tst_spoilers.moc"
