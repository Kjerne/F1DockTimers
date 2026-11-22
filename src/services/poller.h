#pragma once
// The only component that touches the network (port of services/poller.py).
//
// Ticks once a second (cheap: it only compares deadlines) and starts requests when they are due.
// Every request is async; failures back off 1, 2, 4 ... min (max 15) and keep the previously
// loaded rows. The experimental F1 live-timing stream client is not ported yet (its parser is).

#include "../settings.h"
#include "credentials.h"
#include "f1stream.h"
#include "httpapi.h"
#include "store.h"
#include "../core/parsers.h"

#include <QElapsedTimer>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>
#include <optional>

namespace f1 {

inline constexpr int LIVE_EVERY_S = 15;            // OpenF1 allows 30 requests a minute; 4 per poll
inline constexpr int SESSION_INFO_EVERY_S = 60;
inline constexpr double MIN_REFRESH_GAP_S = 5;
inline constexpr int WEATHER_EVERY_S = 2 * 3600;   // forecast refresh (Open-Meteo updates hourly)
inline constexpr int RESULT_RETRY_S = 600;         // session results not published yet: look again in 10 min
inline constexpr int RESULT_REFRESH_S = 6 * 3600;
inline constexpr qint64 WAKE_JUMP_S = 90;          // wall clock moved this much more than the monotonic clock

QString roundKey(const Race* r);
// The live feed sometimes starts without its driver list, so rows show "#6". Fill the code, colour
// and team from drivers the app already knows ({number: {code, color, team}}), never overwriting
// a name the feed has (port of fill_driver_names in the Python app's poller).
void fillDriverNames(QJsonArray& rows, const QMap<QString, QJsonObject>& known);

class Poller : public QObject {
    Q_OBJECT
public:
    using CredentialSource = std::function<std::optional<credentials::Pair>(const QString& username)>;

    Poller(DataStore* store, Settings* settings, HttpApi* http, QObject* parent = nullptr,
           CredentialSource creds = credentials::resolve);
    void start();
    void stop();
    void refreshAll(bool force = false);
    void settingsChanged();
    void republishRace();
    QJsonObject manualOffsets() const;
    void update();

    // Exposed for the tests, as in the Python suite.
    bool openf1Restricted(const Sessions& sessions, qint64 now) const;
    void pollOpenf1();
    void loadOfficialGrid(qint64 now);
    void loadMeasuredWeather(qint64 now);
    void loadWeather(qint64 now);
    void loadLastSession();
    void loadWeekendStep(qint64 now);    // one request towards every session's classification
    void loadLastRaceExtras();           // Jolpica qualifying and sprint for the last race
    double mono() const { return m_clock.elapsed() / 1000.0 + m_monoOffset; }
    void advanceClockForTests(double seconds) { m_monoOffset += seconds; }
    // The experimental F1 stream (created lazily when the user opted in), and its endpoint.
    F1Stream* stream() const { return m_stream; }
    QString streamBase = QString::fromLatin1(F1_STREAM_BASE);

    QMap<QString, double> due;
    QMap<QString, int> attempts;
    QSet<QString> busy;
    double resultDue = 0;
    double weekendDue = 0;   // next step of the weekend loader (session lists and classifications)

Q_SIGNALS:
    void tick(qint64 now);   // once a second, for countdowns
    void woke();

private:
    void get(const QString& name, const QString& url, HttpApi::Ok ok, HttpApi::Fail fail = {}, bool scoped = false,
             bool auth = false);
    void loadSchedule();
    void loadNextSeason();
    void acceptRace(const Race& race);
    void dataset(const QString& name, const QString& url, std::function<QJsonObject(const QJsonValue&)> parse);
    void loadGrid(bool force);
    QString m_extrasKey;                 // season-round the extras were fetched for
    void loadSessionInfo();
    double retryDelay(const QString& name);
    QJsonObject failedState(const QString& key) const;
    void scheduleFromCache();
    void resetPerRace();
    void resetLive();
    void publishLive(const QJsonObject& changes);
    void liveFailure(int status);
    void withToken(std::function<void()> fn);
    void loginFailed();
    void composeOpenf1();
    void runF1Stream();
    void streamUpdated();
    QMap<QString, QJsonObject> knownDrivers() const;
    void stopStream();
    void dirty();

    DataStore* m_store;
    Settings* m_settings;
    HttpApi* m_http;
    CredentialSource m_creds;
    int m_generation = 0;
    std::optional<QString> m_gridKey;
    double m_gridRetryAt = 0;
    std::optional<QString> m_postRaceRound;
    double m_sessionInfoAt = -1e9;
    double m_lastLive = -1e9;
    double m_liveBackoffUntil = 0;
    double m_lastRefresh = -1e9;
    std::optional<parsers::Drivers> m_driversMap;
    std::optional<QJsonValue> m_ofPositions, m_ofIntervals;
    F1Stream* m_stream = nullptr;
    QString m_token;   // OpenF1 bearer token; never logged
    double m_tokenExpires = 0;
    qint64 m_resultWantedTs = 0;
    bool m_coordsRefetched = false;
    qint64 m_lastWall = 0;
    double m_lastMono = 0;
    double m_monoOffset = 0;
    QTimer m_timer, m_saveTimer;
    QElapsedTimer m_clock;
};

}  // namespace f1
