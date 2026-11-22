#include "theme.h"

#include <QApplication>
#include <QPalette>

#include "../paths.h"

#include <QDir>
#include <QFontDatabase>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace f1::ui {

QString loadFonts() {
    static QString family;
    if (!family.isEmpty()) return family;
    const int id = QFontDatabase::addApplicationFont(QDir(paths::resourceDir()).filePath(QStringLiteral("fonts/NotoSans.ttf")));
    const QStringList fams = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
    family = fams.isEmpty() ? QStringLiteral("Segoe UI") : fams.first();
    return family;
}

Theme Theme::fromSettings(const Settings& s) {
    Theme t;
    const QString accent = QColor::isValidColorName(s.accent) ? s.accent : QStringLiteral("#BCC2FF");
    t.accent = QColor::fromString(accent).name().toUpper();
    t.scale = s.ui_scale / 100.0;
    t.fontScale = s.font_scale / 100.0;
    t.family = loadFonts();
    t.mono = s.font_family == u"monospace";
    return t;
}

QString Theme::onAccent() const {
    return QColor::fromString(accent).lightnessF() > 0.55 ? QStringLiteral("#262C67") : QStringLiteral("#F4F4FF");
}

QFont Theme::font(double size, bool bold, std::optional<bool> monoOverride) const {
    const bool useMono = monoOverride.value_or(mono);
    QFont f(useMono ? QStringLiteral("Cascadia Mono") : family);
    if (useMono) {
        f.setStyleHint(QFont::Monospace);
        f.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("Courier New")});
    }
    f.setPixelSize(std::max(6, static_cast<int>(std::nearbyint(size * scale * fontScale))));
    f.setWeight(bold ? QFont::Bold : QFont::Normal);
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QIcon appIcon(const QString& accent, bool live) {
    // Tabler "flag" icon (MIT), stroked in the accent colour.
    const QString svg = QStringLiteral(
                            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" "
                            "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                            "<path d=\"M5 5a5 5 0 0 1 7 0a5 5 0 0 0 7 0v9a5 5 0 0 1 -7 0a5 5 0 0 0 -7 0v-9z\"/>"
                            "<path d=\"M5 21v-7\"/></svg>")
                            .arg(accent);
    QSvgRenderer renderer(svg.toUtf8());
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x1B, 0x1B, 0x29));
        p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.22, size * 0.22);
        const int g = qRound(size * 0.72);
        const double off = (size - g) / 2.0;
        renderer.render(&p, QRectF(qRound(off), qRound(off + size * 0.02), g, g));
        if (live) {
            p.setBrush(QColor::fromString(QLatin1StringView(LIGHT_ON)));
            const double r = size * 0.30;
            p.drawEllipse(QRectF(size - r - size * 0.04, size * 0.04, r, r));
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

// Fusion + the Noctalia palette: native Windows styling ignores parts of a dark stylesheet,
// Fusion follows it consistently (same as the Python app).
void applyDarkPalette() {
    QApplication::setStyle(QStringLiteral("Fusion"));
    QPalette pal;
    const std::pair<QPalette::ColorRole, const char*> colors[] = {
        {QPalette::Window, "#1B1B29"},     {QPalette::WindowText, "#E4E1EC"},     {QPalette::Base, "#24243A"},
        {QPalette::AlternateBase, "#2A2A40"}, {QPalette::Text, "#E4E1EC"},       {QPalette::Button, "#2A2A40"},
        {QPalette::ButtonText, "#E4E1EC"},  {QPalette::Highlight, "#BCC2FF"},    {QPalette::HighlightedText, "#262C67"},
        {QPalette::ToolTipBase, "#24243A"}, {QPalette::ToolTipText, "#E4E1EC"},  {QPalette::PlaceholderText, "#8F8F9E"},
        {QPalette::Link, "#BCC2FF"},        {QPalette::Mid, "#46464F"},          {QPalette::Dark, "#141420"}};
    for (const auto& [role, c] : colors) pal.setColor(role, QColor::fromString(QLatin1StringView(c)));
    for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        pal.setColor(QPalette::Disabled, role, QColor::fromString(QLatin1StringView("#6E6E7A")));
    QApplication::setPalette(pal);
}

}  // namespace f1::ui
