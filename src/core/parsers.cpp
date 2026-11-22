#include "parsers.h"

#include "pyjson.h"
#include "sanitize.h"
#include "timeutil.h"

#include <QHash>
#include <algorithm>
#include <cmath>

namespace f1::parsers {

using namespace f1::py;

namespace {

const QString kMR = QStringLiteral("MRData");

double num(const QJsonValue& v, double def = 0.0) { return toFloat(v).value_or(def); }

QString fmtPts(const QJsonValue& v) {
    const auto n = toFloat(v);
    if (!n) return safe(v, 6);
    if (std::isfinite(*n) && *n == std::trunc(*n)) return QString::number(static_cast<qint64>(*n));
    return fmtG(*n);
}

QString link(const QJsonValue& o) {
    if (!o.isObject()) return {};
    return safeLink(o.toObject().value(QStringLiteral("url"))).value_or(QString());
}

QJsonValue get(const QJsonObject& o, const char* k) { return o.value(QLatin1StringView(k)); }

// dict.get(k, default) on JSON: the raw value, or `def` when missing.
QJsonValue getOr(const QJsonObject& o, const char* k, const QJsonValue& def) {
    const QJsonValue v = o.value(QLatin1StringView(k));
    return v.isUndefined() ? def : v;
}

std::optional<QString> driverKey(const QJsonValue& n) {
    const QString k = (n.isNull() || n.isUndefined()) ? QString() : str(n);
    if (isDigits(k) && k.size() <= 3) return k;
    return std::nullopt;
}

QString lapText(double s) {
    if (!(s > 0)) return {};
    const double rem = std::fmod(s, 3600.0);
    const double h = std::round((s - rem) / 3600.0);
    const double sec = std::fmod(rem, 60.0);
    const double m = std::round((rem - sec) / 60.0);
    if (h >= 1) return QString::asprintf("%d:%02d:%06.3f", static_cast<int>(h), static_cast<int>(m), sec);
    if (m >= 1) return QString::asprintf("%d:%06.3f", static_cast<int>(m), sec);
    return QString::asprintf("%.3f", sec);
}

QString fmtGap(const QJsonValue& v) {
    if (v.isNull() || v.isUndefined() || (v.isString() && v.toString().isEmpty())) return {};
    if (v.isDouble()) return QStringLiteral("+") + (v.toDouble() > 0 ? lapText(v.toDouble()) : QStringLiteral("0.000"));
    return safe(v, 12);
}

QString gapText(const QJsonValue& g) {
    if (g.isNull() || g.isUndefined() || (g.isString() && g.toString().isEmpty())) return {};
    if (g.isDouble()) return g.toDouble() <= 0 ? QString() : QStringLiteral("+") + lapText(g.toDouble());
    const QString text = safe(g, 12);
    if (text.toUpper().contains(QStringLiteral("LAP"))) {
        QString n;
        for (QChar c : text)
            if (c.isDigit()) n += c;
        if (n.isEmpty()) n = QStringLiteral("1");
        return QStringLiteral("+%1 Lap%2").arg(n, n != u"1" ? QStringLiteral("s") : QString());
    }
    return text;
}

bool isIntValue(const QJsonValue& v) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() == std::trunc(v.toDouble());
}

QJsonObject firstList(const QJsonValue& data, QJsonArray* lists) {
    *lists = bounded(path(data, {kMR, QStringLiteral("StandingsTable"), QStringLiteral("StandingsLists")}), 4);
    return lists->isEmpty() ? QJsonObject() : obj(lists->first());
}

QJsonObject firstRace(const QJsonValue& data, bool* isDict) {
    const QJsonArray races = bounded(path(data, {kMR, QStringLiteral("RaceTable"), QStringLiteral("Races")}), 4);
    const QJsonValue r0 = races.isEmpty() ? QJsonValue(QJsonObject()) : races.first();
    *isDict = r0.isObject();
    return obj(r0);
}

}  // namespace

QString driverCode(const QJsonValue& dv) {
    if (!dv.isObject()) return QStringLiteral("?");
    const QJsonObject d = dv.toObject();
    if (truthy(d.value(QStringLiteral("code")))) return safe(d.value(QStringLiteral("code")), 4);
    const QJsonValue fam = d.value(QStringLiteral("familyName"));
    const QString f = truthy(fam) ? str(fam) : QStringLiteral("?");
    return safe(f.left(3).toUpper(), 4);
}

QString driverName(const QJsonValue& dv) {
    if (!dv.isObject()) return {};
    const QJsonObject d = dv.toObject();
    auto part = [&](const char* k) {
        const QJsonValue v = d.value(QLatin1StringView(k));
        return v.isUndefined() ? QString() : str(v);
    };
    return safe((part("givenName") + u' ' + part("familyName")).trimmed(), 40);
}

// ── Jolpica ──────────────────────────────────────────────────────────────────

std::optional<Race> parseRace(const QJsonValue& rv) {
    if (!rv.isObject()) return std::nullopt;
    const QJsonObject r = rv.toObject();
    const QJsonObject circuit = obj(r.value(QStringLiteral("Circuit")));
    const QJsonObject loc = obj(circuit.value(QStringLiteral("Location")));
    Race race;
    race.season = safe(get(r, "season"), 8);
    race.round = safe(get(r, "round"), 4);
    race.name = safe(get(r, "raceName"), 48);
    race.circuitId = safe(get(circuit, "circuitId"), 40);
    race.circuitName = safe(get(circuit, "circuitName"), 48);
    race.locality = safe(get(loc, "locality"), 32);
    race.country = safe(get(loc, "country"), 32);
    race.url = link(r);
    race.lat = coord(get(loc, "lat"), 90);
    race.lon = coord(get(loc, "long"), 180);
    race.rawSessions = buildSessions(r);
    race.sessions = race.rawSessions;
    if (race.rawSessions.isEmpty()) return std::nullopt;
    return race;
}

std::optional<Race> parseNextRace(const QJsonValue& data) {
    const QJsonValue races = path(data, {kMR, QStringLiteral("RaceTable"), QStringLiteral("Races")});
    if (!races.isArray() || races.toArray().isEmpty()) return std::nullopt;
    return parseRace(races.toArray().first());
}

std::optional<Race> firstFutureRace(const QJsonValue& data, qint64 now) {
    const QJsonValue races = path(data, {kMR, QStringLiteral("RaceTable"), QStringLiteral("Races")});
    for (const QJsonValue& r : bounded(races, 40)) {
        auto race = parseRace(r);
        if (!race) continue;
        const auto ts = sessionTs(race->rawSessions, QStringLiteral("race"));
        if (ts && *ts + 3 * 3600 > now) return race;
    }
    return std::nullopt;
}

QJsonObject parseDriverStandings(const QJsonValue& data) {
    QJsonArray lists;
    const QJsonObject l0 = firstList(data, &lists);
    const QJsonArray entries = bounded(lists.isEmpty() ? QJsonValue() : l0.value(QStringLiteral("DriverStandings")), 60);
    const double leader = entries.isEmpty() ? 0 : num(obj(entries.first()).value(QStringLiteral("points")));
    QJsonArray rows;
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const QJsonObject e = obj(entries.at(i));
        const QJsonArray cons = arr(e.value(QStringLiteral("Constructors")));
        const QJsonObject con = cons.isEmpty() ? QJsonObject() : obj(cons.last());
        const double pts = num(get(e, "points"));
        rows.append(QJsonObject{
            {QStringLiteral("pos"), safe(orv(get(e, "positionText"), get(e, "position")), 3)},
            {QStringLiteral("code"), driverCode(get(e, "Driver"))},
            {QStringLiteral("name"), driverName(get(e, "Driver"))},
            {QStringLiteral("team"), safe(get(con, "name"), 32)},
            {QStringLiteral("team_id"), safe(get(con, "constructorId"), 32)},
            {QStringLiteral("pts"), fmtPts(get(e, "points"))},
            {QStringLiteral("wins"), static_cast<qint64>(num(get(e, "wins")))},
            {QStringLiteral("gap"), i == 0 ? 0 : static_cast<qint64>(leader - pts)},
            {QStringLiteral("url"), link(get(e, "Driver"))},
        });
        if (rows.size() == 30) break;
    }
    return {{QStringLiteral("rows"), rows},
            {QStringLiteral("season"), lists.isEmpty() ? QString() : safe(get(l0, "season"), 8)},
            {QStringLiteral("round"), lists.isEmpty() ? QString() : safe(get(l0, "round"), 4)}};
}

QJsonObject parseConstructorStandings(const QJsonValue& data) {
    QJsonArray lists;
    const QJsonObject l0 = firstList(data, &lists);
    const QJsonArray entries =
        bounded(lists.isEmpty() ? QJsonValue() : l0.value(QStringLiteral("ConstructorStandings")), 60);
    const double leader = entries.isEmpty() ? 0 : num(obj(entries.first()).value(QStringLiteral("points")));
    QJsonArray rows;
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const QJsonObject e = obj(entries.at(i));
        const QJsonObject c = obj(get(e, "Constructor"));
        const double pts = num(get(e, "points"));
        rows.append(QJsonObject{
            {QStringLiteral("pos"), safe(orv(get(e, "positionText"), get(e, "position")), 3)},
            {QStringLiteral("name"), safe(get(c, "name"), 32)},
            {QStringLiteral("team_id"), safe(get(c, "constructorId"), 32)},
            {QStringLiteral("pts"), fmtPts(get(e, "points"))},
            {QStringLiteral("wins"), static_cast<qint64>(num(get(e, "wins")))},
            {QStringLiteral("gap"), i == 0 ? 0 : static_cast<qint64>(leader - pts)},
            {QStringLiteral("url"), link(c)},
        });
        if (rows.size() == 15) break;
    }
    return {{QStringLiteral("rows"), rows},
            {QStringLiteral("season"), lists.isEmpty() ? QString() : safe(get(l0, "season"), 8)},
            {QStringLiteral("round"), lists.isEmpty() ? QString() : safe(get(l0, "round"), 4)}};
}

QJsonObject parseResults(const QJsonValue& data, const char* entriesKey) {
    bool isDict = false;
    const QJsonObject r0 = firstRace(data, &isDict);
    const QJsonArray entries = bounded(isDict ? r0.value(QLatin1StringView(entriesKey)) : QJsonValue(), 60);
    QJsonArray rows;
    QJsonValue fastest;   // null
    const qint64 leadLaps = entries.isEmpty() ? 0 : toInt(obj(entries.first()).value(QStringLiteral("laps"))).value_or(0);
    for (const QJsonValue& ev : entries) {
        const QJsonObject e = obj(ev);
        const QJsonObject con = obj(get(e, "Constructor"));
        QJsonValue timed = path(e, {QStringLiteral("Time"), QStringLiteral("time")});
        const QString status = safe(get(e, "status"), 24);
        const auto laps = toInt(get(e, "laps"));
        const qint64 down = laps ? leadLaps - *laps : 0;
        if (status == u"Lapped" || (status.startsWith(u'+') && status.contains(QStringLiteral("Lap")))) {
            // Jolpica's Time for a lapped car is measured within its own lap, so "+4.427" would read
            // as on the lead lap. Show the laps down instead.
            if (down > 0)
                timed = QStringLiteral("+%1 Lap%2").arg(down).arg(down != 1 ? QStringLiteral("s") : QString());
            else
                timed = truthy(QJsonValue(status)) ? QJsonValue(status) : timed;
        }
        rows.append(QJsonObject{
            {QStringLiteral("pos"), safe(get(e, "positionText"), 3)},
            {QStringLiteral("code"), driverCode(get(e, "Driver"))},
            {QStringLiteral("name"), driverName(get(e, "Driver"))},
            {QStringLiteral("team"), safe(get(con, "name"), 32)},
            {QStringLiteral("team_id"), safe(get(con, "constructorId"), 32)},
            {QStringLiteral("detail"), safe(orv(timed, status), 24)},
            {QStringLiteral("status"), status},
            {QStringLiteral("pts"), fmtPts(get(e, "points"))},
            {QStringLiteral("grid"), safe(get(e, "grid"), 3)},
            {QStringLiteral("url"), link(get(e, "Driver"))},
        });
        const QJsonValue fl = get(e, "FastestLap");
        if (fl.isObject() && str(fl.toObject().value(QStringLiteral("rank"))) == u"1") {
            fastest = QJsonObject{
                {QStringLiteral("code"), driverCode(get(e, "Driver"))},
                {QStringLiteral("time"), safe(path(fl, {QStringLiteral("Time"), QStringLiteral("time")}), 12)},
                {QStringLiteral("lap"), safe(fl.toObject().value(QStringLiteral("lap")), 4)},
            };
        }
    }
    while (rows.size() > 30) rows.removeLast();
    const auto dateTs = isDict ? parseUtc(get(r0, "date"), get(r0, "time")) : std::nullopt;
    return {
        {QStringLiteral("race_name"), isDict ? safe(get(r0, "raceName"), 48) : QString()},
        {QStringLiteral("season"), isDict ? safe(get(r0, "season"), 8) : QString()},
        {QStringLiteral("round"), isDict ? safe(get(r0, "round"), 4) : QString()},
        {QStringLiteral("date_ts"), dateTs.value_or(0)},
        {QStringLiteral("circuit"), safe(path(r0, {QStringLiteral("Circuit"), QStringLiteral("circuitName")}), 48)},
        {QStringLiteral("fastest"), fastest},
        {QStringLiteral("rows"), rows},
    };
}

QJsonObject parseQualifying(const QJsonValue& data) {
    bool isDict = false;
    const QJsonObject r0 = firstRace(data, &isDict);
    const QJsonArray entries = bounded(isDict ? r0.value(QStringLiteral("QualifyingResults")) : QJsonValue(), 60);
    QJsonArray rows;
    for (const QJsonValue& qv : entries) {
        const QJsonObject q = obj(qv);
        const QJsonObject con = obj(get(q, "Constructor"));
        const QJsonValue best = orv(orv(orv(get(q, "Q3"), get(q, "Q2")), get(q, "Q1")), QString());
        rows.append(QJsonObject{
            {QStringLiteral("pos"), safe(get(q, "position"), 3)},
            {QStringLiteral("code"), driverCode(get(q, "Driver"))},
            {QStringLiteral("name"), driverName(get(q, "Driver"))},
            {QStringLiteral("team"), safe(get(con, "name"), 32)},
            {QStringLiteral("team_id"), safe(get(con, "constructorId"), 32)},
            {QStringLiteral("detail"), safe(best, 16)},
            {QStringLiteral("url"), link(get(q, "Driver"))},
        });
        if (rows.size() == 30) break;
    }
    return {{QStringLiteral("round"), isDict ? safe(get(r0, "round"), 4) : QString()},
            {QStringLiteral("season"), isDict ? safe(get(r0, "season"), 8) : QString()},
            {QStringLiteral("rows"), rows}};
}

QJsonArray gridFromResults(const QJsonObject& results) {
    QList<QJsonObject> rows;
    for (const QJsonValue& rv : results.value(QStringLiteral("rows")).toArray()) {
        QJsonObject r = obj(rv);
        const QJsonValue gv = getOr(r, "grid", QString());
        const QString g = str(gv);
        if (!isDigits(g)) continue;
        r.insert(QStringLiteral("pos"), g != u"0" ? gv : QJsonValue(QStringLiteral("PL")));
        r.insert(QStringLiteral("detail"), g == u"0" ? QStringLiteral("Pit lane") : QString());
        rows.append(r);
    }
    auto key = [](const QJsonObject& r) {
        const QString p = str(r.value(QStringLiteral("pos")));
        return std::make_pair(p == u"PL", isDigits(p) ? p.toLongLong() : 99LL);
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const QJsonObject& a, const QJsonObject& b) { return key(a) < key(b); });
    QJsonArray out;
    for (const auto& r : rows) out.append(r);
    return out;
}

// ── F1 timing feed ───────────────────────────────────────────────────────────

QString feedSessionId(const QString& name) {
    static const QHash<QString, QString> feedSessions = {
        {QStringLiteral("Practice 1"), QStringLiteral("fp1")},
        {QStringLiteral("Practice 2"), QStringLiteral("fp2")},
        {QStringLiteral("Practice 3"), QStringLiteral("fp3")},
        {QStringLiteral("Sprint Qualifying"), QStringLiteral("sprint_quali")},
        {QStringLiteral("Sprint Shootout"), QStringLiteral("sprint_quali")},
        {QStringLiteral("Sprint"), QStringLiteral("sprint")},
        {QStringLiteral("Qualifying"), QStringLiteral("quali")},
        {QStringLiteral("Race"), QStringLiteral("race")},
    };
    return feedSessions.value(name);
}

std::optional<std::pair<QString, qint64>> parseSessionInfo(const QJsonValue& iv, const Race* race) {
    if (!iv.isObject() || race == nullptr) return std::nullopt;
    const QJsonObject info = iv.toObject();
    const QJsonObject meeting = obj(info.value(QStringLiteral("Meeting")));
    const auto number = toInt(meeting.value(QStringLiteral("Number")));
    const auto round = toInt(QJsonValue(race->round));
    if (!number || !round || *number != *round) return std::nullopt;

    const QJsonValue nv = info.value(QStringLiteral("Name"));
    const QString sid = feedSessionId(truthy(nv) ? str(nv) : QString());
    const auto ts = parseLocal(info.value(QStringLiteral("StartDate")), info.value(QStringLiteral("GmtOffset")));
    if (sid.isEmpty() || !ts) return std::nullopt;
    const auto base = sessionTs(race->rawSessions, sid);
    if (!base || std::llabs(*ts - *base) > MAX_FEED_SHIFT) return std::nullopt;
    return std::make_pair(sid, *ts);
}

// ── OpenF1 ───────────────────────────────────────────────────────────────────

Drivers parseOpenf1Drivers(const QJsonValue& a) {
    Drivers out;
    for (const QJsonValue& dv : bounded(a, 60)) {
        if (!dv.isObject()) continue;
        const QJsonObject d = dv.toObject();
        const auto k = driverKey(d.value(QStringLiteral("driver_number")));
        if (!k) continue;
        out.insert(*k, QJsonObject{
                           {QStringLiteral("acr"), safe(orv(get(d, "name_acronym"), QStringLiteral("#") + *k), 8)},
                           {QStringLiteral("color"), hexColor(get(d, "team_colour")).value_or(QString())},
                           {QStringLiteral("team"), safe(get(d, "team_name"), 32)},
                       });
    }
    return out;
}

std::pair<QJsonArray, qint64> parseOpenf1Positions(const QJsonValue& a, const Drivers& drivers) {
    struct Latest {
        qint64 pos;
        QString date;
    };
    QList<QString> order;   // dict insertion order
    QHash<QString, Latest> latest;
    for (const QJsonValue& rv : bounded(a, 20000)) {
        if (!rv.isObject()) continue;
        const QJsonObject r = rv.toObject();
        const auto k = driverKey(r.value(QStringLiteral("driver_number")));
        const auto pos = toInt(r.value(QStringLiteral("position")));
        if (!pos) continue;
        const QJsonValue dv = r.value(QStringLiteral("date"));
        const QString date = dv.isString() ? dv.toString() : QString();
        if (k && 1 <= *pos && *pos <= 99) {
            auto it = latest.find(*k);
            if (it == latest.end()) {
                order.append(*k);
                latest.insert(*k, {*pos, date});
            } else if (date > it->date) {
                *it = {*pos, date};
            }
        }
    }
    QList<QJsonObject> rows;
    qint64 newest = 0;
    for (const QString& k : order) {
        const Latest& v = latest[k];
        newest = std::max(newest, parseIso(v.date).value_or(0));
        const QJsonObject info = drivers.value(k);
        rows.append(QJsonObject{
            {QStringLiteral("pos"), v.pos},
            {QStringLiteral("num"), k},
            {QStringLiteral("code"), getOr(info, "acr", QStringLiteral("#") + k)},
            {QStringLiteral("color"), getOr(info, "color", QString())},
            {QStringLiteral("team"), getOr(info, "team", QString())},
            {QStringLiteral("best"), QString()},
            {QStringLiteral("gap"), QString()},
            {QStringLiteral("state"), QString()},
            {QStringLiteral("out_part"), 0},
            {QStringLiteral("in_pit"), false},
            {QStringLiteral("pits"), QJsonValue()},
        });
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QJsonObject& x, const QJsonObject& y) {
        return x.value(QStringLiteral("pos")).toInteger() < y.value(QStringLiteral("pos")).toInteger();
    });
    QJsonArray out;
    for (qsizetype i = 0; i < rows.size() && i < 30; ++i) out.append(rows[i]);
    return {out, newest};
}

void applyOpenf1Intervals(QJsonArray& rows, const QJsonValue& a) {
    struct Latest {
        QString date;
        QJsonValue lead, interval;
    };
    QHash<QString, Latest> latest;
    for (const QJsonValue& rv : bounded(a, 20000)) {
        if (!rv.isObject()) continue;
        const QJsonObject r = rv.toObject();
        const auto k = driverKey(r.value(QStringLiteral("driver_number")));
        const QJsonValue dv = r.value(QStringLiteral("date"));
        const QString date = dv.isString() ? dv.toString() : QString();
        if (k && (!latest.contains(*k) || date > latest[*k].date))
            latest.insert(*k, {date, r.value(QStringLiteral("gap_to_leader")), r.value(QStringLiteral("interval"))});
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        QJsonObject row = rows.at(i).toObject();
        const QString numKey = str(getOr(row, "num", QString()));
        auto it = latest.constFind(numKey);
        if (it == latest.constEnd()) continue;
        const bool leader = toInt(row.value(QStringLiteral("pos"))) == 1 && row.value(QStringLiteral("pos")).isDouble();
        row.insert(QStringLiteral("best"), leader ? QString() : fmtGap(it->lead));
        row.insert(QStringLiteral("gap"), leader ? QString() : fmtGap(it->interval));
        rows.replace(i, row);
    }
}

QString parseOpenf1Flag(const QJsonValue& a) {
    QString last;
    for (const QJsonValue& mv : bounded(a, 2000)) {
        if (!mv.isObject()) continue;
        const QJsonObject m = mv.toObject();
        const QJsonValue flag = m.value(QStringLiteral("flag"));
        if (m.value(QStringLiteral("category")) == QJsonValue(QStringLiteral("Flag")) &&
            m.value(QStringLiteral("scope")) == QJsonValue(QStringLiteral("Track")) && flag.isString() &&
            !flag.toString().isEmpty()) {
            last = safe(flag, 16);
        } else if (m.value(QStringLiteral("category")) == QJsonValue(QStringLiteral("SafetyCar")) &&
                   m.value(QStringLiteral("message")).isString()) {
            const QString msg = m.value(QStringLiteral("message")).toString().toUpper();
            if (msg.contains(QStringLiteral("VIRTUAL SAFETY CAR DEPLOYED"))) last = QStringLiteral("VSC");
            else if (msg.contains(QStringLiteral("VIRTUAL SAFETY CAR ENDING"))) last = QStringLiteral("VSC ENDING");
            else if (msg.contains(QStringLiteral("SAFETY CAR DEPLOYED"))) last = QStringLiteral("SAFETY CAR");
        }
    }
    return last;
}

QStringList parseOpenf1RaceControl(const QJsonValue& a, int limit) {
    QStringList msgs;
    for (const QJsonValue& mv : bounded(a, 2000))
        if (mv.isObject() && mv.toObject().value(QStringLiteral("message")).isString())
            msgs << safe(mv.toObject().value(QStringLiteral("message")), 90);
    QStringList out;
    for (qsizetype i = msgs.size() - 1; i >= 0 && out.size() < limit; --i) out << msgs[i];
    return out;
}

namespace {
QJsonObject openf1Session(const QJsonObject& s) {
    const QJsonValue key = s.value(QStringLiteral("session_key"));
    const QJsonValue meeting = s.value(QStringLiteral("meeting_key"));
    return QJsonObject{
        {QStringLiteral("name"), safe(get(s, "session_name"), 24)},
        {QStringLiteral("kind"), safe(get(s, "session_type"), 24)},
        {QStringLiteral("part"), 0},
        {QStringLiteral("status"), QString()},
        {QStringLiteral("start"), parseIso(get(s, "date_start")).value_or(0)},
        {QStringLiteral("end"), parseIso(get(s, "date_end")).value_or(0)},
        {QStringLiteral("location"), safe(get(s, "location"), 32)},
        {QStringLiteral("circuit"), safe(get(s, "circuit_short_name"), 32)},
        {QStringLiteral("key"), isIntValue(key) ? key : QJsonValue()},
        {QStringLiteral("meeting"), isIntValue(meeting) ? meeting : QJsonValue()},
        {QStringLiteral("cancelled"), truthy(get(s, "is_cancelled"))},
    };
}
}  // namespace

QJsonArray parseOpenf1Sessions(const QJsonValue& a) {
    QList<QJsonObject> list;
    for (const QJsonValue& v : bounded(a, 400)) {
        if (!v.isObject()) continue;
        const QJsonObject s = openf1Session(v.toObject());
        if (s.value(QStringLiteral("key")).isNull()) continue;
        list.append(s);
    }
    std::stable_sort(list.begin(), list.end(), [](const QJsonObject& x, const QJsonObject& y) {
        return x.value(QStringLiteral("start")).toInteger() < y.value(QStringLiteral("start")).toInteger();
    });
    QJsonArray out;
    for (const QJsonObject& s : list) out.append(s);
    return out;
}

std::optional<QJsonObject> parseOpenf1Session(const QJsonValue& a) {
    if (!a.isArray() || a.toArray().isEmpty() || !a.toArray().last().isObject()) return std::nullopt;
    return openf1Session(a.toArray().last().toObject());
}

QJsonArray parseOpenf1StartingGrid(const QJsonValue& a, const Drivers& drivers, const QJsonArray& provisional) {
    QHash<QString, QJsonObject> byCode;
    for (const QJsonValue& rv : provisional) {
        const QJsonObject r = obj(rv);
        byCode.insert(r.contains(QStringLiteral("code")) ? str(r.value(QStringLiteral("code"))) : QString(), r);
    }
    QList<QJsonObject> rows;
    for (const QJsonValue& ev : bounded(a, 30)) {
        if (!ev.isObject()) continue;
        const QJsonObject e = ev.toObject();
        const auto k = driverKey(e.value(QStringLiteral("driver_number")));
        const auto pos = toInt(e.value(QStringLiteral("position")));
        if (!pos) continue;
        if (!k || !(1 <= *pos && *pos <= 30)) continue;
        const QJsonObject info = drivers.value(*k);
        const QJsonValue acr = info.value(QStringLiteral("acr"));
        const QString code = truthy(acr) ? str(acr) : QStringLiteral("#") + *k;
        const QJsonObject q = byCode.value(code);
        const auto qpos = toInt(q.value(QStringLiteral("pos")));
        const qint64 moved = qpos ? *pos - *qpos : 0;
        rows.append(QJsonObject{
            {QStringLiteral("pos"), QString::number(*pos)},
            {QStringLiteral("code"), code},
            {QStringLiteral("name"), getOr(q, "name", QString())},
            {QStringLiteral("team"), orv(q.value(QStringLiteral("team")), getOr(info, "team", QString()))},
            {QStringLiteral("team_id"), getOr(q, "team_id", QString())},
            {QStringLiteral("color"), getOr(info, "color", QString())},
            {QStringLiteral("url"), getOr(q, "url", QString())},
            {QStringLiteral("detail"), fmtLap(e.value(QStringLiteral("lap_duration")))},
            {QStringLiteral("moved"), moved},
        });
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QJsonObject& x, const QJsonObject& y) {
        return x.value(QStringLiteral("pos")).toString().toInt() < y.value(QStringLiteral("pos")).toString().toInt();
    });
    QJsonArray out;
    for (const auto& r : rows) out.append(r);
    return out;
}

std::optional<QJsonObject> summariseOpenf1Weather(const QJsonValue& a) {
    QList<double> air, track, hum, wind;
    bool rain = false;
    for (const QJsonValue& ev : bounded(a, 2000)) {
        if (!ev.isObject()) continue;
        const QJsonObject e = ev.toObject();
        const std::pair<const char*, QList<double>*> cols[] = {
            {"air_temperature", &air}, {"track_temperature", &track}, {"humidity", &hum}, {"wind_speed", &wind}};
        for (const auto& [key, out] : cols) {
            const QJsonValue v = e.value(QLatin1StringView(key));
            if (v.isDouble() || v.isBool()) {
                const double x = v.isBool() ? (v.toBool() ? 1.0 : 0.0) : v.toDouble();
                if (-50 < x && x < 90) out->append(x);
            }
        }
        const QJsonValue rf = e.value(QStringLiteral("rainfall"));
        const bool dry = rf.isUndefined() || rf.isNull() || (rf.isDouble() && rf.toDouble() == 0) ||
                         (rf.isBool() && !rf.toBool()) || (rf.isString() && rf.toString() == u"0");
        if (!dry) rain = true;
    }
    if (air.isEmpty()) return std::nullopt;
    auto mn = [](const QList<double>& v) { return *std::min_element(v.begin(), v.end()); };
    auto mx = [](const QList<double>& v) { return *std::max_element(v.begin(), v.end()); };
    double humSum = 0;
    for (double h : hum) humSum += h;
    return QJsonObject{
        {QStringLiteral("air_min"), mn(air)},
        {QStringLiteral("air_max"), mx(air)},
        {QStringLiteral("track_max"), track.isEmpty() ? QJsonValue() : QJsonValue(mx(track))},
        {QStringLiteral("track_min"), track.isEmpty() ? QJsonValue() : QJsonValue(mn(track))},
        {QStringLiteral("humidity"), hum.isEmpty() ? QJsonValue() : QJsonValue(humSum / hum.size())},
        {QStringLiteral("wind_max"), wind.isEmpty() ? QJsonValue() : QJsonValue(mx(wind) * 3.6)},
        {QStringLiteral("rain"), rain},
        {QStringLiteral("samples"), air.size()},
    };
}

QString fmtLap(const QJsonValue& seconds) {
    if (seconds.isNull() || seconds.isUndefined()) return {};
    const auto s = toFloat(seconds);
    if (!s) return {};
    return lapText(*s);
}

QJsonArray parseOpenf1SessionResult(const QJsonValue& a, const Drivers& drivers, const QString& kind) {
    QList<QJsonObject> rows;
    for (const QJsonValue& ev : bounded(a, 60)) {
        if (!ev.isObject()) continue;
        const QJsonObject e = ev.toObject();
        const auto k = driverKey(e.value(QStringLiteral("driver_number")));
        if (!k) continue;
        const QJsonObject info = drivers.value(*k);
        QJsonValue dur = e.value(QStringLiteral("duration")), gap = e.value(QStringLiteral("gap_to_leader"));
        int part = 0;
        if (dur.isArray()) {   // qualifying: one entry per part
            const QJsonArray d = dur.toArray();
            qsizetype idx = -1;
            for (qsizetype i = 0; i < d.size(); ++i)
                if (!d.at(i).isNull()) idx = i;
            part = static_cast<int>(idx + 1);
            dur = idx >= 0 ? d.at(idx) : QJsonValue();
            gap = (gap.isArray() && idx >= 0 && idx < gap.toArray().size()) ? gap.toArray().at(idx) : QJsonValue();
        }
        const auto pos = toInt(e.value(QStringLiteral("position")));
        const QString status = truthy(get(e, "dsq"))   ? QStringLiteral("DSQ")
                               : truthy(get(e, "dns")) ? QStringLiteral("DNS")
                               : truthy(get(e, "dnf")) ? QStringLiteral("DNF")
                                                       : QString();
        QString detail, extra;
        if (kind == u"Race") {
            detail = !status.isEmpty() ? status : (pos == 1 ? fmtLap(dur) : gapText(gap));
        } else {
            detail = !status.isEmpty() ? status : fmtLap(dur);
            extra = pos == 1 ? QString() : gapText(gap);
        }
        if (kind == u"Qualifying" && part && status.isEmpty())
            extra = (extra.isEmpty() ? QString() : extra + QStringLiteral("  ")) + QStringLiteral("Q%1").arg(part);
        const QJsonValue pv = e.value(QStringLiteral("points"));
        QString pts;
        if (truthy(pv)) {
            const double p = toFloat(pv).value_or(0);
            pts = p == std::trunc(p) ? QString::number(static_cast<qint64>(p)) : fmtG(p);
        }
        const QJsonValue laps = e.value(QStringLiteral("number_of_laps"));
        rows.append(QJsonObject{
            {QStringLiteral("pos"), pos ? QString::number(*pos) : QString(QChar(0x2013))},
            {QStringLiteral("num"), *k},
            {QStringLiteral("code"), getOr(info, "acr", QStringLiteral("#") + *k)},
            {QStringLiteral("team"), getOr(info, "team", QString())},
            {QStringLiteral("color"), getOr(info, "color", QString())},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("gap"), extra},
            {QStringLiteral("laps"), isIntValue(laps) ? laps : QJsonValue()},
            {QStringLiteral("pts"), pts},
            {QStringLiteral("status"), status},
        });
    }
    auto key = [](const QJsonObject& r) {
        const QString p = r.value(QStringLiteral("pos")).toString();
        return std::make_pair(!isDigits(p), isDigits(p) ? p.toLongLong() : 99LL);
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const QJsonObject& x, const QJsonObject& y) { return key(x) < key(y); });
    QJsonArray out;
    for (qsizetype i = 0; i < rows.size() && i < 30; ++i) out.append(rows[i]);
    return out;
}

}  // namespace f1::parsers
