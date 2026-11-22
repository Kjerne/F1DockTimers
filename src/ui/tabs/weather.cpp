#include "weather.h"

#include "../../core/i18n.h"
#include "../../core/pyjson.h"

namespace f1::ui {

namespace wx = f1::weather;

namespace {
QString n0(double v) { return QString::number(static_cast<qint64>(py::round(v))); }
QString n1(double v) { return QString::number(std::nearbyint(v * 10) / 10, 'f', 1); }

std::optional<wx::Window> windowFor(const wx::Forecast* fc, const Session& s) {
    return wx::forecastWindow(fc, s.ts, s.ts + durationMin(s.id) * 60LL);
}
}  // namespace

QJsonObject measuredFor(const DataStore* store, const Race* race) {
    const QJsonObject m = store->get(QStringLiteral("measured_weather"));
    if (!race || m.value(QStringLiteral("round")).toString() != race->season + u'-' + race->round) return {};
    return m.value(QStringLiteral("sessions")).toObject();
}

const wx::Forecast* forecastFor(const DataStore* store, const Race* race) {
    const wx::Forecast* fc = store->forecast();
    if (!race || !fc || fc->round != race->season + u'-' + race->round || fc->times.isEmpty()) return nullptr;
    return fc;
}

QString sessionCells(Tab* tab, Row* r, const Session& s, const wx::Forecast* fc, const QJsonObject& measured, qint64 now,
                     int size) {
    const Theme& t = tab->t;
    const QString units = tab->ctx->settings->units;
    const auto win = fc ? windowFor(fc, s) : std::nullopt;
    const QJsonObject m = measured.value(s.id).toObject();
    if (!m.isEmpty()) {
        const bool rain = m.value(QStringLiteral("rain")).toBool();
        const double lo = m.value(QStringLiteral("air_min")).toDouble(), hi = m.value(QStringLiteral("air_max")).toDouble();
        r->cell(new Text(rain ? T(QStringLiteral("Rained")) : T(QStringLiteral("Dry")), t, size, rain, rain ? t.error : t.onSurface), -1, 1);
        r->cell(new Text(wx::tempRange(lo, hi, units), t, size, true, t.onSurface, Qt::AlignRight), TEMP_W);
        r->cell(new Text(rain ? T(QStringLiteral("Wet")) : T(QStringLiteral("Dry")), t, size - 1, rain, rain ? t.error : t.muted,
                         Qt::AlignRight),
                RAIN_W);
        QStringList tip{T(QStringLiteral("Measured at the track by F1 during the session:")),
                        T(QStringLiteral("Air {low} to {high}"), {{"low", wx::temp(lo, units, true)}, {"high", wx::temp(hi, units, true)}})};
        const QJsonValue trackMax = m.value(QStringLiteral("track_max"));
        if (trackMax.isDouble()) tip << T(QStringLiteral("Track up to {temp}"), {{"temp", wx::temp(trackMax.toDouble(), units, true)}});
        const QJsonValue hum = m.value(QStringLiteral("humidity"));
        if (hum.isDouble()) tip << T(QStringLiteral("Humidity {value}%"), {{"value", n0(hum.toDouble())}});
        tip << (rain ? T(QStringLiteral("Rain fell: yes")) : T(QStringLiteral("Rain fell: no")));
        if (win)
            tip << T(QStringLiteral("The forecast was: {sky}, {chance}% chance of rain."),
                     {{"sky", wx::condition(win->code)}, {"chance", n0(win->rainProb.value_or(0))}});
        return tip.join(u'\n');
    }
    if (!win) {
        r->cell(new Text(s.ts > now ? T(QStringLiteral("not yet")) : QString(), t, size - 1, false, t.muted), -1, 1);
        r->cell(new Text(QString(), t, size), TEMP_W);
        r->cell(new Text(QString(), t, size), RAIN_W);
        return {};
    }
    const auto [verdict, level] = wx::outlook(win);
    const QString color = level == u"wet" ? t.error : (level == u"risk" ? t.secondary : t.muted);
    r->cell(new Text(wx::condition(win->code), t, size), -1, 1);
    r->cell(new Text(wx::tempRange(win->tempMin, win->tempMax, units), t, size, true, t.onSurface, Qt::AlignRight), TEMP_W);
    r->cell(new Text(T(QStringLiteral("{chance}%"), {{"chance", n0(win->rainProb.value_or(0))}}), t, size - 1, false, color,
                     Qt::AlignRight),
            RAIN_W);
    QStringList tip{T(QStringLiteral("Forecast for the session:")), verdict, wx::condition(win->code)};
    if (win->rainTotal) tip << T(QStringLiteral("{mm} mm rain expected"), {{"mm", n1(win->rainTotal)}});
    if (win->wind.value_or(0)) tip << T(QStringLiteral("Wind up to {speed}"), {{"speed", wx::wind(win->wind, units)}});
    if (s.ts + durationMin(s.id) * 60LL <= now) tip << T(QStringLiteral("Measured values appear here shortly after the session (OpenF1)."));
    tip.removeAll(QString());
    return tip.join(u'\n');
}

WeatherTab::WeatherTab(Ctx* c, QWidget* parent) : Tab(c, parent), m_nameW(c->compact ? 112 : 128) {
    const qint64 now = nowSec();
    const Race* race = ctx->store->race();
    live(race, now);
    if (!race) {
        hint(T(QStringLiteral("No upcoming race")));
        finish();
        return;
    }
    const QString place = (race->locality.isEmpty() ? race->circuitName : race->locality).toUpper();
    const wx::Forecast* fc = forecastFor(ctx->store, race);
    if (!fc) {
        const qint64 first = race->sessions.isEmpty() ? 0 : race->sessions.first().ts;
        header(T(QStringLiteral("FORECAST · {place}"), {{"place", place}}));
        if (!race->lat) hint(T(QStringLiteral("No location known for this circuit.")));
        else if (first - now > wx::FORECAST_DAYS * 86400LL)
            hint(T(QStringLiteral("The forecast appears about two weeks before the race weekend.")));
        else hint(T(QStringLiteral("Loading the forecast…")));
        finish();
        return;
    }
    // The session that matters most right now: the running one, else the next, else the race.
    auto focus = currentSession(race->sessions, now);
    if (!focus) focus = nextSession(race->sessions, now);
    if (!focus) focus = race->sessions.last();
    outlook(fc, *focus, now);
    lay->addSpacing(t.px(12));

    header(T(QStringLiteral("ALL SESSIONS · {place}"), {{"place", place}}));
    columns(T(QStringLiteral("SESSION")), T(QStringLiteral("START")),
            T(QStringLiteral("Sky, temperature and the highest chance of rain during each session. Hover a row for details.")));
    const QJsonObject measured = measuredFor(ctx->store, race);
    for (const Session& s : race->sessions) {
        const bool isFocus = s.id == focus->id && s.ts == focus->ts;
        Row* r = row(new Text(s.name(), t, 13, isFocus, isFocus ? t.accent : t.onSurface),
                     new Text(fmtDatetime(s.ts, ctx->zone(), ctx->twelve()), t, 12, false, t.muted));
        r->setToolTip(sessionCells(this, r, s, fc, measured, now));
        lay->addWidget(r);
    }
    lay->addSpacing(t.px(8));
    hint(wx::refreshText(ctx->settings->weather_refresh, ctx->settings->weather_custom), t.muted);
    hint(T(QStringLiteral("Weather data by Open-Meteo.com (CC BY 4.0)")), t.muted);
    finish();
}

Row* WeatherTab::row(QWidget* first, QWidget* second) {
    auto* r = new Row(t);
    r->cell(first, m_nameW);
    r->cell(second ? second : new Text(QString(), t, 12), m_startW);
    return r;
}

void WeatherTab::values(Row* r, const QString& sky, const QString& temp, qint64 chance, const QString& color, int size,
                        bool boldRain) {
    r->cell(new Text(sky, t, size), -1, 1);
    r->cell(new Text(temp, t, size, true, t.onSurface, Qt::AlignRight), TEMP_W);
    r->cell(new Text(T(QStringLiteral("{chance}%"), {{"chance", QString::number(chance)}}), t, size - 1, boldRain, color, Qt::AlignRight),
            RAIN_W);
}

void WeatherTab::columns(const QString& first, const QString& second, const QString& tip) {
    Row* r = row(withHelp(new Text(first, t, 10, true, t.muted, Qt::AlignLeft, 0.3), t),
                 new Text(second, t, 10, true, t.muted, Qt::AlignLeft, 0.3));
    r->lay->setContentsMargins(t.px(4), 0, t.px(4), 0);
    r->cell(new Text(T(QStringLiteral("SKY")), t, 10, true, t.muted, Qt::AlignLeft, 0.3), -1, 1);
    r->cell(new Text(T(QStringLiteral("TEMP")), t, 10, true, t.muted, Qt::AlignRight, 0.3), TEMP_W);
    r->cell(new Text(T(QStringLiteral("RAIN")), t, 10, true, t.muted, Qt::AlignRight, 0.3), RAIN_W);
    r->setToolTip(tip);
    lay->addWidget(r);
}

void WeatherTab::live(const Race* race, qint64 now) {
    const QJsonObject liveObj = ctx->store->get(QStringLiteral("live"));
    const QJsonValue lwv = liveObj.value(QStringLiteral("weather"));
    if (!lwv.isObject() || lwv.toObject().isEmpty() ||
        !liveActive(liveObj, race ? race->sessions : Sessions(), now, ctx->settings->debug_force_tabs))
        return;
    const auto lw = wx::LiveWeather::fromJson(lwv.toObject());
    const QString& units = ctx->settings->units;
    header(T(QStringLiteral("TRACK NOW")));
    QStringList parts;
    if (lw.air) parts << T(QStringLiteral("Air {temp}"), {{"temp", wx::temp(lw.air, units, true)}});
    if (lw.track) parts << T(QStringLiteral("Track {temp}"), {{"temp", wx::temp(lw.track, units, true)}});
    auto* r = new Row(t);
    r->cell(withHelp(new Text(parts.join(QStringLiteral("  ·  ")), t, 15, true, t.onSurface), t), -1, 1);
    r->cell(new Text(lw.rain ? T(QStringLiteral("Raining")) : T(QStringLiteral("Dry")), t, 13, true, lw.rain ? t.error : t.accent,
                     Qt::AlignRight),
            TEMP_W + RAIN_W);
    r->setToolTip(T(QStringLiteral("Measured at the track by F1 and updated live during the session.\n"
                                   "Raining / Dry: whether F1's track sensors report rain right now.")));
    lay->addWidget(r);
    QStringList extra;
    if (lw.humidity) extra << T(QStringLiteral("Humidity {value}%"), {{"value", n0(*lw.humidity)}});
    if (lw.wind) extra << T(QStringLiteral("Wind {speed}"), {{"speed", wx::wind(*lw.wind * 3.6, units)}});
    if (!extra.isEmpty()) hint(extra.join(QStringLiteral("  ·  ")));
    hint(T(QStringLiteral("Live from F1's timing feed.")), t.muted);
    lay->addSpacing(t.px(12));
}

void WeatherTab::outlook(const wx::Forecast* fc, const Session& s, qint64 now) {
    const QString& units = ctx->settings->units;
    const auto win = windowFor(fc, s);
    const qint64 sEnd = s.ts + durationMin(s.id) * 60LL;
    const bool running = s.ts <= now && now < sEnd;
    header(running ? T(QStringLiteral("{session} · NOW"), {{"session", s.name().toUpper()}})
                   : T(QStringLiteral("{session} OUTLOOK"), {{"session", s.name().toUpper()}}));
    if (!win) {
        hint(T(QStringLiteral("No forecast for this session yet.")));
        return;
    }
    const auto [verdict, level] = wx::outlook(win);
    const QString verdictColor = level == u"wet" ? t.error : (level == u"risk" ? t.secondary : t.accent);
    // Line 1: sky and temperature, the temperature in the table's temperature column.
    auto* top = new Row(t);
    top->cell(withHelp(new Text(wx::condition(win->code), t, 15, true, t.onSurface), t), -1, 1);
    top->cell(new Text(wx::tempRange(win->tempMin, win->tempMax, units, true), t, 15, true, t.onSurface, Qt::AlignRight),
              TEMP_W + RAIN_W);
    top->setToolTip(T(QStringLiteral("The forecast for the whole session, from lights out to the flag.\n"
                                     "Air temperature from lowest to highest; the track is usually 10 to 20 °C warmer in the sun.")));
    lay->addWidget(top);
    // Line 2: the rain verdict in colour, then the details.
    QStringList bits{verdict};
    if (win->wetFrom)
        bits << T(QStringLiteral("rain likely from {time}"), {{"time", fmtClockTime(*win->wetFrom, ctx->zone(), ctx->twelve())}});
    if (win->rainTotal) bits << T(QStringLiteral("{mm} mm expected"), {{"mm", n1(win->rainTotal)}});
    if (win->wind.value_or(0)) bits << T(QStringLiteral("wind up to {speed}"), {{"speed", wx::wind(win->wind, units)}});
    bits.removeAll(QString());
    auto* line = new Row(t);
    line->lay->setContentsMargins(t.px(4), 0, t.px(4), t.px(2));
    line->cell(new Text(bits.join(QStringLiteral(" · ")), t, 12, false, verdictColor), -1, 1);
    line->setToolTip(T(QStringLiteral("Dry: rain unlikely (under 25%).\nRain possible: showers may come during the session.\n"
                                      "Wet: rain expected at some point during the session.")));
    lay->addWidget(line);
    // Hour by hour, from an hour before the session to an hour after it. The extra hours are
    // greyed out; the verdict above counts the session only.
    QList<wx::Hour> hours;
    for (qsizetype i = 0; i < fc->times.size() && hours.size() < 8; ++i) {
        const qint64 ts = fc->times[i];
        if (s.ts - 3600 - 1800 <= ts && ts < sEnd + 3600) {
            const wx::Hour h = fc->hourAt(i);
            if (h.temp) hours << h;
        }
    }
    if (hours.size() <= 1) return;
    lay->addSpacing(t.px(6));
    columns(T(QStringLiteral("HOUR")), QString(),
            T(QStringLiteral("Hour by hour, from an hour before the session to an hour after it. Greyed-out hours are outside the session.")));
    for (const wx::Hour& h : hours) {
        const bool inside = s.ts - 1800 <= h.ts && h.ts < sEnd;
        const bool past = h.ts + 3600 <= now;
        const bool dim = past || !inside;
        Row* r = row(new Text(fmtClockTime(h.ts, ctx->zone(), ctx->twelve()), t, 12, inside, dim ? t.muted : t.onSurface));
        const bool wet = wx::isWet(h);
        values(r, wx::condition(h.code), wx::temp(h.temp, units), static_cast<qint64>(py::round(h.rainProb.value_or(0))),
               wet ? t.error : t.muted, 12, wet);
        if (!inside && !wet) {   // outside the session: clearly fainter
            const QString faint = QStringLiteral("#80") + t.muted.right(6);
            for (int i = 0; i < r->lay->count(); ++i)
                if (auto* w = qobject_cast<Text*>(r->lay->itemAt(i)->widget())) w->setColor(faint);
        }
        QStringList tip;
        if (!inside) tip << (h.ts < s.ts ? T(QStringLiteral("Before the session")) : T(QStringLiteral("After the session")));
        tip << T(QStringLiteral("Chance of rain in this hour: {chance}%"), {{"chance", n0(h.rainProb.value_or(0))}});
        if (h.rain.value_or(0)) tip << T(QStringLiteral("{mm} mm rain"), {{"mm", n1(*h.rain)}});
        if (h.wind) tip << T(QStringLiteral("Wind {speed}"), {{"speed", wx::wind(h.wind, units)}});
        r->setToolTip(tip.join(u'\n'));
        lay->addWidget(r);
    }
}

}  // namespace f1::ui
