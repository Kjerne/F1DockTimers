// Renders the README screenshots: the panel tabs, the taskbar countdown styles and three Settings
// pages (port of the Python app's tools/readme_shots.py, same sizes and 1.5x scale). Everything is
// drawn by the app's own code, never captured from the desktop, so no wallpaper or personal windows
// end up in the images. Built with the app but never shipped.
//
//   set NOCTALIA_F1_HOME=<folder with config\ and cache\>   (optional: a copy of your data)
//   build\msvc\Release\readme_shots.exe docs\screenshots
#include "core/timeutil.h"
#include "core/tracks.h"
#include "paths.h"
#include "services/store.h"
#include "settings.h"
#include "ui/panel.h"
#include "ui/settings_dialog.h"
#include "ui/taskbar_pill.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTabWidget>
#include <QTimer>
#include <cstdio>

using namespace f1;

namespace {

void panelShots(ui::Ctx& ctx, const QDir& out) {
    ui::F1Panel p(&ctx, QStringLiteral("popup"));
    p.bgOpacity = 1.0;
    p.resize(500, 690);
    p.move(-10000, -10000);   // off screen: the mouse pointer never adds a hover highlight
    p.show();
    for (const char* tab : {"schedule", "weather", "grid", "live", "results", "drivers", "constructors"}) {
        const QString view = QString::fromLatin1(tab);
        if (!p.validViews(nowSec()).contains(view)) continue;   // e.g. no grid outside a race weekend
        p.setView(view, false);
        QApplication::processEvents();
        p.grab().save(out.filePath(QStringLiteral("panel-") + view + QStringLiteral(".png")));
    }
}

void pillShots(const Settings& settings, const DataStore& store, const ui::Theme& theme, const QDir& out) {
    // Three styles on a neutral, Windows 11 like taskbar strip.
    const double dpr = 2.0;
    const int w = 520, h = 3 * 52;
    QImage img(int(w * dpr), int(h * dpr), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(QColor(0x20, 0x20, 0x20));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    struct Variant {
        const char* label;
        std::function<void(Settings&)> apply;
    };
    const Variant styles[] = {
        {"Noctalia", [](Settings& s) { s.pill_style = QStringLiteral("noctalia"); s.pill_radius = 7; }},
        {"Match taskbar", [](Settings& s) { s.pill_style = QStringLiteral("taskbar"); s.pill_radius = 4; }},
        {"Custom box", [](Settings& s) {
             s.pill_style = QStringLiteral("custom");
             s.pill_radius = 0;
             s.pill_bg = QStringLiteral("#E10600");
             s.pill_bg_opacity = 100;
             s.pill_text = s.pill_text2 = QStringLiteral("#FFFFFF");
             s.pill_border = false;
         }},
    };
    QFont tray(QStringLiteral("Segoe UI"));
    tray.setPixelSize(12);
    for (int i = 0; i < 3; ++i) {
        Settings s = settings;
        styles[i].apply(s);
        const int y = i * 52;
        p.fillRect(QRectF(0, y, w, 48), QColor(0x20, 0x20, 0x20));
        p.fillRect(QRectF(0, y + 48, w, 4), QColor(0x15, 0x15, 0x15));
        const auto st = pill::state(store.race(), store.get(QStringLiteral("live")), s, nowSec(), zoneFor(s.timezone));
        const auto style = ui::PillStyle::resolve(s, theme);
        ui::paintPill(p, QRectF(w - 330, y + 9, 200, 30), style, st.top, st.bottom, st.live, false, 11, true,
                      s.pill_radius ? 9 : 6);
        p.setFont(tray);
        p.setPen(Qt::white);
        p.drawText(QRectF(w - 120, y, 110, 48), Qt::AlignCenter, QStringLiteral("^  ENG   19:10"));
        p.setPen(QColor(0x8F, 0x8F, 0x9E));
        p.drawText(QRectF(12, y, 150, 48), Qt::AlignVCenter, QString::fromLatin1(styles[i].label));
    }
    p.end();
    img.save(out.filePath(QStringLiteral("taskbar-styles.png")));
}

void settingsShots(Settings& settings, DataStore& store, const ui::Theme& theme, const QDir& out) {
    ui::SettingsDialog d(&settings, &store, theme, [] {});   // never saves
    d.resize(560, 700);
    d.move(-10000, -10000);
    d.show();
    auto* tabs = d.findChild<QTabWidget*>();
    const std::pair<int, const char*> pages[] = {{2, "settings-taskbar.png"}, {3, "settings-f1.png"}, {4, "settings-live.png"}};
    for (const auto& [index, name] : pages) {
        tabs->setCurrentIndex(index);
        QApplication::processEvents();
        d.grab().save(out.filePath(QString::fromLatin1(name)));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_SCALE_FACTOR", "1.5");
    QApplication app(argc, argv);
    ui::applyDarkPalette();
    const QDir out(argc > 1 && argv[1][0] != '-' ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("docs/screenshots"));
    QDir().mkpath(out.path());

    Settings settings = Settings::load(paths::settingsFile());
    settings.popup_opacity = 100;
    settings.animations = false;
    settings.spoiler_free = false;   // show the real tabs in the README
    settings.grid_official = true;
    const QString cache = QDir(paths::cacheDir()).filePath(QStringLiteral("cache.json"));
    DataStore store(cache, QString());   // read only
    store.loadCache();
    ui::Theme theme = ui::Theme::fromSettings(settings);
    theme.family = ui::loadFonts();
    TrackLibrary tracks;
    tracks.load(QDir(paths::resourceDir()).filePath(QStringLiteral("tracks.txt")));
    ui::Ctx ctx{&store, &settings, &theme, &tracks, [](const QString&) {}};
    // Optional: preselect session pills, e.g. --pick live=grid --pick last=<session key> (checking only).
    for (int i = 1; i + 1 < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == u"--pick") {
            const QString kv = QString::fromLocal8Bit(argv[i + 1]);
            ctx.picks.insert(kv.section(u'=', 0, 0), kv.section(u'=', 1));
        }

    QTimer::singleShot(300, &app, [&] {
        panelShots(ctx, out);
        pillShots(settings, store, theme, out);
        settingsShots(settings, store, theme, out);
        for (const QString& f : out.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name))
            std::printf("%s\n", qPrintable(f));
        app.quit();
    });
    return QApplication::exec();
}
