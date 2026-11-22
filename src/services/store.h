#pragma once
// Shared in-process data store + disk cache (port of services/store.py). Same cache.json shape
// (version 2), so the C++ and Python builds can read each other's cache.

#include "../core/sessions.h"
#include "../core/weather.h"

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <optional>

namespace f1 {

inline constexpr int CACHE_VERSION = 2;

class DataStore : public QObject {
    Q_OBJECT
public:
    // `readFile` is loaded at start; `writeFile` receives saves (empty = never write).
    explicit DataStore(QString readFile, QString writeFile, QObject* parent = nullptr);

    static const QStringList& persisted();
    static QJsonObject empty(const QString& key);

    QJsonObject get(const QString& key) const;
    void set(const QString& key, const QJsonObject& value, bool success = true);
    void setRace(const std::optional<Race>& race, bool success = true);
    void setStatus(std::optional<bool> loading, std::optional<QString> error = std::nullopt,
                   std::optional<bool> offline = std::nullopt);
    double lastUpdated() const;

    const Race* race() const { return m_race ? &*m_race : nullptr; }
    Race* raceMut() { return m_race ? &*m_race : nullptr; }
    // The forecast for the pill (parsed from data["weather"]), or null.
    const weather::Forecast* forecast() const { return m_forecast ? &*m_forecast : nullptr; }

    bool saveCache() const;
    bool loadCache();
    void clearCache();   // deletes the file this build writes; the next fetch fills it again

    QMap<QString, qint64> autoTs;     // session id -> feed start (current round)
    QMap<QString, double> fetched;    // key -> wall-clock seconds of last success
    QJsonObject status{{QStringLiteral("loading"), true}, {QStringLiteral("error"), QString()},
                       {QStringLiteral("offline"), false}};
    bool fromCache = false;
    QString readFile, writeFile;

Q_SIGNALS:
    void changed(const QString& key);

private:
    void refreshForecast();
    std::optional<Race> m_race;
    QMap<QString, QJsonObject> m_data;
    std::optional<weather::Forecast> m_forecast;
};

}  // namespace f1
