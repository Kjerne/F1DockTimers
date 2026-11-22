#pragma once
// Persistent settings: the same JSON file and keys as the Python app
// (%APPDATA%\F1DockTimers\settings.json). Never holds secrets: the OpenF1 password lives in
// Windows Credential Manager; only the username is stored here.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace f1 {

// X(type, name, default). Order and defaults match f1win/settings.py.
#define F1_SETTINGS(X)                                                        \
    /* General */                                                             \
    X(bool, start_with_windows, false)                                        \
    X(bool, start_minimized, true)                                            \
    X(bool, widget_enabled, false)                                            \
    X(bool, taskbar_pill, true)                                               \
    X(int, taskbar_pill_offset, 0)                                            \
    X(bool, pill_embed, false)                                                \
    X(bool, pill_auto_dock, true)                                             \
    X(QString, pill_style, QStringLiteral("noctalia"))                        \
    X(int, pill_radius, 7)                                                    \
    X(int, pill_width, 0)                                                     \
    X(int, pill_height, 0)                                                    \
    X(double, pill_font_px, 11.0)                                             \
    X(bool, pill_autofit, true)                                               \
    X(bool, pill_bold_top, true)                                              \
    X(QString, pill_align, QStringLiteral("left"))                            \
    X(int, pill_lines, 2)                                                     \
    X(bool, pill_border, true)                                                \
    X(QString, pill_bg, QStringLiteral("#1B1B29"))                            \
    X(int, pill_bg_opacity, 90)                                               \
    X(QString, pill_border_color, QStringLiteral("#46464F"))                  \
    X(QString, pill_text, QStringLiteral("#E4E1EC"))                          \
    X(QString, pill_text2, QStringLiteral("#BCC2FF"))                         \
    X(QString, pill_live_color, QStringLiteral("#BCC2FF"))                    \
    X(QString, pill_top, QStringLiteral("{round} · {race} · {session}"))           \
    X(QString, pill_bottom, QStringLiteral("{time} · {countdown}"))                     \
    X(QString, pill_live_top, QStringLiteral("{round} · {race} · ● {session}")) \
    X(QString, pill_live_bottom, QStringLiteral("{live}"))                    \
    X(QString, default_tab, QStringLiteral("schedule"))                       \
    X(QString, time_format, QStringLiteral("24h"))                            \
    X(QString, timezone, QStringLiteral("auto"))                              \
    X(QString, language, QStringLiteral("auto"))                              \
    X(QString, units, QStringLiteral("metric"))                               \
    X(QString, weather_refresh, QStringLiteral("normal"))                     \
    X(QJsonArray, weather_custom, (QJsonArray{15, 60, 180}))                  \
    X(bool, grid_official, true)                                              \
    X(double, schedule_refresh_h, 6.0)                                        \
    X(double, standings_refresh_h, 3.0)                                       \
    /* Appearance */                                                          \
    X(QString, accent, QStringLiteral("#BCC2FF"))                             \
    X(QString, font_family, QStringLiteral("Noto Sans"))                      \
    X(int, font_scale, 100)                                                   \
    X(int, ui_scale, 100)                                                     \
    X(int, popup_opacity, 96)                                                 \
    X(int, widget_opacity, 92)                                                \
    X(bool, animations, true)                                                 \
    X(bool, widget_always_on_top, false)                                      \
    X(bool, widget_click_through, false)                                      \
    X(bool, widget_remember_position, true)                                   \
    X(bool, widget_locked, false)                                             \
    X(QJsonArray, widget_geometry, QJsonArray())                              \
    X(QString, widget_double_click, QStringLiteral("refresh"))                \
    X(bool, popup_pinned, false)                                              \
    /* Formula 1 */                                                           \
    X(QString, countdown_target, QStringLiteral("race"))                      \
    X(QString, favorite_driver, QString())                                    \
    X(QString, favorite_team, QString())                                      \
    X(bool, team_colors, true)                                                \
    X(bool, notify_race, false)                                               \
    X(bool, notify_quali, false)                                              \
    X(bool, notify_sprint, false)                                             \
    X(int, notify_lead_min, 10)                                               \
    X(bool, notify_results, true)                                             \
    X(bool, spoiler_free, false)                                              \
    X(double, spoiler_revealed_at, 0.0)                                       \
    X(QJsonObject, session_offsets, QJsonObject())                            \
    X(QString, live_source, QStringLiteral("openf1"))                         \
    X(QString, openf1_plan, QStringLiteral("free"))                           \
    X(QString, openf1_username, QString())                                    \
    X(bool, debug_force_tabs, false)                                          \
    /* Updates */                                                             \
    X(bool, check_updates, true)                                              \
    X(double, update_checked_at, 0.0)                                         \
    X(QString, update_notified, QString())                                    \
    X(QString, skipped_version, QString())

struct Settings {
#define F1_FIELD(type, name, def) type name = def;
    F1_SETTINGS(F1_FIELD)
#undef F1_FIELD

    // Type-checked load like Settings.load(): wrong types keep the default, then clamp().
    static Settings fromJson(const QJsonObject& data);
    static Settings load(const QString& file);
    QJsonObject toJson() const;
    // Atomic write (QSaveFile). Not used by the app yet: phase 1 is read-only.
    bool save(const QString& file) const;
    void clamp();

    // Not carried between PCs: window positions depend on the monitors there.
    static const QStringList& machineSpecific();
    // Write a shareable copy. Never contains secrets (the password is in Credential Manager).
    bool exportTo(const QString& file, QString* error = nullptr) const;
    // Read an exported file (or a plain settings.json) onto these settings, validated like a normal
    // load. Returns the names of the fields that changed; on a bad file returns nullopt and `error`.
    std::optional<QStringList> importFrom(const QString& file, QString* error);
};

}  // namespace f1
