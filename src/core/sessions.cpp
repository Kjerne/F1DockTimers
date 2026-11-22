#include "sessions.h"

#include "i18n.h"
#include "pyjson.h"
#include "timeutil.h"

#include <QJsonArray>
#include <algorithm>

namespace f1 {

const QStringList& sessionIds() {
    static const QStringList ids = {QStringLiteral("fp1"),    QStringLiteral("fp2"),   QStringLiteral("fp3"),
                                    QStringLiteral("sprint_quali"), QStringLiteral("sprint"),
                                    QStringLiteral("quali"),  QStringLiteral("race")};
    return ids;
}

QString sessionName(const QString& id) {
    if (id == u"fp1") return T(QStringLiteral("Practice 1"));
    if (id == u"fp2") return T(QStringLiteral("Practice 2"));
    if (id == u"fp3") return T(QStringLiteral("Practice 3"));
    if (id == u"sprint_quali") return T(QStringLiteral("Sprint Quali"));
    if (id == u"sprint") return T(QStringLiteral("Sprint"));
    if (id == u"quali") return T(QStringLiteral("Qualifying"));
    if (id == u"race") return T(QStringLiteral("Race"));
    return id;
}

const QMap<QString, int>& durationTable() {
    static const QMap<QString, int> t = {{QStringLiteral("fp1"), 60},          {QStringLiteral("fp2"), 60},
                                         {QStringLiteral("fp3"), 60},          {QStringLiteral("sprint_quali"), 45},
                                         {QStringLiteral("sprint"), 60},       {QStringLiteral("quali"), 60},
                                         {QStringLiteral("race"), 120}};
    return t;
}

int durationMin(const QString& id) { return durationTable().value(id, 60); }

QJsonObject Session::toJson() const {
    return {{QStringLiteral("id"), id}, {QStringLiteral("ts"), ts}, {QStringLiteral("delay"), delay},
            {QStringLiteral("source"), source}};
}

Session Session::fromJson(const QJsonObject& d) {
    Session s;
    s.id = py::str(d.value(QStringLiteral("id")));
    s.ts = py::toInt(d.value(QStringLiteral("ts"))).value_or(0);
    s.delay = py::toInt(d.value(QStringLiteral("delay"))).value_or(0);
    const QJsonValue src = d.value(QStringLiteral("source"));
    s.source = src.isUndefined() ? QStringLiteral("published") : py::str(src);
    return s;
}

static QJsonValue optNum(const std::optional<double>& v) { return v ? QJsonValue(*v) : QJsonValue(); }

QJsonObject Race::toJson() const {
    QJsonArray raw;
    for (const auto& s : rawSessions) raw.append(s.toJson());
    return {{QStringLiteral("season"), season},       {QStringLiteral("round"), round},
            {QStringLiteral("name"), name},           {QStringLiteral("circuit_id"), circuitId},
            {QStringLiteral("circuit_name"), circuitName}, {QStringLiteral("locality"), locality},
            {QStringLiteral("country"), country},     {QStringLiteral("url"), url},
            {QStringLiteral("lat"), optNum(lat)},      {QStringLiteral("lon"), optNum(lon)},
            {QStringLiteral("raw_sessions"), raw}};
}

Race Race::fromJson(const QJsonObject& d) {
    auto s = [&](const char* k) {
        const QJsonValue v = d.value(QLatin1StringView(k));
        return v.isUndefined() ? QString() : py::str(v);
    };
    Race r;
    r.season = s("season");
    r.round = s("round");
    r.name = s("name");
    r.circuitId = s("circuit_id");
    r.circuitName = s("circuit_name");
    r.locality = s("locality");
    r.country = s("country");
    r.url = s("url");
    r.lat = coord(d.value(QStringLiteral("lat")), 90);
    r.lon = coord(d.value(QStringLiteral("lon")), 180);
    for (const QJsonValue& v : d.value(QStringLiteral("raw_sessions")).toArray())
        r.rawSessions.append(Session::fromJson(v.toObject()));
    for (const auto& x : r.rawSessions) r.sessions.append(Session{x.id, x.ts, 0, QStringLiteral("published")});
    return r;
}

bool Race::isSprintWeekend() const {
    return std::any_of(rawSessions.begin(), rawSessions.end(),
                       [](const Session& s) { return s.id == u"sprint" || s.id == u"sprint_quali"; });
}

QString Race::location() const {
    if (!locality.isEmpty() && !country.isEmpty()) return locality + QStringLiteral(", ") + country;
    return locality.isEmpty() ? country : locality;
}

std::optional<double> coord(const QJsonValue& v, double limit) {
    const auto f = py::toFloat(v);
    if (!f || !(-limit <= *f && *f <= limit)) return std::nullopt;
    return f;
}

Sessions buildSessions(const QJsonValue& raceValue) {
    if (!raceValue.isObject()) return {};
    const QJsonObject race = raceValue.toObject();
    const QJsonObject raceSession{{QStringLiteral("date"), race.value(QStringLiteral("date"))},
                                  {QStringLiteral("time"), race.value(QStringLiteral("time"))}};
    const std::pair<const char*, QJsonValue> raw[] = {
        {"fp1", race.value(QStringLiteral("FirstPractice"))},
        {"fp2", race.value(QStringLiteral("SecondPractice"))},
        {"fp3", race.value(QStringLiteral("ThirdPractice"))},
        {"sprint_quali", py::orv(race.value(QStringLiteral("SprintQualifying")), race.value(QStringLiteral("SprintShootout")))},
        {"sprint", race.value(QStringLiteral("Sprint"))},
        {"quali", race.value(QStringLiteral("Qualifying"))},
        {"race", raceSession},
    };
    Sessions out;
    for (const auto& [sid, s] : raw) {
        if (!s.isObject()) continue;
        const QJsonObject o = s.toObject();
        if (auto ts = parseUtc(o.value(QStringLiteral("date")), o.value(QStringLiteral("time"))))
            out.append(Session{QString::fromLatin1(sid), *ts});
    }
    std::stable_sort(out.begin(), out.end(), [](const Session& a, const Session& b) { return a.ts < b.ts; });
    return out;
}

Sessions effectiveSessions(const Sessions& raw, const QMap<QString, qint64>& autoTs, const QJsonObject& manualMin) {
    Sessions out;
    for (const auto& base : raw) {
        QString source = QStringLiteral("published");
        qint64 ts = base.ts;
        if (autoTs.contains(base.id)) {
            ts = autoTs.value(base.id);
            source = QStringLiteral("feed");
        }
        const QJsonValue mv = manualMin.value(base.id);
        const qint64 minutes = py::truthy(mv) ? py::toInt(mv).value_or(0) : 0;
        if (minutes) {
            ts = base.ts + minutes * 60;
            source = QStringLiteral("manual");
        }
        out.append(Session{base.id, ts, py::floordiv(ts - base.ts, 60), source});
    }
    std::stable_sort(out.begin(), out.end(), [](const Session& a, const Session& b) { return a.ts < b.ts; });
    return out;
}

std::optional<qint64> sessionTs(const Sessions& ss, const QString& id) {
    for (const auto& s : ss)
        if (s.id == id) return s.ts;
    return std::nullopt;
}

std::optional<Session> nextSession(const Sessions& ss, qint64 now) {
    for (const auto& s : ss)
        if (s.ts > now) return s;
    return std::nullopt;
}

std::optional<Session> currentSession(const Sessions& ss, qint64 now) {
    for (const auto& s : ss)
        if (s.ts <= now && now < s.ts + durationMin(s.id) * 60LL) return s;
    return std::nullopt;
}

bool nearSession(const Sessions& ss, qint64 now) {
    return std::any_of(ss.begin(), ss.end(),
                       [now](const Session& s) { return s.ts - LIVE_BEFORE <= now && now <= s.ts + LIVE_AFTER; });
}

std::optional<qint64> windowEnd(const Sessions& ss, qint64 now) {
    std::optional<qint64> best;
    for (const auto& s : ss)
        if (s.ts - LIVE_BEFORE <= now && now <= s.ts + LIVE_AFTER) best = std::max(best.value_or(s.ts + LIVE_AFTER), s.ts + LIVE_AFTER);
    return best;
}

bool gridApplicable(const Sessions& ss, qint64 now) {
    const auto q = sessionTs(ss, QStringLiteral("quali"));
    const auto r = sessionTs(ss, QStringLiteral("race"));
    if (!q || !r) return false;
    return *q + 3600 <= now && now < *r;
}

std::optional<std::pair<qint64, qint64>> weekendWindow(const Sessions& ss) {
    if (ss.isEmpty()) return std::nullopt;
    qint64 lo = ss.first().ts, hi = ss.first().ts;
    for (const auto& s : ss) {
        lo = std::min(lo, s.ts);
        hi = std::max(hi, s.ts);
    }
    return std::make_pair(lo, hi);
}

bool liveActive(const QJsonObject& live, const Sessions& ss, qint64 now, bool debug) {
    if (!py::truthy(live.value(QStringLiteral("rows")))) return false;
    if (debug) return true;
    if (!nearSession(ss, now)) return false;
    const QJsonValue lv = live.value(QStringLiteral("latest_sec"));
    const double latest = py::truthy(lv) ? py::toFloat(lv).value_or(0) : 0;
    return latest > 0 && (now - latest) < 15 * 60;
}

}  // namespace f1
