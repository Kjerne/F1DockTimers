#include "poller.h"

#include "../core/parsers.h"
#include "../core/pyjson.h"
#include "../core/timeutil.h"
#include "../core/weather.h"
#include "../core/weekend.h"

#include <QDate>
#include <QDateTime>
#include <QLoggingCategory>
#include <QUrl>
#include <cmath>
#include <memory>

Q_LOGGING_CATEGORY(lcPoll, "f1.poller")

namespace f1 {

namespace {
const QString JOLPICA = QStringLiteral("https://api.jolpi.ca/ergast/f1/");
const QString OPENF1 = QStringLiteral("https://api.openf1.org/v1/");
const QString OPENF1_TOKEN = QStringLiteral("https://api.openf1.org/token");
const QString F1_TIMING = QStringLiteral("https://livetiming.formula1.com/static/");

double wallNow() { return QDateTime::currentMSecsSinceEpoch() / 1000.0; }
QJsonObject merged(QJsonObject a, const QJsonObject& b) {
    for (auto it = b.begin(); it != b.end(); ++it) a.insert(it.key(), it.value());
    return a;
}
bool isInt(const QJsonValue& v) { return v.isDouble() && v.toDouble() == std::trunc(v.toDouble()); }
}  // namespace

QString roundKey(const Race* r) { return r ? r->season + u'-' + r->round : QString(); }

void fillDriverNames(QJsonArray& rows, const QMap<QString, QJsonObject>& known) {
    for (qsizetype i = 0; i < rows.size(); ++i) {
        QJsonObject r = rows[i].toObject();
        if (!py::str(r.value(QStringLiteral("code"))).startsWith(u'#')) continue;
        const QJsonObject d = known.value(py::str(r.value(QStringLiteral("num"))));
        const QJsonValue code = d.value(QStringLiteral("code"));
        if (d.isEmpty() || !py::truthy(code) || py::str(code).startsWith(u'#')) continue;
        r[QStringLiteral("code")] = code;
        for (const char* k : {"color", "team"}) {
            const QString key = QString::fromLatin1(k);
            if (!py::truthy(r.value(key))) r[key] = d.contains(key) ? d.value(key) : QJsonValue(QString());
        }
        rows[i] = r;
    }
}

Poller::Poller(DataStore* store, Settings* settings, HttpApi* http, QObject* parent, CredentialSource creds)
    : QObject(parent), m_store(store), m_settings(settings), m_http(http), m_creds(std::move(creds)) {
    m_clock.start();
    for (const char* k : {"schedule", "drivers", "constructors", "results", "weather", "grid_official", "measured"})
        due.insert(QString::fromLatin1(k), 0.0);
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &Poller::update);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { m_store->saveCache(); });
    m_http->setOpenf1Plan(settings->openf1_plan);
    m_lastWall = nowSec();
    m_lastMono = mono();
    scheduleFromCache();
}

// ── lifecycle ────────────────────────────────────────────────────────────────

void Poller::start() {
    m_timer.start();
    update();
}

void Poller::stop() {
    m_timer.stop();
    stopStream();
    m_store->saveCache();
}

void Poller::scheduleFromCache() {
    // Fresh cached data is not refetched at startup; stale data is.
    const double m = mono(), wall = wallNow();
    const QMap<QString, double> every{{QStringLiteral("schedule"), m_settings->schedule_refresh_h * 3600},
                                      {QStringLiteral("drivers"), m_settings->standings_refresh_h * 3600},
                                      {QStringLiteral("constructors"), m_settings->standings_refresh_h * 3600},
                                      {QStringLiteral("results"), m_settings->standings_refresh_h * 3600}};
    for (auto it = every.begin(); it != every.end(); ++it) {
        const QString key = it.key() == u"schedule" ? QStringLiteral("race") : it.key();
        const double fetched = m_store->fetched.value(key, 0);
        due[it.key()] = fetched ? m + std::max(0.0, it.value() - (wall - fetched)) : 0.0;
    }
    if (m_store->race()) republishRace();
}

void Poller::refreshAll(bool force) {
    const double m = mono();
    if (!force && m - m_lastRefresh < MIN_REFRESH_GAP_S) return;
    m_lastRefresh = m;
    for (auto it = due.begin(); it != due.end(); ++it) it.value() = 0;
    attempts.clear();
    m_gridKey.reset();
    m_gridRetryAt = 0;
    m_liveBackoffUntil = 0;
    m_lastLive = -1e9;
    resultDue = 0;
    m_store->setStatus(true);
    update();
}

void Poller::settingsChanged() {
    republishRace();
    m_gridKey.reset();
    if (m_settings->live_source != u"f1_feed") stopStream();
    resetLive();
    m_lastLive = -1e9;
    resultDue = 0;
    m_http->setOpenf1Plan(m_settings->openf1_plan);
}

// ── helpers ──────────────────────────────────────────────────────────────────

double Poller::retryDelay(const QString& name) {
    const int n = attempts.value(name, 0) + 1;
    attempts[name] = n;
    return std::min(60.0 * std::pow(2.0, n - 1), 900.0);
}

QJsonObject Poller::failedState(const QString& key) const {
    QJsonObject out = m_store->get(key);
    out.insert(QStringLiteral("failed"), true);
    return out;
}

void Poller::dirty() { m_saveTimer.start(); }

void Poller::get(const QString& name, const QString& url, HttpApi::Ok ok, HttpApi::Fail fail, bool scoped, bool auth) {
    if (busy.contains(name)) return;
    busy.insert(name);
    const int gen = m_generation;
    Headers headers;
    if (auth && !m_token.isEmpty()) headers.append({"Authorization", "Bearer " + m_token.toUtf8()});
    m_http->getJson(
        url,
        [this, name, gen, scoped, ok](const QJsonValue& data) {
            busy.remove(name);
            if (scoped && gen != m_generation) return;
            ok(data);
        },
        [this, name, gen, scoped, fail](const QString& reason, int status) {
            busy.remove(name);
            if (scoped && gen != m_generation) return;
            m_store->setStatus(std::nullopt, std::nullopt, m_http->isOffline());
            if (fail) fail(reason, status);
        },
        headers);
}

// ── schedule ─────────────────────────────────────────────────────────────────

QJsonObject Poller::manualOffsets() const {
    const QJsonObject offs = m_settings->session_offsets;
    const Race* race = m_store->race();
    if (!race || offs.value(QStringLiteral("round_key")).toString() != roundKey(race)) return {};
    QJsonObject out;
    for (auto it = offs.begin(); it != offs.end(); ++it)
        if (it.key() != u"round_key" && isInt(it.value())) out.insert(it.key(), it.value());
    return out;
}

void Poller::republishRace() {
    Race* race = m_store->raceMut();
    if (!race) return;
    race->sessions = effectiveSessions(race->rawSessions, m_store->autoTs, manualOffsets());
    m_store->setRace(*race, false);
}

void Poller::resetPerRace() {
    ++m_generation;
    m_store->autoTs.clear();
    m_gridKey.reset();
    m_gridRetryAt = 0;
    for (const char* k : {"grid", "of_drivers", "of_pos", "of_flag", "of_int", "of_session"}) busy.remove(QString::fromLatin1(k));
    m_store->set(QStringLiteral("grid"), DataStore::empty(QStringLiteral("grid")), false);
    resetLive();
    const QJsonObject offs = m_settings->session_offsets;
    if (!offs.isEmpty() && offs.value(QStringLiteral("round_key")).toString() != roundKey(m_store->race()))
        m_settings->session_offsets = {};
}

void Poller::resetLive() {
    m_driversMap.reset();
    m_ofPositions.reset();
    m_ofIntervals.reset();
    if (m_stream) m_stream->state.reset();
    m_store->set(QStringLiteral("live"), DataStore::empty(QStringLiteral("live")), false);
}

void Poller::loadSchedule() {
    get(QStringLiteral("schedule"), JOLPICA + QStringLiteral("current/next.json"),
        [this](const QJsonValue& data) {
            const auto race = parsers::parseNextRace(data);
            if (!race) {
                loadNextSeason();
                return;
            }
            acceptRace(*race);
        },
        [this](const QString& reason, int) {
            due[QStringLiteral("schedule")] = mono() + retryDelay(QStringLiteral("schedule"));
            m_store->setStatus(false, reason);
        });
}

void Poller::loadNextSeason() {
    // Season transition: Jolpica's current/next is empty after the finale.
    const int year = QDateTime::currentDateTimeUtc().date().year();
    const Race* cur = m_store->race();
    int target = year + 1;
    bool ok = true;
    const int curSeason = cur ? cur->season.toInt(&ok) : year;
    if (ok) target = std::max(curSeason + 1, year);
    busy.remove(QStringLiteral("schedule"));
    get(QStringLiteral("schedule_next"), JOLPICA + QStringLiteral("%1/races.json").arg(target),
        [this](const QJsonValue& data) {
            const auto race = parsers::firstFutureRace(data, nowSec());
            if (!race) {
                m_store->setRace(std::nullopt);
                due[QStringLiteral("schedule")] = mono() + m_settings->schedule_refresh_h * 3600;
                m_store->setStatus(false, QStringLiteral("no_race"));
                return;
            }
            acceptRace(*race);
        },
        [this](const QString& reason, int) {
            due[QStringLiteral("schedule")] = mono() + retryDelay(QStringLiteral("schedule"));
            m_store->setStatus(false, reason);
        });
}

void Poller::acceptRace(const Race& race) {
    attempts.remove(QStringLiteral("schedule"));
    due[QStringLiteral("schedule")] = mono() + m_settings->schedule_refresh_h * 3600;
    const Race* old = m_store->race();
    const bool changed = !old || old->round != race.round || old->season != race.season;
    m_store->setRace(race, false);
    if (changed) resetPerRace();
    republishRace();
    m_store->setRace(*m_store->race());
    m_store->setStatus(false, QString());
    qCInfo(lcPoll) << "schedule:" << race.season << "round" << race.round << race.name;
    dirty();
}

// ── weather (Open-Meteo forecast for the circuit) ─────────────────────────────

void Poller::loadWeather(qint64 now) {
    // Hourly forecast for the race weekend, from 16 days before the first session until the race is
    // over. Open-Meteo needs no key; a few requests a day.
    if (m_settings->weather_refresh == u"off" && !m_store->get(QStringLiteral("weather")).value(QStringLiteral("times")).toArray().isEmpty())
        return;   // off: keep what we have, fetch nothing
    const Race* race = m_store->race();
    if (race && !race->lat && !m_coordsRefetched) {
        m_coordsRefetched = true;   // cached race without coordinates: fetch the schedule again
        due[QStringLiteral("schedule")] = 0;
    }
    if (!race || !race->lat || !race->lon || race->sessions.isEmpty()) {
        due[QStringLiteral("weather")] = mono() + 60;   // the schedule may not be loaded yet
        return;
    }
    const qint64 first = race->sessions.first().ts, last = race->sessions.last().ts;
    if (now > last + 4 * 3600 || first - now > weather::FORECAST_DAYS * 86400LL) return;   // over, or too far ahead
    const QString key = roundKey(race);
    get(QStringLiteral("weather"), weather::forecastUrl(*race->lat, *race->lon),
        [this, key](const QJsonValue& data) {
            auto parsed = weather::parseOpenMeteo(data);
            if (!parsed) {
                due[QStringLiteral("weather")] = mono() + 1800;
                return;
            }
            parsed->round = key;
            m_store->set(QStringLiteral("weather"),
                         merged(parsed->toJson(), {{QStringLiteral("loaded"), true}, {QStringLiteral("failed"), false}}));
            dirty();
            const Race* r = m_store->race();
            const auto every = weather::refreshSeconds(r ? r->sessions : Sessions(), nowSec(), durationTable(),
                                                       m_settings->weather_refresh, m_settings->weather_custom);
            due[QStringLiteral("weather")] = mono() + (every ? *every : 6 * 3600);
        },
        [this](const QString&, int) { due[QStringLiteral("weather")] = mono() + 1800; });
}

// ── sessions (F1 timing feed: revised start times) ─────────────────────────────

void Poller::loadSessionInfo() {
    if (!m_store->race()) return;
    get(QStringLiteral("session_info"), F1_TIMING + QStringLiteral("SessionInfo.json"), [this](const QJsonValue& info) {
        const auto got = parsers::parseSessionInfo(info, m_store->race());
        if (!got) return;
        if (!m_store->autoTs.contains(got->first) || m_store->autoTs.value(got->first) != got->second) {
            m_store->autoTs.insert(got->first, got->second);
            republishRace();
            dirty();
        }
    });
}

// ── standings, results, grid ─────────────────────────────────────────────────

void Poller::dataset(const QString& name, const QString& url, std::function<QJsonObject(const QJsonValue&)> parse) {
    get(name, url,
        [this, name, parse](const QJsonValue& data) {
            QJsonObject parsed = parse(data);
            parsed.insert(QStringLiteral("loaded"), true);
            parsed.insert(QStringLiteral("failed"), false);
            attempts.remove(name);
            m_store->set(name, parsed);
            dirty();
        },
        [this, name](const QString&, int) {
            due[name] = mono() + retryDelay(name);
            m_store->set(name, failedState(name), false);
        });
}

void Poller::loadGrid(bool force) {
    const Race* race = m_store->race();
    const QString url = JOLPICA + (force ? QStringLiteral("current/last/qualifying.json")
                                         : QStringLiteral("%1/%2/qualifying.json").arg(race->season, race->round));
    m_gridKey = roundKey(race) + (force ? QStringLiteral(":True") : QStringLiteral(":False"));
    get(QStringLiteral("grid"), url,
        [this](const QJsonValue& data) {
            QJsonObject parsed = parsers::parseQualifying(data);
            if (parsed.value(QStringLiteral("rows")).toArray().isEmpty()) {
                // Jolpica has not published qualifying yet: try again in a few minutes.
                m_gridKey.reset();
                m_gridRetryAt = mono() + retryDelay(QStringLiteral("grid"));
                m_store->set(QStringLiteral("grid"),
                             merged(DataStore::empty(QStringLiteral("grid")), {{QStringLiteral("loaded"), true}}), false);
                return;
            }
            parsed.insert(QStringLiteral("loaded"), true);
            parsed.insert(QStringLiteral("failed"), false);
            parsed.insert(QStringLiteral("provisional"), true);
            attempts.remove(QStringLiteral("grid"));
            m_store->set(QStringLiteral("grid"), parsed);
            dirty();
        },
        [this](const QString&, int) {
            m_gridKey.reset();
            m_gridRetryAt = mono() + retryDelay(QStringLiteral("grid"));
            m_store->set(QStringLiteral("grid"), failedState(QStringLiteral("grid")), false);
        },
        true);
}

void Poller::loadMeasuredWeather(qint64 now) {
    // What the weather really was in each finished session (OpenF1 /weather, measured by F1 at the
    // track). One small request per session, once; free plan, after the session.
    const Race* race = m_store->race();
    if (!race || race->sessions.isEmpty()) return;
    const QString key = roundKey(race);
    QMap<QString, qint64> done;
    for (const auto& s : race->sessions)
        if (s.ts + (durationMin(s.id) + 15) * 60LL <= now) done.insert(s.id, s.ts);
    if (done.isEmpty() || now > race->sessions.last().ts + 6 * 3600) return;
    const QJsonObject stored = m_store->get(QStringLiteral("measured_weather"));
    const QJsonObject have = stored.value(QStringLiteral("round")).toString() == key
                                 ? stored.value(QStringLiteral("sessions")).toObject()
                                 : QJsonObject();
    QMap<QString, qint64> ours;
    for (auto it = done.begin(); it != done.end(); ++it)
        if (!have.contains(it.key())) ours.insert(it.key(), it.value());
    if (ours.isEmpty()) return;

    auto gotWeather = [this, key](const QString& sid, const QJsonValue& data) {
        const auto summary = parsers::summariseOpenf1Weather(data);
        if (!summary) return;
        const QJsonObject cur = m_store->get(QStringLiteral("measured_weather"));
        QJsonObject sessions = cur.value(QStringLiteral("round")).toString() == key
                                   ? cur.value(QStringLiteral("sessions")).toObject()
                                   : QJsonObject();
        sessions.insert(sid, *summary);
        m_store->set(QStringLiteral("measured_weather"),
                     {{QStringLiteral("loaded"), true}, {QStringLiteral("failed"), false},
                      {QStringLiteral("round"), key}, {QStringLiteral("sessions"), sessions}});
        dirty();
    };
    get(QStringLiteral("of_wx_sessions"), OPENF1 + QStringLiteral("sessions?meeting_key=latest"),
        [this, ours, gotWeather](const QJsonValue& arr) {
            if (!arr.isArray()) return;
            for (const QJsonValue& ev : arr.toArray()) {
                const QJsonObject e = py::obj(ev);
                if (!ev.isObject() || !isInt(e.value(QStringLiteral("session_key")))) continue;
                const QString sid = parsers::feedSessionId(py::str(e.value(QStringLiteral("session_name"))));
                const auto start = parseIso(e.value(QStringLiteral("date_start")));
                if (ours.contains(sid) && start && std::llabs(*start - ours.value(sid)) <= 3 * 3600) {
                    const qint64 sk = e.value(QStringLiteral("session_key")).toInteger();
                    get(QStringLiteral("of_wx_%1").arg(sk), OPENF1 + QStringLiteral("weather?session_key=%1").arg(sk),
                        [gotWeather, sid](const QJsonValue& data) { gotWeather(sid, data); });
                }
            }
        });
}

void Poller::loadOfficialGrid(qint64 now) {
    // The official starting grid (after penalties) from OpenF1, published with qualifying. Checked
    // every 30 minutes after qualifying and again 60, 20 and 5 minutes before the race.
    const Race* race = m_store->race();
    const auto q = race ? sessionTs(race->sessions, QStringLiteral("quali")) : std::nullopt;
    const auto r = race ? sessionTs(race->sessions, QStringLiteral("race")) : std::nullopt;
    if (!q || !r || !(*q + 70 * 60 <= now && now <= *r + 15 * 60)) return;
    const QString key = roundKey(race);
    const double m = mono();
    const QJsonObject have = m_store->get(QStringLiteral("grid_official"));
    const bool known = !have.value(QStringLiteral("rows")).toArray().isEmpty() && have.value(QStringLiteral("round")).toString() == key;
    double nxt = m + (known ? 3 * 3600 : 30 * 60);   // look often until it appears, then rarely
    for (int before : {60, 20, 5}) {                 // last checks just before lights out
        const qint64 at = *r - before * 60;
        if (now < at) nxt = std::min(nxt, m + (at - now));
    }
    due[QStringLiteral("grid_official")] = nxt;
    const qint64 qTs = *q;
    get(QStringLiteral("of_grid_session"), OPENF1 + QStringLiteral("sessions?meeting_key=latest&session_name=Qualifying"),
        [this, qTs, key](const QJsonValue& arr) {
            const auto sess = parsers::parseOpenf1Session(arr);
            if (!sess || !py::truthy(sess->value(QStringLiteral("key"))) ||
                std::llabs(sess->value(QStringLiteral("start")).toInteger() - qTs) > 3 * 3600)
                return;   // OpenF1's latest meeting is not this race yet
            const qint64 k = sess->value(QStringLiteral("key")).toInteger();
            auto holder = std::make_shared<QJsonValue>();
            get(QStringLiteral("of_grid"), OPENF1 + QStringLiteral("starting_grid?session_key=%1").arg(k),
                [this, k, key, holder](const QJsonValue& grid) {
                    *holder = grid;
                    get(QStringLiteral("of_grid_drivers"), OPENF1 + QStringLiteral("drivers?session_key=%1").arg(k),
                        [this, k, key, holder](const QJsonValue& drivers) {
                            const QJsonArray rows = parsers::parseOpenf1StartingGrid(
                                *holder, parsers::parseOpenf1Drivers(drivers),
                                m_store->get(QStringLiteral("grid")).value(QStringLiteral("rows")).toArray());
                            if (rows.isEmpty()) return;
                            m_store->set(QStringLiteral("grid_official"),
                                         {{QStringLiteral("loaded"), true}, {QStringLiteral("failed"), false},
                                          {QStringLiteral("round"), key}, {QStringLiteral("session_key"), k},
                                          {QStringLiteral("rows"), rows}});
                            dirty();
                        });
                });
        });
}

// ── live: OpenF1 ─────────────────────────────────────────────────────────────

void Poller::publishLive(const QJsonObject& changes) {
    m_store->set(QStringLiteral("live"), merged(m_store->get(QStringLiteral("live")), changes), false);
}

void Poller::liveFailure(int status) {
    QString err;
    if (status == 401 || status == 403) {
        err = m_token.isEmpty() ? QStringLiteral("auth") : QStringLiteral("token");
        m_token.clear();
        m_liveBackoffUntil = mono() + 60;
    } else if (status == 429) {
        err = QStringLiteral("rate");
        m_liveBackoffUntil = mono() + 60;
    } else {
        err = QStringLiteral("net");
    }
    publishLive({{QStringLiteral("error"), err}, {QStringLiteral("source"), QStringLiteral("openf1")}});
}

void Poller::withToken(std::function<void()> fn) {
    // Sponsor plan only: log in for a bearer token. The free plan never sends credentials.
    const auto creds = m_settings->openf1_plan == u"sponsor" ? m_creds(m_settings->openf1_username) : std::nullopt;
    if (!creds) {
        m_token.clear();
        fn();
        return;
    }
    if (!m_token.isEmpty() && wallNow() < m_tokenExpires - 120) {
        fn();
        return;
    }
    if (busy.contains(QStringLiteral("token"))) return;
    busy.insert(QStringLiteral("token"));
    const QByteArray body = "username=" + QUrl::toPercentEncoding(creds->first) +
                            "&password=" + QUrl::toPercentEncoding(creds->second);
    m_http->postForm(
        OPENF1_TOKEN, body,
        [this, fn](const QJsonValue& data) {
            busy.remove(QStringLiteral("token"));
            const QJsonValue tok = data.isObject() ? data.toObject().value(QStringLiteral("access_token")) : QJsonValue();
            if (tok.isString() && !tok.toString().isEmpty()) {
                m_token = tok.toString();
                const QJsonValue exp = data.toObject().value(QStringLiteral("expires_in"));
                const auto secs = py::truthy(exp) ? py::toFloat(exp) : std::optional<double>(3600);
                m_tokenExpires = wallNow() + secs.value_or(3600);
                fn();
            } else {
                loginFailed();
            }
        },
        [this](const QString&, int) {
            busy.remove(QStringLiteral("token"));
            loginFailed();
        });
}

void Poller::loginFailed() {
    m_token.clear();
    m_liveBackoffUntil = mono() + 300;
    publishLive({{QStringLiteral("error"), QStringLiteral("login")}, {QStringLiteral("source"), QStringLiteral("openf1")}});
}

void Poller::composeOpenf1() {
    // Rebuild the running order from the latest positions, drivers and intervals, whatever order
    // their answers arrived in.
    if (!m_ofPositions) return;
    auto [rows, newest] = parsers::parseOpenf1Positions(*m_ofPositions, m_driversMap.value_or(parsers::Drivers()));
    if (m_ofIntervals) parsers::applyOpenf1Intervals(rows, *m_ofIntervals);
    publishLive({{QStringLiteral("rows"), rows}, {QStringLiteral("latest_sec"), newest},
                 {QStringLiteral("error"), QString()}, {QStringLiteral("source"), QStringLiteral("openf1")}});
}

void Poller::pollOpenf1() {
    withToken([this] {
        auto fail = [this](const QString&, int status) { liveFailure(status); };
        if (!m_driversMap) {
            get(QStringLiteral("of_drivers"), OPENF1 + QStringLiteral("drivers?session_key=latest"),
                [this](const QJsonValue& arr) {
                    m_driversMap = parsers::parseOpenf1Drivers(arr);
                    composeOpenf1();
                },
                fail, true, true);
            get(QStringLiteral("of_session"), OPENF1 + QStringLiteral("sessions?session_key=latest"),
                [this](const QJsonValue& arr) {
                    const auto s = parsers::parseOpenf1Session(arr);
                    publishLive({{QStringLiteral("session"), s ? QJsonValue(*s) : QJsonValue()}});
                },
                {}, true, true);
        }
        get(QStringLiteral("of_pos"), OPENF1 + QStringLiteral("position?session_key=latest"),
            [this](const QJsonValue& arr) {
                m_ofPositions = arr;
                composeOpenf1();
            },
            fail, true, true);
        const qint64 newest = m_store->get(QStringLiteral("live")).value(QStringLiteral("latest_sec")).toInteger();
        if (newest) {
            // Only the last minute: a whole race of intervals is several MB.
            const QString since = QDateTime::fromSecsSinceEpoch(newest - 60, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
            get(QStringLiteral("of_int"), OPENF1 + QStringLiteral("intervals?session_key=latest&date>=") + since,
                [this](const QJsonValue& arr) {
                    m_ofIntervals = arr;
                    composeOpenf1();
                },
                {}, true, true);
        }
        get(QStringLiteral("of_flag"), OPENF1 + QStringLiteral("race_control?session_key=latest"),
            [this](const QJsonValue& arr) {
                publishLive({{QStringLiteral("flag"), parsers::parseOpenf1Flag(arr)},
                             {QStringLiteral("messages"), QJsonArray::fromStringList(parsers::parseOpenf1RaceControl(arr))}});
            },
            {}, true, true);
    });
}

// ── live: experimental F1 stream ─────────────────────────────────────────────

void Poller::runF1Stream() {
    // Lazy: only when the user opted in, and only near a session (or with debug tabs).
    QNetworkAccessManager* nam = m_http->networkManager();
    if (!nam) {
        publishLive({{QStringLiteral("error"), QStringLiteral("net")}, {QStringLiteral("source"), QStringLiteral("f1_feed")}});
        return;
    }
    if (!m_stream) {
        m_stream = new F1Stream(nam, streamBase, this);
        connect(m_stream, &F1Stream::updated, this, &Poller::streamUpdated);
        connect(m_stream, &F1Stream::failed, this, [this](const QString& err) {
            publishLive({{QStringLiteral("error"), err}, {QStringLiteral("source"), QStringLiteral("f1_feed")}});
        });
    }
    m_stream->ensureRunning();
}

QMap<QString, QJsonObject> Poller::knownDrivers() const {
    // Driver number -> code / colour / team from data already loaded: the last session's results,
    // the official grid and OpenF1's driver list (the later source wins).
    QMap<QString, QJsonObject> known;
    for (const char* key : {"last_session", "grid_official"})
        for (const QJsonValue& v : py::arr(m_store->get(QString::fromLatin1(key)).value(QStringLiteral("rows")))) {
            const QJsonObject r = v.toObject();
            if (!v.isObject() || !py::truthy(r.value(QStringLiteral("num"))) || !py::truthy(r.value(QStringLiteral("code")))) continue;
            known[py::str(r.value(QStringLiteral("num")))] = {
                {"code", r.value(QStringLiteral("code"))},
                {"color", r.contains(QStringLiteral("color")) ? r.value(QStringLiteral("color")) : QJsonValue(QString())},
                {"team", r.contains(QStringLiteral("team")) ? r.value(QStringLiteral("team")) : QJsonValue(QString())}};
        }
    if (m_driversMap)
        for (auto it = m_driversMap->cbegin(); it != m_driversMap->cend(); ++it) {
            const QJsonObject& d = it.value();
            if (!py::truthy(d.value(QStringLiteral("acr")))) continue;
            known[it.key()] = {{"code", d.value(QStringLiteral("acr"))},
                               {"color", d.contains(QStringLiteral("color")) ? d.value(QStringLiteral("color")) : QJsonValue(QString())},
                               {"team", d.contains(QStringLiteral("team")) ? d.value(QStringLiteral("team")) : QJsonValue(QString())}};
        }
    return known;
}

void Poller::streamUpdated() {
    if (!m_stream) return;
    const auto view = m_stream->state.view(nowSec());
    if (!view) return;
    QJsonArray rows = view->value(QStringLiteral("rows")).toArray();
    fillDriverNames(rows, knownDrivers());
    publishLive({{QStringLiteral("rows"), rows},
                 {QStringLiteral("flag"), view->value(QStringLiteral("flag"))},
                 {QStringLiteral("latest_sec"), view->value(QStringLiteral("latest_sec"))},
                 {QStringLiteral("session"), view->value(QStringLiteral("session"))},
                 {QStringLiteral("clock"), view->value(QStringLiteral("clock"))},
                 {QStringLiteral("weather"), view->value(QStringLiteral("weather"))},
                 {QStringLiteral("error"), QString()},
                 {QStringLiteral("source"), QStringLiteral("f1_feed")}});
}

void Poller::stopStream() {
    if (m_stream) m_stream->stop();
}

// ── OpenF1: last finished session (works on the free plan) ────────────────────

bool Poller::openf1Restricted(const Sessions& sessions, qint64 now) const {
    // Free plan: OpenF1 serves nothing while a session runs, so stay quiet from 15 min before a
    // session until 20 min after its usual end.
    if (m_settings->openf1_plan == u"sponsor") return false;
    return std::any_of(sessions.begin(), sessions.end(), [now](const Session& s) {
        return s.ts - 15 * 60 <= now && now <= s.ts + (durationMin(s.id) + 20) * 60LL;
    });
}

void Poller::loadLastSession() {
    const qint64 now = nowSec();
    auto failed = [this](const QString&, int status) {
        if (status == 401 || status == 403) resultDue = mono() + RESULT_RETRY_S;   // a session went live meanwhile
        else resultDue = mono() + retryDelay(QStringLiteral("last_session"));
    };
    get(QStringLiteral("of_res_session"), OPENF1 + QStringLiteral("sessions?session_key=latest"),
        [this, now, failed](const QJsonValue& arr) {
            const auto sess = parsers::parseOpenf1Session(arr);
            if (!sess || !py::truthy(sess->value(QStringLiteral("key")))) {
                resultDue = mono() + RESULT_RETRY_S;
                return;
            }
            const qint64 end = sess->value(QStringLiteral("end")).toInteger();
            if (end && end + 10 * 60 > now) {   // still running (or just ended): results come after it
                resultDue = mono() + std::max<double>(RESULT_RETRY_S, end + 20 * 60 - now);
                return;
            }
            const QJsonObject prev = m_store->get(QStringLiteral("last_session"));
            const QJsonValue prevKey = py::obj(prev.value(QStringLiteral("session"))).value(QStringLiteral("key"));
            // (A cached session without its meeting number predates the session pills: refresh it once.)
            if (prevKey == sess->value(QStringLiteral("key")) && py::truthy(prev.value(QStringLiteral("rows"))) &&
                !py::truthy(prev.value(QStringLiteral("partial"))) &&
                py::truthy(py::obj(prev.value(QStringLiteral("session"))).value(QStringLiteral("meeting")))) {
                resultDue = mono() + RESULT_REFRESH_S;
                return;
            }
            const qint64 key = sess->value(QStringLiteral("key")).toInteger();
            const QJsonObject session = *sess;
            get(QStringLiteral("of_res_drivers"), OPENF1 + QStringLiteral("drivers?session_key=%1").arg(key),
                [this, key, session, failed](const QJsonValue& arr2) {
                    const auto drivers = parsers::parseOpenf1Drivers(arr2);
                    get(QStringLiteral("of_result"), OPENF1 + QStringLiteral("session_result?session_key=%1").arg(key),
                        [this, drivers, session](const QJsonValue& arr3) {
                            const QJsonArray rows = parsers::parseOpenf1SessionResult(
                                arr3, drivers, session.value(QStringLiteral("kind")).toString());
                            if (rows.isEmpty()) {
                                resultDue = mono() + RESULT_RETRY_S;
                                return;
                            }
                            attempts.remove(QStringLiteral("last_session"));
                            resultDue = mono() + RESULT_REFRESH_S;
                            m_store->set(QStringLiteral("last_session"),
                                         {{QStringLiteral("loaded"), true}, {QStringLiteral("failed"), false},
                                          {QStringLiteral("session"), session}, {QStringLiteral("rows"), rows}});
                            dirty();
                        },
                        failed);
                },
                failed);
        },
        failed);
}

// ── every session of a weekend (session pills in Live and Last) ───────────────

void Poller::loadLastRaceExtras() {
    // Jolpica's qualifying and sprint classifications of the round the "results" key holds.
    const QJsonObject res = m_store->get(QStringLiteral("results"));
    const QString season = res.value(QStringLiteral("season")).toString(), round = res.value(QStringLiteral("round")).toString();
    if (season.isEmpty() || round.isEmpty()) return;
    const QString key = season + u'-' + round;
    if (m_extrasKey == key) return;
    m_extrasKey = key;
    dataset(QStringLiteral("last_qualifying"), JOLPICA + QStringLiteral("%1/%2/qualifying.json").arg(season, round),
            parsers::parseQualifying);
    dataset(QStringLiteral("last_sprint"), JOLPICA + QStringLiteral("%1/%2/sprint.json").arg(season, round),
            [](const QJsonValue& d) { return parsers::parseResults(d, "SprintResults"); });
}

void Poller::loadWeekendStep(qint64 now) {
    QJsonObject store = m_store->get(QStringLiteral("weekends"));
    QJsonObject meetings = store.value(QStringLiteral("meetings")).toObject();
    QJsonObject rounds = store.value(QStringLiteral("rounds")).toObject();
    auto save = [this](QJsonObject meetingsNow, QJsonObject roundsNow, const QList<qint64>& wanted) {
        m_store->set(QStringLiteral("weekends"),
                     {{QStringLiteral("loaded"), true}, {QStringLiteral("failed"), false},
                      {QStringLiteral("meetings"), weekend::prune(meetingsNow, wanted)}, {QStringLiteral("rounds"), roundsNow}});
        dirty();
    };
    auto again = [this](double seconds) { weekendDue = mono() + seconds; };
    auto failed = [this](const QString&, int) { weekendDue = mono() + retryDelay(QStringLiteral("weekend")); };

    // The meetings wanted: the latest session's (Live) and the last race's (Last).
    QList<qint64> wanted;
    const qint64 liveMeeting =
        py::toInt(py::obj(m_store->get(QStringLiteral("last_session")).value(QStringLiteral("session"))).value(QStringLiteral("meeting")))
            .value_or(0);
    if (liveMeeting) wanted << liveMeeting;
    const QJsonObject res = m_store->get(QStringLiteral("results"));
    const QString season = res.value(QStringLiteral("season")).toString();
    const QString roundKey = season + u'-' + res.value(QStringLiteral("round")).toString();
    const qint64 raceTs = py::toInt(res.value(QStringLiteral("date_ts"))).value_or(0);
    if (!season.isEmpty() && raceTs) {
        if (!rounds.contains(roundKey)) {
            // Which OpenF1 meeting is Jolpica's last race? Match the race start.
            get(QStringLiteral("of_weekend"), OPENF1 + QStringLiteral("sessions?year=%1&session_name=Race").arg(season),
                [this, store, roundKey, raceTs, save, again, wanted](const QJsonValue& arr) mutable {
                    QJsonObject r = store.value(QStringLiteral("rounds")).toObject();
                    const auto mk = weekend::meetingForRace(parsers::parseOpenf1Sessions(arr), raceTs);
                    r.insert(roundKey, mk ? QJsonValue(*mk) : QJsonValue(0));   // 0: OpenF1 does not have it
                    attempts.remove(QStringLiteral("weekend"));
                    if (mk && !wanted.contains(*mk)) wanted << *mk;
                    save(store.value(QStringLiteral("meetings")).toObject(), r, wanted);
                    again(2);
                },
                failed);
            return;
        }
        if (const qint64 mk = rounds.value(roundKey).toInteger(); mk && !wanted.contains(mk)) wanted << mk;
    }
    // Keep only this round's mapping.
    if (rounds.size() > 1) rounds = QJsonObject{{roundKey, rounds.value(roundKey)}};

    for (const qint64 mk : wanted) {
        const QString key = QString::number(mk);
        QJsonObject meeting = meetings.value(key).toObject();
        if (weekend::needsListing(meeting, now)) {
            get(QStringLiteral("of_weekend"), OPENF1 + QStringLiteral("sessions?meeting_key=%1").arg(mk),
                [this, meetings, rounds, meeting, key, now, save, again, wanted](const QJsonValue& arr) mutable {
                    const QJsonArray list = parsers::parseOpenf1Sessions(arr);
                    if (list.isEmpty()) {
                        again(RESULT_RETRY_S);
                        return;
                    }
                    meeting.insert(QStringLiteral("sessions"), list);
                    meeting.insert(QStringLiteral("location"), list.first().toObject().value(QStringLiteral("location")));
                    meeting.insert(QStringLiteral("listed"), now);
                    meetings.insert(key, meeting);
                    attempts.remove(QStringLiteral("weekend"));
                    save(meetings, rounds, wanted);
                    again(2);
                },
                failed);
            return;
        }
        const auto next = weekend::nextResultToFetch(meeting, now);
        if (!next) continue;
        if (meeting.value(QStringLiteral("drivers")).toObject().isEmpty()) {
            get(QStringLiteral("of_weekend"), OPENF1 + QStringLiteral("drivers?meeting_key=%1").arg(mk),
                [this, meetings, rounds, meeting, key, save, again, wanted](const QJsonValue& arr) mutable {
                    QJsonObject drivers;
                    const auto map = parsers::parseOpenf1Drivers(arr);
                    for (auto it = map.cbegin(); it != map.cend(); ++it) drivers.insert(it.key(), it.value());
                    if (drivers.isEmpty()) {
                        again(RESULT_RETRY_S);
                        return;
                    }
                    meeting.insert(QStringLiteral("drivers"), drivers);
                    meetings.insert(key, meeting);
                    save(meetings, rounds, wanted);
                    again(2);
                },
                failed);
            return;
        }
        QString kind;
        for (const QJsonValue& v : meeting.value(QStringLiteral("sessions")).toArray())
            if (v.toObject().value(QStringLiteral("key")).toInteger() == *next) kind = v.toObject().value(QStringLiteral("kind")).toString();
        auto record = [this, meetings, rounds, meeting, key, kind, next, now, save, again, wanted](const QJsonValue& arr) mutable {
                parsers::Drivers drivers;
                const QJsonObject d = meeting.value(QStringLiteral("drivers")).toObject();
                for (auto it = d.begin(); it != d.end(); ++it) drivers.insert(it.key(), it.value().toObject());
                const QJsonArray rows = parsers::parseOpenf1SessionResult(arr, drivers, kind);
                QJsonObject results = meeting.value(QStringLiteral("results")).toObject();
                // An empty answer is remembered too (fetched again later), so one session OpenF1
                // has no classification for never blocks the others.
                results.insert(QString::number(*next), QJsonObject{{QStringLiteral("rows"), rows}, {QStringLiteral("fetched"), now}});
                meeting.insert(QStringLiteral("results"), results);
                meetings.insert(key, meeting);
                attempts.remove(QStringLiteral("weekend"));
                save(meetings, rounds, wanted);
                again(2);
        };
        // OpenF1 answers 404 when it has no classification for a session (it happens, e.g. a
        // practice): remember that like an empty answer instead of retrying it forever.
        get(QStringLiteral("of_weekend"), OPENF1 + QStringLiteral("session_result?session_key=%1").arg(*next), record,
            [record, failed](const QString& why, int status) mutable {
                if (status == 404) record(QJsonArray());
                else failed(why, status);
            });
        return;
    }
    // Nothing to do: look again when the next session's classification is due.
    double wait = 30 * 60;
    for (const qint64 mk : wanted)
        for (const QJsonValue& v : meetings.value(QString::number(mk)).toObject().value(QStringLiteral("sessions")).toArray()) {
            const qint64 left = v.toObject().value(QStringLiteral("end")).toInteger() + weekend::RESULTS_AFTER_S - now;
            if (left > 0) wait = std::min<double>(wait, left + 30);
        }
    again(std::max(60.0, wait));
}

// ── scheduler ────────────────────────────────────────────────────────────────

void Poller::update() {
    const qint64 now = nowSec();
    const double m = mono();
    // Sleep/resume or clock change: the wall clock jumped ahead of the monotonic one.
    if ((now - m_lastWall) - (m - m_lastMono) > WAKE_JUMP_S || now - m_lastWall > WAKE_JUMP_S) {
        qCInfo(lcPoll) << "clock jump detected (sleep/resume?); refreshing";
        m_lastWall = now;
        m_lastMono = m;
        Q_EMIT woke();
        refreshAll(true);
        return;
    }
    m_lastWall = now;
    m_lastMono = m;

    if (m >= due.value(QStringLiteral("schedule"))) {
        due[QStringLiteral("schedule")] = m + 3600;   // placeholder until the answer sets it
        loadSchedule();
    }
    const double standingsEvery = m_settings->standings_refresh_h * 3600;
    if (m >= due.value(QStringLiteral("drivers"))) {
        due[QStringLiteral("drivers")] = m + standingsEvery;
        dataset(QStringLiteral("drivers"), JOLPICA + QStringLiteral("current/driverStandings.json"), parsers::parseDriverStandings);
    }
    if (m >= due.value(QStringLiteral("constructors"))) {
        due[QStringLiteral("constructors")] = m + standingsEvery;
        dataset(QStringLiteral("constructors"), JOLPICA + QStringLiteral("current/constructorStandings.json"),
                parsers::parseConstructorStandings);
    }
    if (m >= due.value(QStringLiteral("results"))) {
        due[QStringLiteral("results")] = m + standingsEvery;
        dataset(QStringLiteral("results"), JOLPICA + QStringLiteral("current/last/results.json"),
                [](const QJsonValue& d) { return parsers::parseResults(d); });
    }
    if (m >= due.value(QStringLiteral("measured"))) {
        due[QStringLiteral("measured")] = m + 20 * 60;
        loadMeasuredWeather(now);
    }
    if (m >= due.value(QStringLiteral("grid_official"))) {
        due[QStringLiteral("grid_official")] = m + 30 * 60;
        loadOfficialGrid(now);
    }
    if (m >= due.value(QStringLiteral("weather"))) {
        due[QStringLiteral("weather")] = m + WEATHER_EVERY_S;
        loadWeather(now);
    }

    const Race* race = m_store->race();
    const Sessions sessions = race ? race->sessions : Sessions();
    if (m_store->status.value(QStringLiteral("loading")).toBool() && busy.isEmpty() && race) m_store->setStatus(false);

    // After a race the standings, result and schedule change: refresh once per round.
    const auto raceTs = sessionTs(sessions, QStringLiteral("race"));
    if (race && raceTs && now > *raceTs + 3 * 3600 + 900 && m_postRaceRound != roundKey(race)) {
        m_postRaceRound = roundKey(race);
        for (auto it = due.begin(); it != due.end(); ++it) it.value() = m;
    }

    // F1's timing feed: follow delays during the weekend, once a minute.
    if (race) {
        const auto win = weekendWindow(race->rawSessions);
        if (win && win->first - 3 * 3600 <= now && now <= win->second + 4 * 3600 && m - m_sessionInfoAt >= SESSION_INFO_EVERY_S) {
            m_sessionInfoAt = m;
            loadSessionInfo();
        }
    }

    const bool force = m_settings->debug_force_tabs;
    if (race && (force || gridApplicable(sessions, now))) {
        const QString key = roundKey(race) + (force ? QStringLiteral(":True") : QStringLiteral(":False"));
        if (m >= m_gridRetryAt && m_gridKey != key) loadGrid(force);
    }

    // Last finished session from OpenF1 (free plan friendly). Look again once a scheduled session
    // has ended, otherwise every few hours.
    qint64 finished = 0;
    for (const auto& s : sessions)
        if (now > s.ts + (durationMin(s.id) + 20) * 60LL && s.ts > m_resultWantedTs) finished = std::max(finished, s.ts);
    if (finished) {
        m_resultWantedTs = finished;
        resultDue = std::min(resultDue, m);
    }
    const bool resultBusy = busy.contains(QStringLiteral("of_res_session")) || busy.contains(QStringLiteral("of_res_drivers")) ||
                            busy.contains(QStringLiteral("of_result"));
    if (m >= resultDue && !openf1Restricted(sessions, now) && !resultBusy) {
        resultDue = m + RESULT_RETRY_S;   // replaced by the answer
        loadLastSession();
    }

    // Every session of the current and the last race weekend, for the session pills.
    if (m >= weekendDue && !openf1Restricted(sessions, now) && !busy.contains(QStringLiteral("of_weekend"))) {
        weekendDue = m + 30 * 60;   // replaced when there is more to do
        loadWeekendStep(now);
    }
    loadLastRaceExtras();

    if (nearSession(sessions, now) || force) {
        if (m_settings->live_source == u"f1_feed") {
            runF1Stream();
        } else {
            stopStream();
            const bool sponsor = m_settings->openf1_plan == u"sponsor";
            if (!sponsor && !force) {
                // Free plan: no live data during sessions. Say so instead of polling.
                if (m_store->get(QStringLiteral("live")).value(QStringLiteral("error")).toString() != u"free")
                    publishLive({{QStringLiteral("error"), QStringLiteral("free")}, {QStringLiteral("source"), QStringLiteral("openf1")}});
            } else if (m - m_lastLive >= LIVE_EVERY_S && m >= m_liveBackoffUntil) {
                m_lastLive = m;
                pollOpenf1();
            }
        }
    } else {
        stopStream();
        const QJsonObject live = m_store->get(QStringLiteral("live"));
        if (py::truthy(live.value(QStringLiteral("rows"))) || py::truthy(live.value(QStringLiteral("latest_sec"))) ||
            py::truthy(live.value(QStringLiteral("error"))))
            resetLive();
    }
    Q_EMIT tick(now);
}

}  // namespace f1
