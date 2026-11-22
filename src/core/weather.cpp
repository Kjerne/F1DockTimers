#include "weather.h"

#include "i18n.h"
#include "pyjson.h"

#include <algorithm>
#include <cmath>

namespace f1::weather {

namespace {

QString n0(double v) { return QString::number(static_cast<qint64>(py::round(v))); }

QJsonValue jv(const Opt& v) { return v ? QJsonValue(*v) : QJsonValue(); }
QJsonValue jv(const std::optional<int>& v) { return v ? QJsonValue(*v) : QJsonValue(); }

QList<Opt> floats(const QJsonValue& a, qsizetype n) {
    QList<Opt> out;
    const QJsonArray arr = a.isArray() ? a.toArray() : QJsonArray();
    for (qsizetype i = 0; i < arr.size() && i < MAX_HOURS; ++i) {
        const QJsonValue v = arr.at(i);
        out.append(v.isNull() ? Opt() : py::toFloat(v));
    }
    while (out.size() < n) out.append(Opt());
    while (out.size() > n) out.removeLast();
    return out;
}

QList<std::optional<int>> ints(const QJsonValue& a, qsizetype n) {
    QList<std::optional<int>> out;
    const QJsonArray arr = a.isArray() ? a.toArray() : QJsonArray();
    for (qsizetype i = 0; i < arr.size() && i < MAX_HOURS; ++i) {
        const QJsonValue v = arr.at(i);
        const auto x = v.isNull() ? std::optional<qint64>() : py::toInt(v);
        out.append(x ? std::optional<int>(static_cast<int>(*x)) : std::nullopt);
    }
    while (out.size() < n) out.append(std::nullopt);
    while (out.size() > n) out.removeLast();
    return out;
}

template <typename T>
QJsonArray column(const QList<T>& v) {
    QJsonArray a;
    for (const auto& x : v) a.append(jv(x));
    return a;
}

}  // namespace

QString forecastUrl(double lat, double lon) {
    return QStringLiteral("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2"
                          "&hourly=temperature_2m,precipitation_probability,precipitation,weather_code,wind_speed_10m"
                          "&timeformat=unixtime&timezone=UTC&forecast_days=%3&past_days=%4")
        .arg(QString::asprintf("%.4f", lat), QString::asprintf("%.4f", lon))
        .arg(FORECAST_DAYS)
        .arg(PAST_DAYS);
}

QJsonObject Forecast::toJson() const {
    QJsonArray t;
    for (qint64 x : times) t.append(x);
    QJsonObject o{{QStringLiteral("times"), t},         {QStringLiteral("temp"), column(temp)},
                  {QStringLiteral("rain_prob"), column(rainProb)}, {QStringLiteral("rain"), column(rain)},
                  {QStringLiteral("code"), column(code)}, {QStringLiteral("wind"), column(wind)}};
    if (!round.isEmpty()) o.insert(QStringLiteral("round"), round);
    return o;
}

std::optional<Forecast> Forecast::fromJson(const QJsonObject& o) {
    Forecast f;
    for (const QJsonValue& v : o.value(QStringLiteral("times")).toArray()) {
        const auto t = py::toInt(v);
        if (!t) return std::nullopt;
        f.times.append(*t);
    }
    const qsizetype n = f.times.size();
    f.temp = floats(o.value(QStringLiteral("temp")), n);
    f.rainProb = floats(o.value(QStringLiteral("rain_prob")), n);
    f.rain = floats(o.value(QStringLiteral("rain")), n);
    f.code = ints(o.value(QStringLiteral("code")), n);
    f.wind = floats(o.value(QStringLiteral("wind")), n);
    const QJsonValue r = o.value(QStringLiteral("round"));
    if (r.isString()) f.round = r.toString();
    return f;
}

Hour Forecast::hourAt(qsizetype i) const {
    Hour h;
    h.ts = times.value(i);
    h.temp = temp.value(i);
    h.rainProb = rainProb.value(i);
    h.rain = rain.value(i);
    h.code = code.value(i);
    h.wind = wind.value(i);
    return h;
}

std::optional<Forecast> parseOpenMeteo(const QJsonValue& data) {
    const QJsonValue hv = data.isObject() ? data.toObject().value(QStringLiteral("hourly")) : QJsonValue();
    if (!hv.isObject()) return std::nullopt;
    const QJsonObject hourly = hv.toObject();
    Forecast f;
    const QJsonArray t = hourly.value(QStringLiteral("time")).toArray();
    for (qsizetype i = 0; i < t.size() && i < MAX_HOURS; ++i) {
        const auto x = t.at(i).isNull() ? std::optional<qint64>() : py::toInt(t.at(i));
        if (!x) return std::nullopt;
        f.times.append(*x);
    }
    if (f.times.isEmpty()) return std::nullopt;
    const qsizetype n = f.times.size();
    f.temp = floats(hourly.value(QStringLiteral("temperature_2m")), n);
    f.rainProb = floats(hourly.value(QStringLiteral("precipitation_probability")), n);
    f.rain = floats(hourly.value(QStringLiteral("precipitation")), n);
    f.code = ints(hourly.value(QStringLiteral("weather_code")), n);
    f.wind = floats(hourly.value(QStringLiteral("wind_speed_10m")), n);
    return f;
}

std::optional<Hour> forecastAt(const Forecast* fc, qint64 ts) {
    if (!fc || fc->times.isEmpty()) return std::nullopt;
    qsizetype best = 0;
    for (qsizetype k = 1; k < fc->times.size(); ++k)
        if (std::llabs(fc->times[k] - ts) < std::llabs(fc->times[best] - ts)) best = k;
    if (std::llabs(fc->times[best] - ts) > 90 * 60) return std::nullopt;
    Hour h = fc->hourAt(best);
    if (!h.temp) return std::nullopt;
    return h;
}

std::optional<Window> forecastWindow(const Forecast* fc, qint64 start, qint64 end) {
    QList<qsizetype> idx;
    if (fc)
        for (qsizetype i = 0; i < fc->times.size(); ++i)
            if (start - 1800 <= fc->times[i] && fc->times[i] < end) idx.append(i);
    if (idx.isEmpty()) {
        const auto hour = forecastAt(fc, start);
        if (!hour) return std::nullopt;
        Window w;
        static_cast<Hour&>(w) = *hour;
        Hour first = *hour;
        first.ts = start;
        w.hours = {first};
        w.tempMin = w.tempMax = *hour->temp;
        w.rainTotal = hour->rain.value_or(0.0);
        if (isWet(*hour)) w.wetFrom = start;
        return w;
    }
    QList<Hour> hours;
    for (qsizetype i : idx) {
        Hour h = fc->hourAt(i);
        if (h.temp) hours.append(h);
    }
    if (hours.isEmpty()) return std::nullopt;
    Window w;
    w.ts = hours.first().ts;
    w.temp = hours.first().temp;
    w.tempMin = w.tempMax = *hours.first().temp;
    double rp = 0, rain = 0, total = 0, wind = 0;
    const Hour* worst = &hours.first();
    for (const Hour& h : hours) {
        w.tempMin = std::min(w.tempMin, *h.temp);
        w.tempMax = std::max(w.tempMax, *h.temp);
        rp = std::max(rp, h.rainProb.value_or(0));
        rain = std::max(rain, h.rain.value_or(0));
        total += h.rain.value_or(0);
        wind = std::max(wind, h.wind.value_or(0));
        const auto key = [](const Hour& x) { return std::make_pair(severity(x.code), x.rainProb.value_or(0)); };
        if (key(h) > key(*worst)) worst = &h;
        if (!w.wetFrom && isWet(h)) w.wetFrom = h.ts;
    }
    w.rainProb = rp;
    w.rain = rain;
    w.rainTotal = total;
    w.code = worst->code;
    w.wind = wind;
    w.hours = hours;
    return w;
}

int severity(std::optional<int> code) {
    const int c = code.value_or(0);
    switch (c) {
    case 95: case 96: case 99: return 5;
    case 65: case 67: case 82: return 4;
    case 61: case 63: case 66: case 80: case 81: return 3;
    case 51: case 53: case 55: case 56: case 57: case 71: case 73: case 75: case 77: case 85: case 86: return 2;
    case 3: case 45: case 48: return 1;
    default: return 0;
    }
}

std::pair<QString, QString> outlook(const std::optional<Window>& w) {
    if (!w) return {};
    const double p = w->rainProb.value_or(0);
    if (isWet(*w) || w->rainTotal >= 1.0) return {T(QStringLiteral("Wet: rain expected")), QStringLiteral("wet")};
    if (p >= 25 || severity(w->code) >= 2)
        return {T(QStringLiteral("Rain possible ({chance}%)"), {{"chance", n0(p)}}), QStringLiteral("risk")};
    return {T(QStringLiteral("Dry: rain unlikely ({chance}%)"), {{"chance", n0(p)}}), QStringLiteral("dry")};
}

std::array<int, 3> timings(const QString& speed, const QJsonArray& custom) {
    if (speed == u"custom" && !custom.isEmpty() && custom.size() >= 3) {
        std::array<int, 3> out{};
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            const auto x = py::toInt(custom.at(i));
            if (!x) { ok = false; break; }
            out[i] = static_cast<int>(std::max<qint64>(5, *x));
        }
        if (ok) return out;
    }
    if (speed == u"fast") return {5, 15, 60};
    if (speed == u"slow") return {30, 120, 360};
    return {10, 30, 240};
}

QString fmtMinutes(int m) {
    if (m >= 60 && m % 60 == 0) return T(QStringLiteral("{hours} h"), {{"hours", QString::number(m / 60)}});
    return T(QStringLiteral("{minutes} min"), {{"minutes", QString::number(m)}});
}

std::optional<int> refreshSeconds(const Sessions& sessions, qint64 now, const QMap<QString, int>& durations,
                                  const QString& speed, const QJsonArray& custom) {
    if (speed == u"off") return std::nullopt;
    const auto [near, weekend, other] = timings(speed, custom);
    for (const auto& s : sessions) {
        const qint64 end = s.ts + durations.value(s.id, 60) * 60LL;
        if (s.ts - 3 * 3600 <= now && now <= end) return near * 60;   // just before and during a session
    }
    if (!sessions.isEmpty() && sessions.first().ts - 24 * 3600 <= now && now <= sessions.last().ts + 3 * 3600)
        return weekend * 60;                                          // race weekend
    return other * 60;
}

QString refreshText(const QString& speed, const QJsonArray& custom) {
    if (speed == u"off") return T(QStringLiteral("Forecast updates are off (Settings › Formula 1)."));
    const auto [near, weekend, other] = timings(speed, custom);
    return T(QStringLiteral("Updated every {near} around sessions, {weekend} during the weekend, {other} otherwise."),
             {{"near", fmtMinutes(near)}, {"weekend", fmtMinutes(weekend)}, {"other", fmtMinutes(other)}});
}

QString condition(std::optional<int> code) {
    if (!code) return {};
    switch (*code) {
    case 0: return T(QStringLiteral("Clear"));
    case 1: case 2: return T(QStringLiteral("Partly cloudy"));
    case 3: return T(QStringLiteral("Overcast"));
    case 45: case 48: return T(QStringLiteral("Fog"));
    case 51: case 53: case 55: case 56: case 57: return T(QStringLiteral("Drizzle"));
    case 61: case 63: case 66: case 80: case 81: return T(QStringLiteral("Rain"));
    case 65: case 67: case 82: return T(QStringLiteral("Heavy rain"));
    case 71: case 73: case 75: case 77: case 85: case 86: return T(QStringLiteral("Snow"));
    case 95: case 96: case 99: return T(QStringLiteral("Thunderstorm"));
    default: return {};
    }
}

bool isWet(const Hour& h) {
    if (h.rainProb.value_or(0) >= 50 || h.rain.value_or(0) >= 0.5) return true;
    switch (h.code.value_or(0)) {
    case 61: case 63: case 65: case 66: case 67: case 80: case 81: case 82: case 95: case 96: case 99: return true;
    default: return false;
    }
}

bool isWet(const std::optional<Hour>& h) { return h && isWet(*h); }

QString temp(Opt c, const QString& units, bool unit) {
    if (!c) return {};
    const bool imp = units == u"imperial";
    const double v = imp ? *c * 9 / 5 + 32 : *c;
    return n0(v) + QChar(0x00B0) + (unit ? (imp ? QStringLiteral("F") : QStringLiteral("C")) : QString());
}

QString tempRange(double lo, double hi, const QString& units, bool unit) {
    const QString a = temp(lo, units), b = temp(hi, units);
    const QString suffix = unit ? (units == u"imperial" ? QStringLiteral("F") : QStringLiteral("C")) : QString();
    return (a != b ? a.chopped(1) + QChar(0x2013) + b : b) + suffix;
}

QString wind(Opt kmh, const QString& units) {
    if (!kmh) return {};
    if (units == u"imperial") return T(QStringLiteral("{speed} mph"), {{"speed", n0(*kmh / 1.609)}});
    return T(QStringLiteral("{speed} km/h"), {{"speed", n0(*kmh)}});
}

QString shortText(const Hour& h, const QString& units) {
    QStringList bits{temp(h.temp, units)};
    if (h.rainProb) bits << n0(*h.rainProb) + u'%';
    return bits.join(QStringLiteral(" · "));
}

QJsonObject LiveWeather::toJson() const {
    return {{QStringLiteral("air"), jv(air)},   {QStringLiteral("track"), jv(track)},
            {QStringLiteral("humidity"), jv(humidity)}, {QStringLiteral("rain"), rain},
            {QStringLiteral("wind"), jv(wind)}, {QStringLiteral("wind_dir"), jv(windDir)}};
}

LiveWeather LiveWeather::fromJson(const QJsonObject& o) {
    auto f = [&](const char* k) {
        const QJsonValue v = o.value(QLatin1StringView(k));
        return v.isNull() ? Opt() : py::toFloat(v);
    };
    LiveWeather w;
    w.air = f("air");
    w.track = f("track");
    w.humidity = f("humidity");
    w.wind = f("wind");
    w.windDir = f("wind_dir");
    w.rain = py::truthy(o.value(QStringLiteral("rain")));
    return w;
}

std::optional<LiveWeather> parseLiveWeather(const QJsonValue& dv) {
    if (!dv.isObject()) return std::nullopt;
    const QJsonObject d = dv.toObject();
    auto f = [&](const char* k) {
        const QJsonValue v = d.value(QLatin1StringView(k));
        return (v.isNull() || v.isUndefined()) ? Opt() : py::toFloat(v);
    };
    LiveWeather w;
    w.air = f("AirTemp");
    w.track = f("TrackTemp");
    w.humidity = f("Humidity");
    w.wind = f("WindSpeed");
    w.windDir = f("WindDirection");
    const QJsonValue rv = d.value(QStringLiteral("Rainfall"));
    const QString r = (rv.isUndefined() ? QStringLiteral("0") : py::str(rv)).trimmed();
    w.rain = !(r == u"0" || r.isEmpty() || r == u"False" || r == u"false");
    if (!w.air && !w.track) return std::nullopt;
    return w;
}

}  // namespace f1::weather
