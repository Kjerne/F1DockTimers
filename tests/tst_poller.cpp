// The poller against a fake network: ports the poller parts of test_openf1_plans.py and
// test_persistence.py, plus the request flows the Python suite leaves untested.
#include "common.h"
#include "core/sessions.h"
#include "core/timeutil.h"
#include "core/weekend.h"
#include "services/poller.h"

#include <QTemporaryDir>
#include <QTimeZone>

using namespace f1;

namespace {

struct Call {
    QString url;
    Headers headers;
    bool post = false;
    QByteArray body;
};

// Answers synchronously, like the duck-typed Http classes in the Python tests.
class FakeHttp : public HttpApi {
public:
    struct Answer {
        QJsonValue data;
        int status = 200;   // anything else: fail(reason, status)
    };
    QList<std::pair<QString, Answer>> routes;   // first url substring match wins
    std::optional<Answer> fallback = Answer{QJsonValue(), 0};
    QList<Call> calls;
    bool offline = false;
    QString plan;

    void route(const QString& part, const QJsonValue& data) { routes.append({part, Answer{data, 200}}); }
    void failRoute(const QString& part, int status) { routes.append({part, Answer{QJsonValue(), status}}); }

    void answer(const QString& url, const Ok& ok, const Fail& fail) {
        std::optional<Answer> a;
        for (const auto& [part, ans] : routes)
            if (url.contains(part)) { a = ans; break; }
        if (!a) a = fallback;
        if (a->status == 200) ok(a->data);
        else fail(a->status ? QStringLiteral("http %1").arg(a->status) : QStringLiteral("net"), a->status);
    }
    void getJson(const QString& url, Ok ok, Fail fail, const Headers& headers, int) override {
        calls.append({url, headers, false, {}});
        answer(url, ok, fail);
    }
    void postForm(const QString& url, const QByteArray& body, Ok ok, Fail fail, int) override {
        calls.append({url, {}, true, body});
        answer(url, ok, fail);
    }
    void setOpenf1Plan(const QString& p) override { plan = p; }
    bool isOffline() const override { return offline; }

    bool anyAuth() const {
        for (const auto& c : calls)
            for (const auto& h : c.headers)
                if (h.first == "Authorization") return true;
        return false;
    }
    bool anyPost() const {
        return std::any_of(calls.begin(), calls.end(), [](const Call& c) { return c.post; });
    }
    int count(const QString& part) const {
        return static_cast<int>(std::count_if(calls.begin(), calls.end(), [&](const Call& c) { return c.url.contains(part); }));
    }
};

std::optional<credentials::Pair> noCreds(const QString&) { return std::nullopt; }
std::optional<credentials::Pair> someCreds(const QString&) {
    return credentials::Pair{QStringLiteral("me@example.com"), QStringLiteral("p&ss word")};
}

struct Rig {
    QTemporaryDir dir;
    DataStore store{dir.filePath(QStringLiteral("c.json")), dir.filePath(QStringLiteral("c.json"))};
    Settings settings;
    FakeHttp http;
    std::unique_ptr<Poller> poller;
    void make(Poller::CredentialSource creds = noCreds) {
        poller = std::make_unique<Poller>(&store, &settings, &http, nullptr, std::move(creds));
    }
};

QJsonArray arr(std::initializer_list<QJsonValue> v) { return QJsonArray(v); }

}  // namespace

class TestPoller : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init() {
        qunsetenv("OPENF1_USERNAME");
        qunsetenv("OPENF1_PASSWORD");
    }

    // test_openf1_plans.py::test_free_plan_stays_quiet_during_sessions
    void freePlanStaysQuietDuringSessions() {
        Rig r;
        r.store.setRace(t::nextRace());
        r.settings.openf1_plan = QStringLiteral("free");
        r.settings.openf1_username = QStringLiteral("someone");
        r.make(someCreds);   // even with a stored password, the free plan never logs in
        const qint64 fp1 = t::ts(*r.store.race(), "fp1");
        QVERIFY(r.poller->openf1Restricted(r.store.race()->sessions, fp1 + 600));
        QVERIFY(!r.poller->openf1Restricted(r.store.race()->sessions, fp1 - 3 * 3600));
        r.settings.openf1_plan = QStringLiteral("sponsor");
        QVERIFY(!r.poller->openf1Restricted(r.store.race()->sessions, fp1 + 600));
        r.settings.openf1_plan = QStringLiteral("free");
        r.http.calls.clear();
        r.poller->pollOpenf1();
        QVERIFY(!r.http.calls.isEmpty());
        QVERIFY(!r.http.anyAuth());
        QVERIFY(!r.http.anyPost());
    }

    // test_persistence.py::test_failed_fetch_keeps_previous_rows
    void failedFetchKeepsPreviousRows() {
        Rig r;
        r.http.offline = true;
        QJsonObject d = parsers::parseDriverStandings(t::fx("driver_standings.json"));
        d["loaded"_L1] = true;
        r.store.set(QStringLiteral("drivers"), d);
        r.make();
        r.poller->update();
        QVERIFY(!r.store.get(QStringLiteral("drivers"))["failed"_L1].toBool());   // fresh: not refetched
        QCOMPARE(r.http.count(QStringLiteral("driverStandings")), 0);
        r.poller->refreshAll(true);
        const QJsonObject after = r.store.get(QStringLiteral("drivers"));
        QVERIFY(after["failed"_L1].toBool());
        QCOMPARE(after["rows"_L1][0]["code"_L1].toString(), QStringLiteral("ANT"));
        QVERIFY(r.store.status["offline"_L1].toBool());
        QCOMPARE(r.poller->attempts.value(QStringLiteral("drivers")), 1);
        QVERIFY(r.poller->due.value(QStringLiteral("drivers")) > 0);
    }

    // test_persistence.py::test_live_unavailable_message
    void liveUnavailableMessage() {
        Rig r;
        r.http.failRoute(QStringLiteral("openf1"), 401);
        r.make();
        r.poller->pollOpenf1();
        QCOMPARE(r.store.get(QStringLiteral("live"))["error"_L1].toString(), QStringLiteral("auth"));
        QVERIFY(r.store.get(QStringLiteral("live"))["rows"_L1].toArray().isEmpty());
    }

    void rateLimitedLiveSaysSo() {
        Rig r;
        r.http.failRoute(QStringLiteral("openf1"), 429);
        r.make();
        r.poller->pollOpenf1();
        QCOMPARE(r.store.get(QStringLiteral("live"))["error"_L1].toString(), QStringLiteral("rate"));
    }

    void sponsorPlanLogsInAndSendsTheToken() {
        Rig r;
        r.settings.openf1_plan = QStringLiteral("sponsor");
        r.settings.openf1_username = QStringLiteral("me@example.com");
        r.http.route(QStringLiteral("/token"), QJsonObject{{"access_token", "tok123"}, {"expires_in", 3600}});
        r.http.route(QStringLiteral("drivers"), t::fx("openf1_drivers.json"));
        r.http.route(QStringLiteral("position"), t::fx("openf1_position.json"));
        r.http.route(QStringLiteral("race_control"), t::fx("openf1_race_control.json"));
        r.http.route(QStringLiteral("sessions"), QJsonArray());
        r.make(someCreds);
        r.poller->pollOpenf1();
        QVERIFY(r.http.anyPost());
        const Call login = r.http.calls.first();
        QVERIFY(login.post && login.url.endsWith(QStringLiteral("/token")));
        QCOMPARE(login.body, QByteArray("username=me%40example.com&password=p%26ss%20word"));
        for (const Call& c : r.http.calls) {
            if (c.post) continue;
            QCOMPARE(c.headers.size(), 1);
            QCOMPARE(c.headers.first().second, QByteArray("Bearer tok123"));
        }
        const QJsonObject live = r.store.get(QStringLiteral("live"));
        QCOMPARE(live["error"_L1].toString(), QString());
        QCOMPARE(live["flag"_L1].toString(), QStringLiteral("CHEQUERED"));
        QVERIFY(!live["rows"_L1].toArray().isEmpty());
        // A second poll reuses the token: no second login.
        r.http.calls.clear();
        r.poller->pollOpenf1();
        QVERIFY(!r.http.anyPost());
    }

    void sponsorWithoutPasswordFallsBackToNoLogin() {
        Rig r;
        r.settings.openf1_plan = QStringLiteral("sponsor");
        r.make(noCreds);
        r.poller->pollOpenf1();
        QVERIFY(!r.http.anyPost());
        QVERIFY(!r.http.anyAuth());
    }

    void failedLoginBacksOff() {
        Rig r;
        r.settings.openf1_plan = QStringLiteral("sponsor");
        r.http.failRoute(QStringLiteral("/token"), 401);
        r.make(someCreds);
        r.poller->pollOpenf1();
        QCOMPARE(r.store.get(QStringLiteral("live"))["error"_L1].toString(), QStringLiteral("login"));
        QCOMPARE(r.http.count(QStringLiteral("position")), 0);
    }

    void scheduleAndStandingsOnFirstUpdate() {
        Rig r;
        r.http.route(QStringLiteral("current/next.json"), t::fx("next.json"));
        r.http.route(QStringLiteral("driverStandings"), t::fx("driver_standings.json"));
        r.http.route(QStringLiteral("constructorStandings"), t::fx("constructor_standings.json"));
        r.http.route(QStringLiteral("current/last/results"), t::fx("results.json"));
        r.http.fallback = FakeHttp::Answer{QJsonArray(), 200};
        r.make();
        r.poller->update();
        QVERIFY(r.store.race());
        QCOMPARE(r.store.race()->name, QStringLiteral("Las Vegas Grand Prix"));
        QCOMPARE(r.store.get(QStringLiteral("drivers"))["rows"_L1][0]["code"_L1].toString(), QStringLiteral("ANT"));
        QVERIFY(r.store.get(QStringLiteral("constructors"))["loaded"_L1].toBool());
        QCOMPARE(r.store.get(QStringLiteral("results"))["race_name"_L1].toString(), QStringLiteral("Brazilian Grand Prix"));
        // Jolpica only ever gets plain GETs.
        for (const Call& c : r.http.calls)
            if (c.url.contains(QStringLiteral("jolpi"))) QVERIFY(c.headers.isEmpty() && !c.post);
        // Nothing is fetched again within the refresh interval. (Once the fixture's race, Las Vegas
        // on 2026-11-22, is over, the poller refreshes the schedule and standings on purpose, so this part only
        // holds while it is still ahead.)
        r.poller->update();
        if (*sessionTs(r.store.race()->sessions, QStringLiteral("race")) > nowSec()) {
            QCOMPARE(r.http.count(QStringLiteral("current/next.json")), 1);
            QCOMPARE(r.http.count(QStringLiteral("driverStandings")), 1);
        }
    }

    void seasonTransitionAsksForNextYear() {
        Rig r;
        r.http.route(QStringLiteral("current/next.json"),
                     QJsonObject{{"MRData", QJsonObject{{"RaceTable", QJsonObject{{"Races", QJsonArray()}}}}}});
        r.http.route(QStringLiteral("/races.json"), t::fx("season.json"));
        r.http.fallback = FakeHttp::Answer{QJsonArray(), 200};
        r.make();
        r.poller->update();
        const int year = QDate::currentDate().year();
        bool asked = false;
        for (const Call& c : r.http.calls) asked |= c.url.endsWith(QStringLiteral("/%1/races.json").arg(year)) ||
                                                    c.url.endsWith(QStringLiteral("/%1/races.json").arg(year + 1));
        QVERIFY(asked);
        // season.json is all in the past for "now": no race, status says so.
        if (!r.store.race()) QCOMPARE(r.store.status["error"_L1].toString(), QStringLiteral("no_race"));
    }

    void scheduleFailureBacksOff() {
        Rig r;
        r.make();
        r.poller->update();
        QCOMPARE(r.store.status["error"_L1].toString(), QStringLiteral("net"));
        QCOMPARE(r.poller->attempts.value(QStringLiteral("schedule")), 1);
        QVERIFY(r.poller->due.value(QStringLiteral("schedule")) >= r.poller->mono() + 59);
    }

    void weatherForecastForTheWeekend() {
        Rig r;
        Race race = t::nextRace();
        r.store.setRace(race);
        QVERIFY(race.lat.has_value());
        QJsonArray times, temps;
        const qint64 q = t::ts(race, "quali");
        for (int i = -2; i < 4; ++i) {
            times.append(q + i * 3600);
            temps.append(12 + i);
        }
        r.http.route(QStringLiteral("open-meteo"), QJsonObject{{"hourly", QJsonObject{{"time", times}, {"temperature_2m", temps}}}});
        r.make();
        r.poller->loadWeather(q - 86400);
        QCOMPARE(r.http.count(QStringLiteral("api.open-meteo.com/v1/forecast?latitude=")), 1);
        const QJsonObject w = r.store.get(QStringLiteral("weather"));
        QCOMPARE(w["round"_L1].toString(), QStringLiteral("2026-21"));
        QVERIFY(w["loaded"_L1].toBool());
        QVERIFY(r.store.forecast() && r.store.forecast()->times.size() == 6);
        // Weeks ahead or after the weekend: nothing is fetched.
        r.http.calls.clear();
        r.poller->loadWeather(q - 30 * 86400);
        r.poller->loadWeather(q + 10 * 86400);
        QCOMPARE(r.http.calls.size(), 0);
        // "off" keeps what we have and fetches nothing.
        r.settings.weather_refresh = QStringLiteral("off");
        r.poller->loadWeather(q - 86400);
        QCOMPARE(r.http.calls.size(), 0);
    }

    void measuredWeatherOncePerSession() {
        Rig r;
        Race race = t::nextRace();
        r.store.setRace(race);
        const qint64 fp1 = t::ts(race, "fp1");
        const QDateTime start = QDateTime::fromSecsSinceEpoch(fp1, QTimeZone::UTC);
        r.http.route(QStringLiteral("sessions?meeting_key=latest"),
                     arr({QJsonObject{{"session_key", 9001}, {"session_name", "Practice 1"},
                                      {"date_start", start.toString(Qt::ISODate)}},
                          QJsonObject{{"session_key", 9002}, {"session_name", "Practice 2"},
                                      {"date_start", "2026-11-21T00:00:00+00:00"}}}));
        r.http.route(QStringLiteral("weather?session_key=9001"),
                     arr({QJsonObject{{"air_temperature", 14.5}, {"track_temperature", 20.0}, {"rainfall", 0}},
                          QJsonObject{{"air_temperature", 13.0}, {"rainfall", 1}}}));
        r.make();
        r.poller->loadMeasuredWeather(fp1 + 2 * 3600);   // FP1 done, FP2 not yet
        QCOMPARE(r.http.count(QStringLiteral("weather?session_key=9002")), 0);
        const QJsonObject m = r.store.get(QStringLiteral("measured_weather"));
        QCOMPARE(m["round"_L1].toString(), QStringLiteral("2026-21"));
        QCOMPARE(m["sessions"_L1]["fp1"_L1]["air_min"_L1].toDouble(), 13.0);
        QVERIFY(m["sessions"_L1]["fp1"_L1]["rain"_L1].toBool());
        r.http.calls.clear();
        r.poller->loadMeasuredWeather(fp1 + 2 * 3600);   // already have it
        QCOMPARE(r.http.calls.size(), 0);
    }

    void officialGridAfterQualifying() {
        Rig r;
        Race race = t::nextRace();
        r.store.setRace(race);
        const qint64 q = t::ts(race, "quali");
        QJsonObject grid{{"loaded", true}, {"rows", arr({QJsonObject{{"pos", "1"}, {"code", "ANT"}, {"name", "Antonelli"}}})}};
        r.store.set(QStringLiteral("grid"), grid);
        r.http.route(QStringLiteral("session_name=Qualifying"),
                     arr({QJsonObject{{"session_key", 7}, {"session_name", "Qualifying"}, {"session_type", "Qualifying"},
                                      {"date_start", QDateTime::fromSecsSinceEpoch(q, QTimeZone::UTC).toString(Qt::ISODate)}}}));
        r.http.route(QStringLiteral("starting_grid?session_key=7"),
                     arr({QJsonObject{{"position", 1}, {"driver_number", 12}, {"lap_duration", 93.5}}}));
        r.http.route(QStringLiteral("drivers?session_key=7"), arr({QJsonObject{{"driver_number", 12}, {"name_acronym", "ANT"}}}));
        r.make();
        r.poller->loadOfficialGrid(q + 30 * 60);   // too early: nothing
        QCOMPARE(r.http.calls.size(), 0);
        r.poller->loadOfficialGrid(q + 2 * 3600);
        const QJsonObject og = r.store.get(QStringLiteral("grid_official"));
        QCOMPARE(og["round"_L1].toString(), QStringLiteral("2026-21"));
        QCOMPARE(og["rows"_L1][0]["name"_L1].toString(), QStringLiteral("Antonelli"));
        QCOMPARE(og["session_key"_L1].toInteger(), 7);
    }

    void officialGridIgnoresAnotherMeeting() {
        Rig r;
        Race race = t::nextRace();
        r.store.setRace(race);
        const qint64 q = t::ts(race, "quali");
        r.http.route(QStringLiteral("session_name=Qualifying"),
                     arr({QJsonObject{{"session_key", 7}, {"date_start", "2026-11-08T17:00:00+00:00"}}}));
        r.make();
        r.poller->loadOfficialGrid(q + 2 * 3600);
        QCOMPARE(r.http.count(QStringLiteral("starting_grid")), 0);
    }

    void lastSessionResultsOnTheFreePlan() {
        Rig r;
        r.http.route(QStringLiteral("sessions?session_key=latest"),
                     arr({QJsonObject{{"session_key", 55}, {"meeting_key", 1270}, {"session_name", "Race"}, {"session_type", "Race"},
                                      {"date_start", "2025-11-09T17:00:00+00:00"}, {"date_end", "2025-11-09T19:00:00+00:00"}}}));
        r.http.route(QStringLiteral("drivers?session_key=55"), t::fx("openf1_drivers.json"));
        r.http.route(QStringLiteral("session_result?session_key=55"), t::fx("openf1_result_race.json"));
        r.make();
        r.poller->loadLastSession();
        const QJsonObject last = r.store.get(QStringLiteral("last_session"));
        QCOMPARE(last["session"_L1]["key"_L1].toInteger(), 55);
        QCOMPARE(last["rows"_L1][0]["detail"_L1].toString(), QStringLiteral("1:35:23.504"));
        QVERIFY(!r.http.anyAuth());
        // Same session again: no new result requests.
        r.http.calls.clear();
        r.poller->loadLastSession();
        QCOMPARE(r.http.count(QStringLiteral("session_result")), 0);
    }

    // ── every session of a weekend (session pills) ──────────────────────────────────────────

    void weekendRules() {
        auto sess = [](const char* name, const char* type) { return QJsonObject{{"name", name}, {"kind", type}}; };
        QCOMPARE(weekend::pillLabel(sess("Practice 2", "Practice")), QStringLiteral("FP2"));
        QCOMPARE(weekend::pillLabel(sess("Sprint Qualifying", "Qualifying")), QStringLiteral("SQ"));
        QCOMPARE(weekend::pillLabel(sess("Sprint Shootout", "Qualifying")), QStringLiteral("SQ"));
        QCOMPARE(weekend::pillLabel(sess("Sprint", "Race")), QStringLiteral("Sprint"));
        QCOMPARE(weekend::pillLabel(sess("Qualifying", "Qualifying")), QStringLiteral("Quali"));
        QCOMPARE(weekend::pillLabel(sess("Race", "Race")), QStringLiteral("Race"));
        QCOMPARE(weekend::sessionKind(sess("Sprint", "Race")), QStringLiteral("sprint"));

        const qint64 now = 1'800'000'000;
        const QJsonArray races{QJsonObject{{"name", "Race"}, {"kind", "Race"}, {"start", now - 86400}, {"meeting", 1270}},
                               QJsonObject{{"name", "Race"}, {"kind", "Race"}, {"start", now - 15 * 86400}, {"meeting", 1269}}};
        QCOMPARE(weekend::meetingForRace(races, now - 86400 + 3600).value_or(0), 1270);
        QVERIFY(!weekend::meetingForRace(races, now - 7 * 86400));

        auto s2 = [](qint64 key, qint64 start, qint64 end, bool cancelled = false) {
            return QJsonObject{{"key", key}, {"name", "Practice 1"}, {"start", start}, {"end", end}, {"cancelled", cancelled}};
        };
        QJsonObject meeting{{"sessions", QJsonArray{s2(1, now - 7200, now - 3600), s2(2, now - 600, now + 3000),
                                                    s2(3, now - 9000, now - 8000, true), s2(4, now + 3600, now + 7200)}}};
        QCOMPARE(weekend::startedSessions(meeting, now).size(), 2);   // not the cancelled or future one
        QCOMPARE(weekend::nextResultToFetch(meeting, now).value_or(0), 1);
        meeting["results"_L1] = QJsonObject{{"1", QJsonObject{{"rows", QJsonArray()}, {"fetched", now - 60}}}};
        QVERIFY(!weekend::nextResultToFetch(meeting, now));                          // fetched early, a minute ago
        QCOMPARE(weekend::nextResultToFetch(meeting, now + 3600).value_or(0), 1);    // an hour later: again
        meeting["results"_L1] = QJsonObject{{"1", QJsonObject{{"rows", QJsonArray()}, {"fetched", now + 3 * 3600}}}};
        QCOMPARE(weekend::nextResultToFetch(meeting, now + 30 * 3600).value_or(0), 2);   // 1 is final; 2 has ended since
        meeting["listed"_L1] = now;
        QVERIFY(!weekend::needsListing(meeting, now + 600));
        QVERIFY(weekend::needsListing(meeting, now + 3700));                         // weekend running, list an hour old
        QVERIFY(!weekend::needsListing(meeting, now + 30 * 86400));                  // long over: keep it
        QVERIFY(weekend::needsListing(QJsonObject(), now));
        const QJsonObject pruned = weekend::prune(QJsonObject{{"1", 1}, {"2", 2}, {"3", 3}}, {3, 1});
        QCOMPARE(pruned.keys(), (QStringList{QStringLiteral("1"), QStringLiteral("3")}));
    }

    void weekendLoaderFetchesEverySessionOnce() {
        Rig r;
        const qint64 now = nowSec();
        const qint64 race = now - 3 * 86400;
        auto iso = [](qint64 ts) { return QDateTime::fromSecsSinceEpoch(ts, QTimeZone::UTC).toString(Qt::ISODate); };
        r.store.set(QStringLiteral("last_session"),
                    {{"loaded", true}, {"session", QJsonObject{{"key", 905}, {"meeting", 1270}}}, {"rows", QJsonArray()}});
        r.store.set(QStringLiteral("results"),
                    {{"loaded", true}, {"season", "2025"}, {"round", "21"}, {"date_ts", race}, {"rows", QJsonArray{QJsonObject{{"pos", "1"}}}}});
        QJsonArray list;
        const char* names[][2] = {{"Practice 1", "Practice"}, {"Practice 2", "Practice"}, {"Practice 3", "Practice"},
                                  {"Qualifying", "Qualifying"}, {"Race", "Race"}};
        for (int i = 0; i < 5; ++i) {
            const qint64 start = race - (4 - i) * 20 * 3600;
            list.append(QJsonObject{{"session_key", 901 + i}, {"meeting_key", 1270}, {"session_name", names[i][0]},
                                    {"session_type", names[i][1]}, {"location", "Las Vegas"},
                                    {"date_start", iso(start)}, {"date_end", iso(start + 3600)}});
        }
        r.http.route(QStringLiteral("session_name=Race"), arr({list.last()}));
        r.http.route(QStringLiteral("sessions?meeting_key=1270"), list);
        r.http.route(QStringLiteral("drivers?meeting_key=1270"), t::fx("openf1_drivers.json"));
        r.http.failRoute(QStringLiteral("session_result?session_key=901"), 404);   // OpenF1 has none for FP1
        r.http.route(QStringLiteral("session_result"), t::fx("openf1_result_race.json"));
        r.make();
        for (int i = 0; i < 12; ++i) r.poller->loadWeekendStep(now);
        QVERIFY(r.store.get(QStringLiteral("weekends"))["meetings"_L1]["1270"_L1]["results"_L1]["901"_L1]["rows"_L1].toArray().isEmpty());
        const QJsonObject w = r.store.get(QStringLiteral("weekends"));
        QCOMPARE(w["rounds"_L1]["2025-21"_L1].toInteger(), 1270);
        const QJsonObject m = w["meetings"_L1]["1270"_L1].toObject();
        QCOMPARE(m["sessions"_L1].toArray().size(), 5);
        QCOMPARE(m["location"_L1].toString(), QStringLiteral("Las Vegas"));
        QCOMPARE(m["results"_L1].toObject().size(), 5);
        QCOMPARE(m["results"_L1]["905"_L1]["rows"_L1][0]["detail"_L1].toString(), QStringLiteral("1:35:23.504"));
        QCOMPARE(r.http.count(QStringLiteral("drivers?meeting_key")), 1);
        QCOMPARE(r.http.count(QStringLiteral("session_result")), 5);
        QCOMPARE(r.http.count(QStringLiteral("session_name=Race")), 1);
        QVERIFY(!r.http.anyAuth());
        // Everything final: more steps ask for nothing.
        r.http.calls.clear();
        for (int i = 0; i < 4; ++i) r.poller->loadWeekendStep(now);
        QCOMPARE(r.http.calls.size(), 0);
    }

    void weekendDataSurvivesARestart() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("cache.json"));
        DataStore a(file, file);
        a.set(QStringLiteral("weekends"), {{"loaded", true}, {"rounds", QJsonObject{{"2026-21", 1300}}},
                                           {"meetings", QJsonObject{{"1300", QJsonObject{{"location", "Las Vegas"}}}}}});
        QVERIFY(a.saveCache());
        DataStore b(file, file);
        QVERIFY(b.loadCache());
        QCOMPARE(b.get(QStringLiteral("weekends"))["meetings"_L1]["1300"_L1]["location"_L1].toString(), QStringLiteral("Las Vegas"));
    }

    void lastRaceExtrasFromJolpica() {
        Rig r;
        r.store.set(QStringLiteral("results"), {{"loaded", true}, {"season", "2025"}, {"round", "21"}, {"rows", QJsonArray()}});
        r.http.route(QStringLiteral("2025/21/qualifying.json"), t::fx("qualifying.json"));
        r.http.route(QStringLiteral("2025/21/sprint.json"),
                     QJsonObject{{"MRData", QJsonObject{{"RaceTable", QJsonObject{{"Races", QJsonArray()}}}}}});
        r.make();
        r.poller->loadLastRaceExtras();
        QVERIFY(!r.store.get(QStringLiteral("last_qualifying"))["rows"_L1].toArray().isEmpty());
        QVERIFY(r.store.get(QStringLiteral("last_sprint"))["rows"_L1].toArray().isEmpty());   // not a sprint weekend
        r.poller->loadLastRaceExtras();   // same round: not again
        QCOMPARE(r.http.count(QStringLiteral("qualifying.json")), 1);
    }

    void stillRunningSessionWaits() {
        Rig r;
        const QString end = QDateTime::currentDateTimeUtc().addSecs(1800).toString(Qt::ISODate);
        r.http.route(QStringLiteral("sessions?session_key=latest"),
                     arr({QJsonObject{{"session_key", 56}, {"session_type", "Practice"}, {"date_end", end}}}));
        r.make();
        r.poller->loadLastSession();
        QCOMPARE(r.http.count(QStringLiteral("session_result")), 0);
        QVERIFY(r.poller->resultDue >= r.poller->mono() + RESULT_RETRY_S - 1);
    }

    void manualOffsetsOnlyForTheirRound() {
        Rig r;
        r.store.setRace(t::nextRace());
        r.settings.session_offsets = QJsonObject{{"round_key", "2026-21"}, {"quali", 15}, {"fp1", "x"}};
        r.make();
        QCOMPARE(r.poller->manualOffsets(), (QJsonObject{{"quali", 15}}));
        const Session q = *std::find_if(r.store.race()->sessions.begin(), r.store.race()->sessions.end(),
                                        [](const Session& s) { return s.id == u"quali"; });
        QCOMPARE(q.source, QStringLiteral("manual"));
        r.settings.session_offsets = QJsonObject{{"round_key", "2026-20"}, {"quali", 15}};
        QVERIFY(r.poller->manualOffsets().isEmpty());
    }

    void credentialTargetsMatchKeyring() {
        QCOMPARE(credentials::compoundTarget(QStringLiteral("me@x.com"), QString::fromLatin1(credentials::SERVICE)),
                 QStringLiteral("me@x.com@F1DockTimers/OpenF1"));
        qputenv("OPENF1_USERNAME", "envuser");
        qputenv("OPENF1_PASSWORD", "envpass");
        const auto c = credentials::resolve(QString());
        QVERIFY(c && c->first == u"envuser" && c->second == u"envpass");
        qunsetenv("OPENF1_USERNAME");
        qunsetenv("OPENF1_PASSWORD");
        QVERIFY(!credentials::resolve(QString()));   // no username: never touches the store
    }
};

QTEST_GUILESS_MAIN(TestPoller)
#include "tst_poller.moc"
