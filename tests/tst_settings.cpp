// Settings file compatibility with the Python app (reference-python/f1win/settings.py).
#include "common.h"
#include "paths.h"
#include "services/autostart.h"
#include "settings.h"

#include <QTemporaryDir>

using namespace f1;

class TestSettings : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void defaultsMatchPython() {
        const Settings s;
        QVERIFY(s.taskbar_pill);
        QCOMPARE(s.pill_top, QStringLiteral("{round} · {race} · {session}"));
        QCOMPARE(s.weather_custom, (QJsonArray{15, 60, 180}));
        QCOMPARE(s.toJson().size(), 71);   // every field of the Python dataclass
    }
    void typeChecksAndClamping() {
        const QJsonObject raw{{"time_format", "13h"},        {"taskbar_pill", 1},          // int is not a bool
                              {"pill_radius", 99},           {"notify_lead_min", 7.9},     // float into int truncates
                              {"schedule_refresh_h", 12},    {"pill_lines", 5},
                              {"favorite_driver", " ver "},  {"weather_custom", QJsonArray{1, "9000", 30}},
                              {"pill_top", QString(200, u'x')}, {"unknown_key", true},
                              {"session_offsets", QJsonObject{{"round_key", "2026-21"}, {"quali", 15}}}};
        const Settings s = Settings::fromJson(raw);
        QCOMPARE(s.time_format, QStringLiteral("24h"));
        QVERIFY(s.taskbar_pill);
        QCOMPARE(s.pill_radius, 30);
        QCOMPARE(s.notify_lead_min, 7);
        QCOMPARE(s.schedule_refresh_h, 12.0);
        QCOMPARE(s.pill_lines, 2);
        QCOMPARE(s.favorite_driver, QStringLiteral("VER"));
        QCOMPARE(s.weather_custom, (QJsonArray{5, 1440, 30}));
        QCOMPARE(s.pill_top.size(), 120);
        QCOMPARE(s.session_offsets["quali"_L1].toInt(), 15);
    }
    void roundTrip() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.json"));
        Settings a;
        a.accent = QStringLiteral("#FF0000");
        a.pill_font_px = 12.5;
        QVERIFY(a.save(file));
        const Settings b = Settings::load(file);
        QCOMPARE(b.accent, QStringLiteral("#FF0000"));
        QCOMPARE(b.pill_font_px, 12.5);
        QCOMPARE(Settings::load(dir.filePath(QStringLiteral("missing.json"))).accent, QStringLiteral("#BCC2FF"));
    }
    // test_persistence.py::test_settings_export_import
    void exportImport() {
        QTemporaryDir dir;
        Settings a;
        a.pill_style = QStringLiteral("custom");
        a.pill_radius = 0;
        a.favorite_driver = QStringLiteral("NOR");
        a.widget_geometry = QJsonArray{1, 2, 400, 500};
        a.openf1_username = QStringLiteral("me@example.com");
        a.start_with_windows = true;
        const QString out = dir.filePath(QStringLiteral("export.json"));
        QVERIFY(a.exportTo(out));
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(!text.contains("widget_geometry") && !text.contains("start_with_windows"));
        QVERIFY(!text.toLower().contains("password"));
        Settings b;
        b.widget_geometry = QJsonArray{9, 9, 300, 300};
        QString err;
        const auto changed = b.importFrom(out, &err);
        QVERIFY(changed.has_value());
        QCOMPARE(b.pill_style, QStringLiteral("custom"));
        QCOMPARE(b.pill_radius, 0);
        QCOMPARE(b.favorite_driver, QStringLiteral("NOR"));
        QCOMPARE(b.widget_geometry, (QJsonArray{9, 9, 300, 300}));
        QVERIFY(!b.start_with_windows);
        QVERIFY(changed->contains(QStringLiteral("pill_style")));
    }
    // test_persistence.py::test_settings_import_rejects_bad_files
    void importRejectsBadFiles() {
        QTemporaryDir dir;
        auto write = [&](const char* name, const QByteArray& body) {
            QFile f(dir.filePath(QString::fromLatin1(name)));
            f.open(QIODevice::WriteOnly);
            f.write(body);
            return f.fileName();
        };
        QString err;
        QVERIFY(!Settings().importFrom(write("bad.json", "{not json"), &err));
        QVERIFY(!Settings().importFrom(write("other.json", R"({"app": "Something", "settings": {}})"), &err));
        Settings s;
        QVERIFY(s.importFrom(write("h.json", R"({"app": "NoctaliaF1", "settings": {"ui_scale": 99999, "time_format": 5, "evil": 1}})"), &err));
        QCOMPARE(s.ui_scale, 200);
        QCOMPARE(s.time_format, QStringLiteral("24h"));
        QVERIFY(!s.toJson().contains(QStringLiteral("evil")));
    }
    void preRenameFolderIsMigrated() {
        // Like the Python app: settings and cache in a "NoctaliaF1" folder move to F1DockTimers once.
        QTemporaryDir tmp;
        const QByteArray home = qgetenv("NOCTALIA_F1_HOME"), appdata = qgetenv("APPDATA");
        qunsetenv("NOCTALIA_F1_HOME");
        qputenv("APPDATA", QDir::toNativeSeparators(tmp.path()).toUtf8());
        QDir().mkpath(tmp.filePath(QStringLiteral("NoctaliaF1/lang")));
        QFile old(tmp.filePath(QStringLiteral("NoctaliaF1/settings.json")));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write(R"({"favorite_driver": "NOR"})");
        old.close();
        QFile(tmp.filePath(QStringLiteral("NoctaliaF1/lang/xx.txt"))).open(QIODevice::WriteOnly);
        const QString file = paths::settingsFile();
        QVERIFY(file.contains(QStringLiteral("F1DockTimers")));
        QCOMPARE(Settings::load(file).favorite_driver, QStringLiteral("NOR"));
        QVERIFY(QFile::exists(tmp.filePath(QStringLiteral("F1DockTimers/lang/xx.txt"))));
        // Only once: a later run does not copy the old folder again.
        QFile::remove(tmp.filePath(QStringLiteral("F1DockTimers/lang/xx.txt")));
        paths::configDir();
        QVERIFY(!QFile::exists(tmp.filePath(QStringLiteral("F1DockTimers/lang/xx.txt"))));
        qputenv("APPDATA", appdata);
        qputenv("NOCTALIA_F1_HOME", home);
    }
    void autostartCommandMatchesTheInstaller() {
        // installer.iss registers "{app}\F1DockTimers.exe" --minimized; the app writes the same.
        const QString expected = QStringLiteral("\"C:\\Programs\\F1DockTimers\\F1DockTimers.exe\" --minimized");
        QCOMPARE(autostart::command(QStringLiteral("C:/Programs/F1DockTimers/F1DockTimers.exe")), expected);
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
