#include "store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace f1 {

namespace {
double wallNow() { return QDateTime::currentMSecsSinceEpoch() / 1000.0; }
}  // namespace

const QStringList& DataStore::persisted() {
    static const QStringList keys = {QStringLiteral("drivers"),      QStringLiteral("constructors"),
                                     QStringLiteral("results"),      QStringLiteral("grid"),
                                     QStringLiteral("last_session"), QStringLiteral("weather"),
                                     QStringLiteral("grid_official"), QStringLiteral("measured_weather"),
                                     QStringLiteral("weekends"),     QStringLiteral("last_qualifying"),
                                     QStringLiteral("last_sprint")};
    return keys;
}

QJsonObject DataStore::empty(const QString& key) {
    if (key == u"live")
        return {{QStringLiteral("rows"), QJsonArray()}, {QStringLiteral("flag"), QString()},
                {QStringLiteral("latest_sec"), 0},      {QStringLiteral("error"), QString()},
                {QStringLiteral("source"), QStringLiteral("openf1")}, {QStringLiteral("session"), QJsonValue()},
                {QStringLiteral("clock"), QJsonValue()}, {QStringLiteral("messages"), QJsonArray()}};
    return {{QStringLiteral("loaded"), false}, {QStringLiteral("failed"), false}, {QStringLiteral("rows"), QJsonArray()}};
}

DataStore::DataStore(QString readFile_, QString writeFile_, QObject* parent)
    : QObject(parent), readFile(std::move(readFile_)), writeFile(std::move(writeFile_)) {
    for (const QString& k : persisted()) m_data.insert(k, empty(k));
    m_data.insert(QStringLiteral("live"), empty(QStringLiteral("live")));
}

QJsonObject DataStore::get(const QString& key) const {
    auto it = m_data.constFind(key);
    return (it != m_data.constEnd() && !it->isEmpty()) ? *it : empty(key);
}

void DataStore::set(const QString& key, const QJsonObject& value, bool success) {
    m_data.insert(key, value);
    if (success && key != u"live") fetched.insert(key, wallNow());
    if (key == u"weather") refreshForecast();
    Q_EMIT changed(key);
}

void DataStore::setRace(const std::optional<Race>& race, bool success) {
    m_race = race;
    if (success) fetched.insert(QStringLiteral("race"), wallNow());
    Q_EMIT changed(QStringLiteral("race"));
}

void DataStore::setStatus(std::optional<bool> loading, std::optional<QString> error, std::optional<bool> offline) {
    if (loading) status.insert(QStringLiteral("loading"), *loading);
    if (error) status.insert(QStringLiteral("error"), *error);
    if (offline) status.insert(QStringLiteral("offline"), *offline);
    Q_EMIT changed(QStringLiteral("status"));
}

double DataStore::lastUpdated() const {
    double best = 0;
    for (auto it = fetched.begin(); it != fetched.end(); ++it)
        if (it.key() == u"race" || persisted().contains(it.key())) best = std::max(best, it.value());
    return best;
}

void DataStore::refreshForecast() {
    const QJsonObject w = m_data.value(QStringLiteral("weather"));
    m_forecast = w.value(QStringLiteral("times")).isArray() ? weather::Forecast::fromJson(w) : std::nullopt;
}

bool DataStore::saveCache() const {
    if (writeFile.isEmpty()) return false;
    QJsonObject autoObj, fetchedObj, data;
    for (auto it = autoTs.begin(); it != autoTs.end(); ++it) autoObj.insert(it.key(), it.value());
    for (auto it = fetched.begin(); it != fetched.end(); ++it) fetchedObj.insert(it.key(), it.value());
    for (const QString& k : persisted())
        if (m_data.value(k).value(QStringLiteral("loaded")).toBool()) data.insert(k, m_data.value(k));
    const QJsonObject payload{{QStringLiteral("version"), CACHE_VERSION},
                              {QStringLiteral("saved"), wallNow()},
                              {QStringLiteral("race"), m_race ? QJsonValue(m_race->toJson()) : QJsonValue()},
                              {QStringLiteral("auto_ts"), autoObj},
                              {QStringLiteral("fetched"), fetchedObj},
                              {QStringLiteral("data"), data}};
    QDir().mkpath(QFileInfo(writeFile).absolutePath());
    QSaveFile f(writeFile);   // atomic replace, like the Python tempfile + os.replace
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return f.commit();
}

bool DataStore::loadCache() {
    QFile f(readFile);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject payload = doc.object();
    if (payload.value(QStringLiteral("version")).toInteger(-1) != CACHE_VERSION) return false;
    const QJsonValue rv = payload.value(QStringLiteral("race"));
    if (rv.isObject()) m_race = Race::fromJson(rv.toObject());
    const QJsonObject a = payload.value(QStringLiteral("auto_ts")).toObject();
    for (auto it = a.begin(); it != a.end(); ++it) autoTs.insert(it.key(), it.value().toInteger());
    const QJsonObject fe = payload.value(QStringLiteral("fetched")).toObject();
    for (auto it = fe.begin(); it != fe.end(); ++it) fetched.insert(it.key(), it.value().toDouble());
    const QJsonObject data = payload.value(QStringLiteral("data")).toObject();
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (!persisted().contains(it.key()) || !it.value().isObject()) continue;
        QJsonObject v = it.value().toObject();
        // Tables need their rows; the weather and the weekend data have their own shape.
        const bool rowless = it.key() == u"weather" || it.key() == u"measured_weather" || it.key() == u"weekends";
        if (v.value(QStringLiteral("rows")).isArray() || rowless) {
            v.insert(QStringLiteral("failed"), false);
            m_data.insert(it.key(), v);
        }
    }
    refreshForecast();
    fromCache = true;
    status.insert(QStringLiteral("loading"), false);
    return true;
}

void DataStore::clearCache() {
    if (!writeFile.isEmpty()) QFile::remove(writeFile);
    fetched.clear();
}

}  // namespace f1
