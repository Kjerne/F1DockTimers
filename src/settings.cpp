#include "settings.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

namespace f1 {

namespace {

// isinstance checks from Settings.load(); bools are never accepted as numbers.
void read(const QJsonValue& v, bool& out) {
    if (v.isBool()) out = v.toBool();
}
void read(const QJsonValue& v, int& out) {
    if (v.isDouble()) out = static_cast<int>(std::clamp(v.toDouble(), -2147483648.0, 2147483647.0));
}
void read(const QJsonValue& v, double& out) {
    if (v.isDouble()) out = v.toDouble();
}
void read(const QJsonValue& v, QString& out) {
    if (v.isString()) out = v.toString();
}
void read(const QJsonValue& v, QJsonArray& out) {
    if (v.isArray()) out = v.toArray();
}
void read(const QJsonValue& v, QJsonObject& out) {
    if (v.isObject()) out = v.toObject();
}

template <typename T>
T clampv(T v, T lo, T hi) { return std::max(lo, std::min(hi, v)); }

bool oneOf(const QString& v, std::initializer_list<const char*> options) {
    for (const char* o : options)
        if (v == QLatin1StringView(o)) return true;
    return false;
}

}  // namespace

Settings Settings::fromJson(const QJsonObject& data) {
    Settings s;
#define F1_READ(type, name, def)                                   \
    if (data.contains(QStringLiteral(#name))) read(data.value(QStringLiteral(#name)), s.name);
    F1_SETTINGS(F1_READ)
#undef F1_READ
    s.clamp();
    return s;
}

Settings Settings::load(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return Settings();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return Settings();
    return fromJson(doc.object());
}

QJsonObject Settings::toJson() const {
    QJsonObject o;
#define F1_WRITE(type, name, def) o.insert(QStringLiteral(#name), name);
    F1_SETTINGS(F1_WRITE)
#undef F1_WRITE
    return o;
}

bool Settings::save(const QString& file) const {
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return f.commit();
}

void Settings::clamp() {
    if (!oneOf(time_format, {"24h", "12h"})) time_format = QStringLiteral("24h");
    if (!oneOf(default_tab, {"schedule", "drivers", "constructors"})) default_tab = QStringLiteral("schedule");
    if (!oneOf(countdown_target, {"race", "session"})) countdown_target = QStringLiteral("race");
    if (!oneOf(openf1_plan, {"free", "sponsor"})) openf1_plan = QStringLiteral("free");
    if (!oneOf(live_source, {"openf1", "f1_feed"})) live_source = QStringLiteral("openf1");
    if (!oneOf(widget_double_click, {"refresh", "panel", "settings", "none"}))
        widget_double_click = QStringLiteral("refresh");
    notify_lead_min = clampv(notify_lead_min, 0, 120);
    taskbar_pill_offset = clampv(taskbar_pill_offset, -400, 1600);
    {   // custom weather timings: 5 min .. 24 h, sane even if hand-edited
        bool ok = weather_custom.size() >= 3;
        QJsonArray fixed;
        for (int i = 0; ok && i < 3; ++i) {
            const QJsonValue x = weather_custom.at(i);
            if (x.isDouble()) fixed.append(clampv(static_cast<int>(x.toDouble()), 5, 1440));
            else if (x.isString()) {
                bool okInt = false;
                const int n = x.toString().trimmed().toInt(&okInt);
                if (okInt) fixed.append(clampv(n, 5, 1440));
                else ok = false;
            } else if (x.isBool()) fixed.append(clampv(x.toBool() ? 1 : 0, 5, 1440));
            else ok = false;
        }
        weather_custom = ok ? fixed : QJsonArray{15, 60, 180};
    }
    if (!oneOf(pill_style, {"noctalia", "taskbar", "custom"})) pill_style = QStringLiteral("noctalia");
    if (!oneOf(pill_align, {"left", "center"})) pill_align = QStringLiteral("left");
    pill_lines = pill_lines == 1 ? 1 : 2;
    pill_radius = clampv(pill_radius, 0, 30);
    pill_width = clampv(pill_width, 0, 900);
    pill_height = clampv(pill_height, -1, 80);
    pill_font_px = clampv(pill_font_px, 7.0, 28.0);
    pill_bg_opacity = clampv(pill_bg_opacity, 0, 100);
    for (QString* t : {&pill_top, &pill_bottom, &pill_live_top, &pill_live_bottom}) *t = t->left(120);
    font_scale = clampv(font_scale, 70, 160);
    ui_scale = clampv(ui_scale, 70, 200);
    popup_opacity = clampv(popup_opacity, 40, 100);
    widget_opacity = clampv(widget_opacity, 0, 100);
    schedule_refresh_h = clampv(schedule_refresh_h, 0.5, 48.0);
    standings_refresh_h = clampv(standings_refresh_h, 0.5, 48.0);
    favorite_driver = favorite_driver.trimmed().toUpper().left(4);
    favorite_team = favorite_team.trimmed().toLower().left(32);
}

}  // namespace f1

namespace f1 {

const QStringList& Settings::machineSpecific() {
    static const QStringList k = {QStringLiteral("widget_geometry"), QStringLiteral("start_with_windows"),
                                  QStringLiteral("update_checked_at"), QStringLiteral("update_notified"),
                                  QStringLiteral("skipped_version"), QStringLiteral("spoiler_revealed_at")};
    return k;
}

bool Settings::exportTo(const QString& file, QString* error) const {
    QJsonObject data = toJson();
    for (const QString& k : machineSpecific()) data.remove(k);
    const QJsonObject payload{{QStringLiteral("app"), QStringLiteral("F1DockTimers")}, {QStringLiteral("kind"), QStringLiteral("settings")},
                              {QStringLiteral("version"), 1}, {QStringLiteral("settings"), data}};
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(payload).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

std::optional<QStringList> Settings::importFrom(const QString& file, QString* error) {
    QFile f(file);
    QJsonParseError perr{};
    const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &perr) : QJsonDocument();
    if (!doc.isObject()) {
        *error = QStringLiteral("Not a readable settings file.");
        return std::nullopt;
    }
    const QJsonObject payload = doc.object();
    const QString app = payload.value(QStringLiteral("app")).toString();
    QJsonObject data;
    bool ok = false;
    if (app == u"F1DockTimers" || app == u"NoctaliaF1") {
        ok = payload.value(QStringLiteral("settings")).isObject();
        data = payload.value(QStringLiteral("settings")).toObject();
    } else {
        // A plain settings.json copied from another PC or folder also works.
        int known = 0;
        const QJsonObject defaults = Settings().toJson();
        for (auto it = payload.begin(); it != payload.end(); ++it) known += defaults.contains(it.key()) ? 1 : 0;
        if (known >= 5) {
            ok = true;
            data = payload;
        }
    }
    if (!ok) {
        *error = QStringLiteral("This file is not an F1DockTimers settings export.");
        return std::nullopt;
    }
    for (const QString& k : machineSpecific()) data.remove(k);
    const QJsonObject incoming = Settings::fromJson(data).toJson();   // same type checks and clamping as a load
    QJsonObject mine = toJson();
    QStringList changed;
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (!mine.contains(it.key())) continue;
        const QJsonValue v = incoming.value(it.key());
        if (mine.value(it.key()) != v) {
            mine.insert(it.key(), v);
            changed << it.key();
        }
    }
    QJsonObject keep = mine;
    *this = Settings::fromJson(keep);
    return changed;
}

}  // namespace f1
