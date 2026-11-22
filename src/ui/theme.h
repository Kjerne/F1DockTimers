#pragma once
// Noctalia palette and metrics (port of ui/theme.py) plus the tray / window icon (ui/widgets.py).

#include "../settings.h"

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>
#include <algorithm>
#include <cmath>
#include <optional>

namespace f1::ui {

inline constexpr const char* LIGHT_ON = "#E10600";
inline constexpr const char* LIGHT_GO = "#1FBF5B";
inline constexpr const char* LIGHT_OFF = "#3A3F44";

struct Theme {
    QString accent = QStringLiteral("#BCC2FF");
    QString surface = QStringLiteral("#1B1B29");
    QString onSurface = QStringLiteral("#E4E1EC");
    QString muted = QStringLiteral("#C6C5D0");
    QString secondary = QStringLiteral("#C4C4DD");
    QString outline = QStringLiteral("#46464F");
    QString error = QStringLiteral("#FFB4AB");
    double scale = 1.0;
    double fontScale = 1.0;
    QString family = QStringLiteral("Noto Sans");
    bool mono = false;

    static Theme fromSettings(const Settings& s);
    QString onAccent() const;   // dark text on light accents, light text otherwise
    int px(double v) const { return std::max(1, static_cast<int>(std::nearbyint(v * scale))); }
    QFont font(double size, bool bold = false, std::optional<bool> monoOverride = std::nullopt) const;
};

// Register the bundled Noto Sans (OFL). Returns the family to use.
QString loadFonts();
// Fusion style with the Noctalia colours, for the whole app (also used by tools/readme_shots).
void applyDarkPalette();

// The flag glyph on a dark rounded square; `live` adds a red dot.
QIcon appIcon(const QString& accent = QStringLiteral("#BCC2FF"), bool live = false);

}  // namespace f1::ui
