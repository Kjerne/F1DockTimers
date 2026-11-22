#include "f1feed.h"

#include "pyjson.h"
#include "sanitize.h"
#include "timeutil.h"
#include "weather.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <map>

namespace f1 {

using namespace f1::py;

namespace {

constexpr qsizetype MAX_LINE_BYTES = 4 * 1024 * 1024;
constexpr int MAX_DRIVERS = 30;

QJsonValue normalise(const QJsonValue& v) {
    if (v.isArray()) {
        QJsonObject o;
        const QJsonArray a = v.toArray();
        for (qsizetype i = 0; i < a.size(); ++i) o.insert(QString::number(i), normalise(a.at(i)));
        return o;
    }
    if (v.isObject()) {
        QJsonObject o;
        const QJsonObject src = v.toObject();
        for (auto it = src.begin(); it != src.end(); ++it)
            if (it.key() != u"_kf") o.insert(it.key(), normalise(it.value()));
        return o;
    }
    return v;
}

void merge(QJsonObject& into, const QJsonObject& patch) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.key() == u"_kf") continue;
        if (it.value().isObject() && into.value(it.key()).isObject()) {
            QJsonObject child = into.value(it.key()).toObject();
            merge(child, it.value().toObject());
            into.insert(it.key(), child);
        } else {
            into.insert(it.key(), it.value());
        }
    }
}

QString text(const QJsonValue& v, int limit = 12) { return (v.isNull() || v.isUndefined()) ? QString() : safe(v, limit); }

QString valueOf(const QJsonValue& node) {
    return node.isObject() ? text(node.toObject().value(QStringLiteral("Value"))) : QString();
}

std::optional<qint64> clockSeconds(const QJsonValue& raw) {
    static const QRegularExpression re(QStringLiteral("^(\\d+):(\\d\\d):(\\d\\d)"));
    const auto m = re.match(truthy(raw) ? str(raw) : QString());
    if (!m.hasMatch()) return std::nullopt;
    return m.captured(1).toLongLong() * 3600 + m.captured(2).toLongLong() * 60 + m.captured(3).toLongLong();
}

std::optional<double> lapSeconds(const QString& s) {
    static const QRegularExpression mmss(QStringLiteral("^(\\d+):(\\d+\\.\\d+)$"));
    static const QRegularExpression ss(QStringLiteral("^(\\d+\\.\\d+)$"));
    if (const auto m = mmss.match(s); m.hasMatch()) return m.captured(1).toLongLong() * 60 + m.captured(2).toDouble();
    if (const auto m = ss.match(s); m.hasMatch()) return m.captured(1).toDouble();
    return std::nullopt;
}

qint64 intOr(const QJsonValue& v, qint64 d) { return toInt(v).value_or(d); }

QString trackStatus(const QString& s) {
    if (s == u"AllClear") return QStringLiteral("CLEAR");
    if (s == u"Yellow") return QStringLiteral("YELLOW");
    if (s == u"Red") return QStringLiteral("RED");
    if (s == u"SCDeployed") return QStringLiteral("SAFETY CAR");
    if (s == u"VSCDeployed") return QStringLiteral("VSC");
    if (s == u"VSCEnding") return QStringLiteral("VSC ENDING");
    return {};
}

struct Row {
    QJsonObject out;
    qint64 pos = 99, order = 99;
    std::map<int, std::optional<double>> seconds;
    int bestPart = 0;
};

Row buildRow(const QString& number, const QJsonObject& line, const QJsonObject& driver, int part) {
    QString best, gap, state;
    int outPart = 0;
    Row r;
    if (part > 0) {
        const QJsonObject times = obj(line.value(QStringLiteral("BestLapTimes")));
        for (int p = 1; p <= 3; ++p) r.seconds[p] = lapSeconds(valueOf(times.value(QString::number(p - 1))));
        for (int p = part; p > 0; --p) {
            const QString v = valueOf(times.value(QString::number(p - 1)));
            if (!v.isEmpty()) {
                best = v;
                r.bestPart = p;
                if (p < part) state = QStringLiteral("prev");
                break;
            }
        }
        if (line.value(QStringLiteral("KnockedOut")) == QJsonValue(true)) {
            state = QStringLiteral("out");
            for (int p = 3; p >= 1; --p)
                if (!valueOf(times.value(QString::number(p - 1))).isEmpty()) {
                    outPart = p;
                    break;
                }
            if (!outPart) outPart = part;
        } else if (line.value(QStringLiteral("Cutoff")) == QJsonValue(true)) {
            state = QStringLiteral("danger");
        }
    } else {
        const QString toLeader = text(line.value(QStringLiteral("GapToLeader")));
        if (!toLeader.isEmpty()) {
            best = toLeader;
            gap = valueOf(line.value(QStringLiteral("IntervalToPositionAhead")));
        } else {
            best = valueOf(line.value(QStringLiteral("BestLapTime")));
        }
    }
    r.pos = intOr(line.value(QStringLiteral("Position")), 99);
    r.order = intOr(line.value(QStringLiteral("Line")), 99);
    const QJsonValue tla = orv(driver.value(QStringLiteral("Tla")), QStringLiteral("#") + number);
    r.out = QJsonObject{
        {QStringLiteral("num"), number},
        {QStringLiteral("code"), text(tla, 4)},
        {QStringLiteral("color"), hexColor(driver.value(QStringLiteral("TeamColour"))).value_or(QString())},
        {QStringLiteral("team"), text(driver.value(QStringLiteral("TeamName")), 32)},
        {QStringLiteral("best"), best},
        {QStringLiteral("gap"), gap},
        {QStringLiteral("state"), state},
        {QStringLiteral("out_part"), outPart},
        {QStringLiteral("in_pit"), line.value(QStringLiteral("InPit")) == QJsonValue(true)},
        {QStringLiteral("pits"), intOr(line.value(QStringLiteral("NumberOfPitStops")), 0)},
    };
    return r;
}

}  // namespace

void FeedState::reset() {
    feeds = {};
    lastMessage = 0;
}

bool FeedState::handleLine(const QString& line, qint64 now) {
    if (line.size() > MAX_LINE_BYTES || !line.startsWith(QStringLiteral("data:"))) return false;
    bool changed = false;
    const QStringList frames = line.mid(5).split(QChar(0x1E));
    for (const QString& frame : frames) {
        const QString payload = frame.trimmed();
        if (!payload.startsWith(u'{')) continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
        const QJsonObject msg = doc.object();
        const QJsonValue type = msg.value(QStringLiteral("type"));
        if (type == QJsonValue(3) && msg.value(QStringLiteral("result")).isObject()) {
            const QJsonObject result = msg.value(QStringLiteral("result")).toObject();
            for (auto it = result.begin(); it != result.end(); ++it)
                if (it.value().isObject() || it.value().isArray()) feeds.insert(it.key(), normalise(it.value()));
            changed = true;
        } else if (type == QJsonValue(1) && msg.value(QStringLiteral("target")) == QJsonValue(QStringLiteral("feed")) &&
                   msg.value(QStringLiteral("arguments")).isArray()) {
            const QJsonArray args = msg.value(QStringLiteral("arguments")).toArray();
            if (args.size() >= 2 && args.at(0).isString() && (args.at(1).isObject() || args.at(1).isArray())) {
                const QString name = args.at(0).toString();
                QJsonObject cur = feeds.value(name).isObject() ? feeds.value(name).toObject() : QJsonObject();
                const QJsonValue patch = normalise(args.at(1));
                merge(cur, patch.toObject());
                feeds.insert(name, cur);
                changed = true;
            }
        }
    }
    if (changed) lastMessage = now;
    return changed;
}

std::optional<QJsonObject> FeedState::view(qint64 now) const {
    const QJsonValue tv = feeds.value(QStringLiteral("TimingData"));
    if (!tv.isObject() || !tv.toObject().value(QStringLiteral("Lines")).isObject()) return std::nullopt;
    const QJsonObject timing = tv.toObject();
    const QJsonObject drivers = obj(orv(feeds.value(QStringLiteral("DriverList")), QJsonObject()));
    const QJsonObject info = obj(orv(feeds.value(QStringLiteral("SessionInfo")), QJsonObject()));
    const QString kind = text(info.value(QStringLiteral("Type")), 24);
    int part = 0;
    if (kind == u"Qualifying") {
        const QJsonValue sp = timing.value(QStringLiteral("SessionPart"));
        part = truthy(sp) ? static_cast<int>(toInt(sp).value_or(0)) : 0;
    }

    static const QRegularExpression numRe(QStringLiteral("^\\d{1,3}$"));
    QList<Row> rows;
    const QJsonObject lines = timing.value(QStringLiteral("Lines")).toObject();
    for (auto it = lines.begin(); it != lines.end(); ++it) {
        if (numRe.match(it.key()).hasMatch() && it.value().isObject() && rows.size() < MAX_DRIVERS) {
            const QJsonObject drv = obj(drivers.value(it.key()));
            rows.append(buildRow(it.key(), it.value().toObject(), drv, part));
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return std::make_pair(a.order, a.pos) < std::make_pair(b.order, b.pos);
    });
    for (qsizetype i = 0; i < rows.size(); ++i) rows[i].out.insert(QStringLiteral("pos"), i + 1);

    if (part > 0) {
        std::map<int, double> leader;
        for (const Row& r : rows)
            for (int p = 1; p <= 3; ++p)
                if (!leader.count(p) && r.seconds.at(p)) leader[p] = *r.seconds.at(p);
        for (Row& r : rows) {
            auto t = r.seconds.count(r.bestPart) ? r.seconds.at(r.bestPart) : std::nullopt;
            auto f = leader.find(r.bestPart);
            if (t && f != leader.end() && *t - f->second > 0.0005)
                r.out.insert(QStringLiteral("gap"), QString::asprintf("+%.3f", *t - f->second));
        }
    }
    QJsonArray outRows;
    for (const Row& r : rows) outRows.append(r.out);

    const QJsonValue status = obj(orv(feeds.value(QStringLiteral("TrackStatus")), QJsonObject())).value(QStringLiteral("Message"));
    QJsonValue clock;
    const QJsonValue ex = feeds.value(QStringLiteral("ExtrapolatedClock"));
    if (ex.isObject()) {
        const QJsonObject e = ex.toObject();
        if (const auto remaining = clockSeconds(e.value(QStringLiteral("Remaining")))) {
            clock = QJsonObject{{QStringLiteral("remaining"), *remaining},
                                {QStringLiteral("at"), parseIso(e.value(QStringLiteral("Utc"))).value_or(now)},
                                {QStringLiteral("running"), e.value(QStringLiteral("Extrapolating")) == QJsonValue(true)}};
        }
    }
    const auto w = weather::parseLiveWeather(feeds.value(QStringLiteral("WeatherData")));
    return QJsonObject{
        {QStringLiteral("rows"), outRows},
        {QStringLiteral("flag"), trackStatus(truthy(status) ? str(status) : QString())},
        {QStringLiteral("latest_sec"), lastMessage},
        {QStringLiteral("session"),
         QJsonObject{{QStringLiteral("name"), text(info.value(QStringLiteral("Name")), 24)},
                     {QStringLiteral("kind"), kind},
                     {QStringLiteral("part"), part},
                     {QStringLiteral("status"), text(info.value(QStringLiteral("SessionStatus")), 16)}}},
        {QStringLiteral("clock"), clock},
        {QStringLiteral("weather"), w ? QJsonValue(w->toJson()) : QJsonValue()},
    };
}

}  // namespace f1
