#pragma once
// What the taskbar countdown and the schedule countdown say. Pure.
// Port of the pure parts of ui/taskbar_pill.py (pill_fields, render_template, weather_fields,
// pill_state) and ui/tabs/schedule.py (countdown_state). They live in core here so the tests
// need nothing but Qt Core.

#include "../settings.h"
#include "sessions.h"
#include "timeutil.h"
#include "weather.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <tuple>

namespace f1::pill {

QString shortSessionName(const QString& id);   // FP1 / SQ / Sprint / Quali / GP
QString shortRace(const Race& race);           // "Las Vegas" from "Las Vegas Grand Prix"
const QStringList& tokenNames();
// Example text per token, for the settings help (translated).
QString tokenExample(const QString& token);

struct Preset {
    QString label, top, bottom, liveTop, liveBottom;
};
// Ready-made layouts for the countdown (settings dialog).
QList<Preset> presets();

using Fields = QMap<QString, QString>;

// Every template token's value, and whether a session is running.
std::pair<Fields, bool> fields(const Race* race, const QJsonObject& live, const Settings& s, qint64 now,
                               const Zone& zone = std::nullopt);
// {temp} {rain} {sky} {wx} {track_temp} for the current or next session.
Fields weatherFields(const Race* race, const QJsonObject& live, const weather::Forecast* forecast,
                     const Settings& s, qint64 now);
// Fill {tokens}; unknown tokens stay visible and doubled separators from empty tokens collapse.
QString renderTemplate(const QString& tpl, const Fields& f);

struct State {
    QString top, bottom;
    bool live = false;
    bool operator==(const State&) const = default;
};
State state(const Race* race, const QJsonObject& live, const Settings& s, qint64 now, const Zone& zone = std::nullopt,
            const weather::Forecast* forecast = nullptr);

// (label, big text, live?) for the schedule tab's countdown block.
std::tuple<QString, QString, bool> countdownState(const Race* race, const Settings& s, qint64 now);

}  // namespace f1::pill
