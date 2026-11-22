#pragma once
// Track weather (port of f1win/core/weather.py).
//
// Forecast: Open-Meteo (https://open-meteo.com, free, no key, CC BY 4.0) for the circuit's
// coordinates, hourly up to 16 days ahead. Live: the WeatherData topic of F1's live timing stream.

#include "sessions.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <array>
#include <optional>
#include <utility>

namespace f1::weather {

inline constexpr int FORECAST_DAYS = 16;
inline constexpr int PAST_DAYS = 3;
inline constexpr int MAX_HOURS = (FORECAST_DAYS + PAST_DAYS) * 24 + 24;

using Opt = std::optional<double>;

QString forecastUrl(double lat, double lon);

// One forecast hour (also the common part of a session window).
struct Hour {
    qint64 ts = 0;
    Opt temp, rainProb, rain, wind;
    std::optional<int> code;
};

struct Forecast {
    QString round;                  // "2026-21" once stored for a race weekend
    QList<qint64> times;
    QList<Opt> temp, rainProb, rain, wind;
    QList<std::optional<int>> code;

    QJsonObject toJson() const;     // Python cache shape: {"times": [...], "temp": [...], ...}
    static std::optional<Forecast> fromJson(const QJsonObject& o);
    Hour hourAt(qsizetype i) const;
};

struct Window : Hour {
    double tempMin = 0, tempMax = 0, rainTotal = 0;
    QList<Hour> hours;
    std::optional<qint64> wetFrom;
};

// Open-Meteo JSON -> equal-length columns, or nullopt.
std::optional<Forecast> parseOpenMeteo(const QJsonValue& data);
// The forecast hour nearest to ts (within 90 minutes).
std::optional<Hour> forecastAt(const Forecast* fc, qint64 ts);
// Summary over a session: whether it rains at any point, not just at the start.
std::optional<Window> forecastWindow(const Forecast* fc, qint64 start, qint64 end);

int severity(std::optional<int> code);
// (verdict, level) where level is "dry", "risk" or "wet".
std::pair<QString, QString> outlook(const std::optional<Window>& w);

std::array<int, 3> timings(const QString& speed, const QJsonArray& custom = {});
QString fmtMinutes(int m);
// How soon to fetch the forecast again in seconds (nullopt = never: updates are off).
std::optional<int> refreshSeconds(const Sessions& sessions, qint64 now, const QMap<QString, int>& durations,
                                  const QString& speed = QStringLiteral("normal"), const QJsonArray& custom = {});
QString refreshText(const QString& speed, const QJsonArray& custom = {});

QString condition(std::optional<int> code);
bool isWet(const Hour& h);
bool isWet(const std::optional<Hour>& h);

QString temp(Opt c, const QString& units = QStringLiteral("metric"), bool unit = false);
QString tempRange(double lo, double hi, const QString& units = QStringLiteral("metric"), bool unit = false);
QString wind(Opt kmh, const QString& units = QStringLiteral("metric"));
QString shortText(const Hour& h, const QString& units = QStringLiteral("metric"));

struct LiveWeather {
    Opt air, track, humidity, wind, windDir;
    bool rain = false;
    QJsonObject toJson() const;
    static LiveWeather fromJson(const QJsonObject& o);
};
// F1 WeatherData -> numbers / bool, or nullopt without air and track temperatures.
std::optional<LiveWeather> parseLiveWeather(const QJsonValue& d);

}  // namespace f1::weather
