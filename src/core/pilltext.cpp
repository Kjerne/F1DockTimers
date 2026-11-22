#include "pilltext.h"

#include "i18n.h"
#include "pyjson.h"
#include "teams.h"

#include <QRegularExpression>

namespace f1::pill {

using namespace f1::py;

QString shortSessionName(const QString& id) {
    if (id == u"fp1") return QStringLiteral("FP1");
    if (id == u"fp2") return QStringLiteral("FP2");
    if (id == u"fp3") return QStringLiteral("FP3");
    if (id == u"sprint_quali") return QStringLiteral("SQ");
    if (id == u"sprint") return QStringLiteral("Sprint");
    if (id == u"quali") return QStringLiteral("Quali");
    if (id == u"race") return QStringLiteral("GP");
    return id;
}

QString shortRace(const Race& race) {
    QString name = QString(race.name).replace(QStringLiteral(" Grand Prix"), QString()).trimmed();
    if (name.isEmpty()) name = race.locality;
    return name.size() <= 18 ? name : name.left(17) + QChar(0x2026);
}

const QStringList& tokenNames() {
    static const QStringList t = {
        QStringLiteral("round"),     QStringLiteral("race"),      QStringLiteral("race_full"),
        QStringLiteral("session"),   QStringLiteral("session_full"), QStringLiteral("time"),
        QStringLiteral("date"),      QStringLiteral("countdown"), QStringLiteral("countdown_short"),
        QStringLiteral("city"),      QStringLiteral("country"),   QStringLiteral("circuit"),
        QStringLiteral("live"),      QStringLiteral("flag"),      QStringLiteral("leader"),
        QStringLiteral("temp"),      QStringLiteral("rain"),      QStringLiteral("sky"),
        QStringLiteral("wx"),        QStringLiteral("track_temp")};
    return t;
}

QString tokenExample(const QString& k) {
    static const QMap<QString, const char*> ex = {
        {QStringLiteral("round"), F1_TR_NOOP("Round, e.g. R21")},
        {QStringLiteral("race"), F1_TR_NOOP("Short race name, e.g. Las Vegas")},
        {QStringLiteral("race_full"), F1_TR_NOOP("Las Vegas Grand Prix")},
        {QStringLiteral("session"), F1_TR_NOOP("FP1 / FP2 / Q2 / Sprint / GP")},
        {QStringLiteral("session_full"), F1_TR_NOOP("Practice 1")},
        {QStringLiteral("time"), F1_TR_NOOP("Local start, e.g. Fri 01:30")},
        {QStringLiteral("date"), F1_TR_NOOP("20 Nov")},
        {QStringLiteral("countdown"), F1_TR_NOOP("in 2d 6h 19m")},
        {QStringLiteral("city"), F1_TR_NOOP("Las Vegas")},
        {QStringLiteral("country"), F1_TR_NOOP("USA")},
        {QStringLiteral("circuit"), F1_TR_NOOP("Las Vegas Strip Street Circuit")},
        {QStringLiteral("live"), F1_TR_NOOP("LIVE, or LIVE · 12:34 left")},
        {QStringLiteral("flag"), F1_TR_NOOP("Track flag while live")},
        {QStringLiteral("leader"), F1_TR_NOOP("Leader while live")},
        {QStringLiteral("temp"), F1_TR_NOOP("Air temperature: live at the track, else the forecast, e.g. 18°")},
        {QStringLiteral("rain"), F1_TR_NOOP("Chance of rain during the session, e.g. 20%")},
        {QStringLiteral("sky"), F1_TR_NOOP("Forecast sky, e.g. Partly cloudy")},
        {QStringLiteral("wx"), F1_TR_NOOP("Temperature and rain chance, e.g. 18° · 20%")},
        {QStringLiteral("track_temp"), F1_TR_NOOP("Track temperature while live, e.g. 31°")},
    };
    if (k == u"countdown_short") return QStringLiteral("2d");
    return ex.contains(k) ? T(QString::fromUtf8(ex.value(k))) : QString();
}

QList<Preset> presets() {
    const QString dot = QStringLiteral(" · ");
    return {
        {QStringLiteral("Round, race and session"), QStringLiteral("{round}") + dot + QStringLiteral("{race}") + dot + QStringLiteral("{session}"),
         QStringLiteral("{time}") + dot + QStringLiteral("{countdown}"),
         QStringLiteral("{round}") + dot + QStringLiteral("{race}") + dot + QStringLiteral("● {session}"), QStringLiteral("{live}")},
        {T(QStringLiteral("Session and countdown")), QStringLiteral("{session}") + dot + QStringLiteral("{time}"), QStringLiteral("{countdown}"),
         QStringLiteral("● {session}"), QStringLiteral("{live}")},
        {T(QStringLiteral("Race and countdown")), QStringLiteral("{round}") + dot + QStringLiteral("{race}"),
         QStringLiteral("{session} {countdown}"), QStringLiteral("● {session} {race}"), QStringLiteral("{live}")},
        {T(QStringLiteral("Compact (one line)")), QStringLiteral("{session} {countdown_short}"), QString(),
         QStringLiteral("● {session} LIVE"), QString()},
    };
}

std::pair<Fields, bool> fields(const Race* race, const QJsonObject& live, const Settings& s, qint64 now,
                               const Zone& zone) {
    Fields f;
    for (const QString& k : tokenNames()) f.insert(k, QString());
    if (race == nullptr) {
        f[QStringLiteral("race")] = QStringLiteral("F1");
        f[QStringLiteral("session")] = QStringLiteral("F1");
        f[QStringLiteral("countdown")] = T(QStringLiteral("No upcoming race"));
        f[QStringLiteral("countdown_short")] = QString(QChar(0x2013));
        return {f, false};
    }
    f[QStringLiteral("round")] = QStringLiteral("R") + race->round;
    f[QStringLiteral("race")] = shortRace(*race);
    f[QStringLiteral("race_full")] = race->name;
    f[QStringLiteral("city")] = race->locality;
    f[QStringLiteral("country")] = race->country;
    f[QStringLiteral("circuit")] = race->circuitName;
    const Sessions& sessions = race->sessions;
    const bool twelve = s.time_format == u"12h";
    if (const auto cur = currentSession(sessions, now)) {
        QString name = shortSessionName(cur->id);
        const QJsonObject sess = obj(live.value(QStringLiteral("session")));
        const QJsonValue pv = sess.value(QStringLiteral("part"));
        const qint64 part = truthy(pv) ? toInt(pv).value_or(0) : 0;
        if ((cur->id == u"quali" || cur->id == u"sprint_quali") && part)
            name = (cur->id == u"sprint_quali" ? QStringLiteral("SQ") : QStringLiteral("Q")) + QString::number(part);
        f[QStringLiteral("session")] = name;
        f[QStringLiteral("session_full")] = sessionName(cur->id);
        f[QStringLiteral("time")] = fmtDatetime(cur->ts, zone, twelve);
        f[QStringLiteral("live")] = QStringLiteral("LIVE");
        f[QStringLiteral("countdown")] = T(QStringLiteral("live now"));
        f[QStringLiteral("countdown_short")] = QStringLiteral("LIVE");
        if (liveActive(live, sessions, now, s.debug_force_tabs)) {
            const QJsonValue cv = live.value(QStringLiteral("clock"));
            if (cv.isObject()) {
                const QJsonObject clock = cv.toObject();
                const double remaining = toFloat(clock.value(QStringLiteral("remaining"))).value_or(0);
                const double at = toFloat(clock.value(QStringLiteral("at"))).value_or(now);
                const double left = remaining - (truthy(clock.value(QStringLiteral("running"))) ? (now - at) : 0);
                f[QStringLiteral("live")] = QStringLiteral("LIVE · %1 left").arg(fmtClock(left));
            }
            const QJsonValue flag = live.value(QStringLiteral("flag"));
            f[QStringLiteral("flag")] = friendlyFlag(truthy(flag) ? str(flag) : QString());
            const QJsonArray rows = arr(live.value(QStringLiteral("rows")));
            // Spoiler-free: no leader unless results were revealed after this session started.
            const bool hidden = s.spoiler_free && s.spoiler_revealed_at < cur->ts;
            if (!rows.isEmpty() && !hidden) {
                const QJsonValue code = obj(rows.first()).value(QStringLiteral("code"));
                f[QStringLiteral("leader")] = code.isUndefined() ? QString() : str(code);
            }
        }
        return {f, true};
    }
    const auto nxt = nextSession(sessions, now);
    if (!nxt) {
        f[QStringLiteral("session")] = QString(QChar(0x2014));
        f[QStringLiteral("countdown")] = T(QStringLiteral("Weekend over"));
        f[QStringLiteral("countdown_short")] = T(QStringLiteral("done"));
        return {f, false};
    }
    const QDateTime dt = toLocal(nxt->ts, zone);
    f[QStringLiteral("session")] = shortSessionName(nxt->id);
    f[QStringLiteral("session_full")] = sessionName(nxt->id);
    f[QStringLiteral("time")] = fmtDatetime(nxt->ts, zone, twelve);
    f[QStringLiteral("date")] = QStringLiteral("%1 %2").arg(dt.date().day()).arg(monthName(dt.date().month()));
    f[QStringLiteral("countdown")] = fmtLong(static_cast<double>(nxt->ts - now));
    f[QStringLiteral("countdown_short")] = fmtShort(static_cast<double>(nxt->ts - now));
    return {f, false};
}

QString renderTemplate(const QString& tpl, const Fields& f) {
    QString text;
    for (qsizetype i = 0; i < tpl.size(); ++i) {
        const QChar c = tpl.at(i);
        if (c == u'{' && i + 1 < tpl.size() && tpl.at(i + 1) == u'{') {
            text += u'{';
            ++i;
        } else if (c == u'}' && i + 1 < tpl.size() && tpl.at(i + 1) == u'}') {
            text += u'}';
            ++i;
        } else if (c == u'{') {
            const qsizetype close = tpl.indexOf(u'}', i + 1);
            if (close < 0) {
                text += tpl.mid(i);
                break;
            }
            QString inner = tpl.mid(i + 1, close - i - 1);
            // field name ends at a conversion or format spec
            qsizetype cut = inner.size();
            for (QChar stop : {QChar(u'!'), QChar(u':')}) {
                const qsizetype k = inner.indexOf(stop);
                if (k >= 0 && k < cut) cut = k;
            }
            const QString name = inner.left(cut);
            const QString key = name.section(u'.', 0, 0).section(u'[', 0, 0);
            auto it = f.constFind(key);
            text += it != f.constEnd() ? *it : QStringLiteral("{") + name + QStringLiteral("}");
            i = close;
        } else {
            text += c;
        }
    }
    const QString dd = QStringLiteral("·  ·"), sd = QStringLiteral(" ·  ");
    while (text.contains(dd) || text.contains(sd)) {
        text.replace(dd, QStringLiteral("·"));
        text.replace(sd, QStringLiteral(" · "));
    }
    text = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).join(u' ');
    // strip(" ·")
    qsizetype a = 0, b = text.size();
    auto strip = [](QChar ch) { return ch == u' ' || ch == QChar(0x00B7); };
    while (a < b && strip(text.at(a))) ++a;
    while (b > a && strip(text.at(b - 1))) --b;
    return text.mid(a, b - a);
}

Fields weatherFields(const Race* race, const QJsonObject& live, const weather::Forecast* forecast, const Settings& s,
                     qint64 now) {
    Fields out{{QStringLiteral("temp"), {}}, {QStringLiteral("rain"), {}}, {QStringLiteral("sky"), {}},
               {QStringLiteral("wx"), {}},   {QStringLiteral("track_temp"), {}}};
    if (race == nullptr) return out;
    const QString& units = s.units;
    auto ses = currentSession(race->sessions, now);
    if (!ses) ses = nextSession(race->sessions, now);
    if (ses && forecast && forecast->round == race->season + u'-' + race->round) {
        const auto win = weather::forecastWindow(forecast, ses->ts, ses->ts + durationMin(ses->id) * 60LL);
        if (win) {
            out[QStringLiteral("temp")] = weather::temp(win->temp, units);
            out[QStringLiteral("rain")] = QString::number(static_cast<qint64>(py::round(win->rainProb.value_or(0)))) + u'%';
            out[QStringLiteral("sky")] = weather::condition(win->code);
            out[QStringLiteral("wx")] = weather::shortText(*win, units);
        }
    }
    const QJsonValue lwv = live.value(QStringLiteral("weather"));
    if (truthy(lwv) && lwv.isObject() && currentSession(race->sessions, now)) {
        const auto lw = weather::LiveWeather::fromJson(lwv.toObject());
        if (lw.air) {
            out[QStringLiteral("temp")] = weather::temp(lw.air, units);
            const QString rain = out.value(QStringLiteral("rain"));
            out[QStringLiteral("wx")] = out.value(QStringLiteral("temp")) +
                                        (rain.isEmpty() ? QString() : QStringLiteral(" · ") + rain);
        }
        if (lw.track) out[QStringLiteral("track_temp")] = weather::temp(lw.track, units);
        if (lw.rain) out[QStringLiteral("sky")] = T(QStringLiteral("Raining"));
    }
    return out;
}

State state(const Race* race, const QJsonObject& live, const Settings& s, qint64 now, const Zone& zone,
            const weather::Forecast* forecast) {
    auto [f, isLive] = fields(race, live, s, now, zone);
    static const QStringList wxTokens = {QStringLiteral("{temp}"), QStringLiteral("{rain}"), QStringLiteral("{sky}"),
                                         QStringLiteral("{wx}"), QStringLiteral("{track_temp}")};
    bool wantWeather = false;
    for (const QString* tpl : {&s.pill_top, &s.pill_bottom, &s.pill_live_top, &s.pill_live_bottom})
        for (const QString& tok : wxTokens)
            if (tpl->contains(tok)) wantWeather = true;
    if (wantWeather) {   // only when the user shows weather
        const Fields w = weatherFields(race, live, forecast, s, now);
        for (auto it = w.begin(); it != w.end(); ++it) f[it.key()] = it.value();
    }
    if (race == nullptr) return {QStringLiteral("F1"), T(QStringLiteral("No upcoming race")), false};
    const QString& top = isLive ? s.pill_live_top : s.pill_top;
    const QString& bottom = isLive ? s.pill_live_bottom : s.pill_bottom;
    const bool one = s.pill_lines == 1;
    return {renderTemplate(top, f), one ? QString() : renderTemplate(bottom, f), isLive};
}

std::tuple<QString, QString, bool> countdownState(const Race* race, const Settings& s, qint64 now) {
    const Sessions sessions = race ? race->sessions : Sessions();
    if (const auto live = currentSession(sessions, now))
        return {QStringLiteral("LIVE · ") + live->name().toUpper(), QStringLiteral("● LIVE"), true};
    const auto raceTs = sessionTs(sessions, QStringLiteral("race"));
    if (s.countdown_target == u"session") {
        if (const auto nxt = nextSession(sessions, now)) {
            const QString label = nxt->id == u"race"
                                      ? T(QStringLiteral("LIGHTS OUT"))
                                      : T(QStringLiteral("NEXT · {upper}"), {{"upper", nxt->name().toUpper()}});
            return {label, fmtLong(static_cast<double>(nxt->ts - now)), false};
        }
    }
    if (!raceTs) return {QString(), QString(), false};
    if (now >= *raceTs) return {T(QStringLiteral("LIGHTS OUT")), T(QStringLiteral("Race finished")), false};
    return {T(QStringLiteral("LIGHTS OUT")), fmtLong(static_cast<double>(*raceTs - now)), false};
}

}  // namespace f1::pill
