// Port of reference-python/tests/test_ui.py (popup placement, panel rendering, popup dismiss) and
// the circuit-mapping part of test_persistence.py.
#include "common.h"
#include "core/tracks.h"
#include "ui/desktop_widget.h"
#include "ui/popup.h"
#include "ui/tabs/schedule.h"
#include "ui/tabs/weather.h"
#include "ui/settings_dialog.h"
#include "ui/tabs/live.h"
#include "ui/tabs/tables.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace f1;
using namespace f1::ui;

namespace {
const QSize SIZE(500, 640);

bool inside(const QPoint& pos, const QSize& size, const QRect& avail) {
    const QRect r(pos, size);
    return r.intersected(avail) == r;
}

QString tracksFile() { return QStringLiteral(F1_FIXTURES) + QStringLiteral("../../assets/tracks.txt"); }

QJsonObject loaded(QJsonObject o) {
    o["loaded"_L1] = true;
    return o;
}

// A store with the fixture weekend plus a forecast and measured weather for it.
struct Fixture {
    QTemporaryDir dir;
    DataStore store{QString(), QString()};
    Settings settings;
    Theme theme;
    TrackLibrary tracks;
    Ctx ctx{&store, &settings, &theme, &tracks, [](const QString&) {}};
    Fixture() {
        settings.animations = false;
        settings.timezone = QStringLiteral("Europe/Copenhagen");
        theme.family = loadFonts();
        tracks.load(tracksFile());
        Race race = t::nextRace();
        store.setRace(race);
        store.set(QStringLiteral("drivers"), loaded(parsers::parseDriverStandings(t::fx("driver_standings.json"))));
        QJsonArray times, temp, prob, code;
        const qint64 first = race.sessions.first().ts - 3 * 3600;
        for (int i = 0; i < 80; ++i) {
            times.append(first + i * 3600);
            temp.append(10 + (i % 6));
            prob.append(i % 7 == 0 ? 60 : 10);
            code.append(i % 7 == 0 ? 61 : 2);
        }
        auto fc = weather::parseOpenMeteo(QJsonObject{{"hourly", QJsonObject{{"time", times}, {"temperature_2m", temp},
                                                                             {"precipitation_probability", prob},
                                                                             {"weather_code", code}}}});
        fc->round = QStringLiteral("2026-21");
        store.set(QStringLiteral("weather"), loaded(fc->toJson()));
        store.set(QStringLiteral("measured_weather"),
                  loaded({{"round", "2026-21"},
                          {"sessions", QJsonObject{{"fp1", QJsonObject{{"air_min", 11.6}, {"air_max", 13.0}, {"rain", true}}}}}}));
    }
};

template <typename W>
QList<W*> kids(QWidget* root) {
    return root->findChildren<W*>();
}

QStringList texts(QWidget* root) {
    QStringList out;
    for (Text* t : kids<Text>(root)) out << t->text();
    return out;
}
}  // namespace

class TestUi : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void widgetRestore() {
        const QList<QRect> screens{QRect(0, 0, 1920, 1040), QRect(1920, 0, 2560, 1400)};
        QCOMPARE(restoreRect(QJsonArray{2000, 100, 420, 560}, screens), QRect(2000, 100, 420, 560));
        QVERIFY(!restoreRect(QJsonArray{9000, 100, 420, 560}, screens));   // monitor unplugged
        QVERIFY(restoreRect(QJsonArray{10, 10, 50, 50}, screens)->width() >= 300);   // minimum size
        QVERIFY(!restoreRect(QJsonValue(QStringLiteral("junk")), screens));
    }
    void desktopWidgetFlagsAndGeometry() {
        Fixture f;
        f.settings.widget_geometry = QJsonArray{40, 50, 420, 560};
        f.settings.widget_always_on_top = true;
        DesktopWidget w(&f.ctx);
        QVERIFY(w.windowFlags() & Qt::WindowStaysOnTopHint);
        QVERIFY(!(w.windowFlags() & Qt::WindowTransparentForInput));
        f.settings.widget_click_through = true;
        w.applySettings();
        QVERIFY(w.windowFlags() & Qt::WindowTransparentForInput);
        w.show();
        for (const char* tab : {"schedule", "weather", "drivers"}) {
            w.panel->setView(QString::fromLatin1(tab), false);
            QCoreApplication::processEvents();
            QVERIFY(!w.grab().isNull());
        }
    }
    void bottomTaskbarAtEachScale_data() {
        QTest::addColumn<double>("scale");
        QTest::newRow("100") << 1.0;
        QTest::newRow("125") << 1.25;
        QTest::newRow("150") << 1.5;
    }
    void bottomTaskbarAtEachScale() {
        QFETCH(double, scale);
        const QRect full(0, 0, qRound(2560 / scale), qRound(1440 / scale));
        const QRect avail(0, 0, full.width(), full.height() - qRound(48 / scale));
        const QRect icon(full.width() - 200, avail.bottom() + 8, 24, 24);
        const auto [pos, edge] = anchorPosition(icon, avail, full, SIZE);
        QCOMPARE(edge, QStringLiteral("bottom"));
        QVERIFY(inside(pos, SIZE, avail));
        QVERIFY(pos.y() + SIZE.height() <= avail.bottom());
    }
    void topLeftRightTaskbars() {
        const QRect full(0, 0, 1920, 1080);
        const std::tuple<QRect, QRect, QString> cases[] = {
            {QRect(0, 48, 1920, 1032), QRect(1800, 10, 24, 24), QStringLiteral("top")},
            {QRect(60, 0, 1860, 1080), QRect(10, 900, 24, 24), QStringLiteral("left")},
            {QRect(0, 0, 1860, 1080), QRect(1880, 900, 24, 24), QStringLiteral("right")}};
        for (const auto& [avail, icon, want] : cases) {
            const auto [pos, edge] = anchorPosition(icon, avail, full, SIZE);
            QCOMPARE(edge, want);
            QVERIFY(inside(pos, SIZE, avail));
        }
    }
    void secondMonitorWithNegativeCoordinates() {
        const QRect full(-1920, 0, 1920, 1080), avail(-1920, 0, 1920, 1032);
        const auto [pos, edge] = anchorPosition(QRect(-30, 1040, 24, 24), avail, full, SIZE);
        QVERIFY(inside(pos, SIZE, avail));
        QVERIFY(pos.x() < 0);
    }
    void iconAtScreenCornerIsClamped() {
        const QRect full(0, 0, 1366, 768), avail(0, 0, 1366, 728);
        QVERIFY(inside(anchorPosition(QRect(1360, 740, 24, 24), avail, full, SIZE).first, SIZE, avail));
        QVERIFY(inside(anchorPosition(QRect(), avail, full, SIZE).first, SIZE, avail));   // unknown icon rect
    }

    void tabListAndPickView() {
        Fixture f;
        const Race* r = f.store.race();
        const qint64 fp1 = t::ts(*r, "fp1"), q = t::ts(*r, "quali");
        QStringList ids;
        for (const auto& e : tabList(&f.store, f.settings, fp1 - 3600)) ids << e.id;
        QCOMPARE(ids, (QStringList{"schedule", "weather", "live", "results", "drivers", "constructors"}));
        ids.clear();
        for (const auto& e : tabList(&f.store, f.settings, q + 2 * 3600)) ids << e.id;
        QVERIFY(ids.contains(QStringLiteral("grid")));
        QCOMPARE(pickView(&f.store, f.settings, q + 2 * 3600), QStringLiteral("grid"));
        QCOMPARE(pickView(&f.store, f.settings, fp1 - 3600), QStringLiteral("schedule"));
        f.settings.default_tab = QStringLiteral("drivers");
        QCOMPARE(pickView(&f.store, f.settings, fp1 - 3600), QStringLiteral("drivers"));
    }

    void panelRendersAllTabsWithoutErrors_data() {
        QTest::addColumn<int>("uiScale");
        QTest::newRow("100") << 100;
        QTest::newRow("150") << 150;
    }
    void panelRendersAllTabsWithoutErrors() {
        QFETCH(int, uiScale);
        Fixture f;
        f.settings.ui_scale = uiScale;
        f.settings.debug_force_tabs = true;
        f.theme.scale = uiScale / 100.0;
        F1Panel p(&f.ctx, QStringLiteral("popup"));
        p.resize(qRound(500 * uiScale / 100.0), qRound(640 * uiScale / 100.0));
        p.show();
        for (const char* tab : {"schedule", "weather", "grid", "live", "results", "drivers", "constructors"}) {
            p.setView(QString::fromLatin1(tab), false);
            QApplication::processEvents();
            QCOMPARE(p.view, QString::fromLatin1(tab));
            QVERIFY(!p.grab().isNull());
            // Content must fit the panel width: no horizontal scrolling.
            QVERIFY(p.scroll->widget()->minimumSizeHint().width() <= p.scroll->viewport()->width() + 1);
        }
        p.cycle(1);
        QCOMPARE(p.view, QStringLiteral("schedule"));
    }

    void scheduleTabShowsSessionsWeatherAndMeasured() {
        Fixture f;
        ScheduleTab tab(&f.ctx);
        const QStringList all = texts(&tab);
        for (const char* s : {"Practice 1", "Practice 2", "Practice 3", "Qualifying", "Race", "SESSIONS", "SKY", "TEMP", "RAIN"})
            QVERIFY2(all.contains(QString::fromLatin1(s)), s);
        QVERIFY(all.contains(QStringLiteral("Fri 01:30")));          // Copenhagen time, as in the screenshot
        QVERIFY(all.contains(QStringLiteral("Rained")));             // measured FP1
        QVERIFY(all.contains(QStringLiteral("12–13°")));   // measured air range, banker's rounding
        QVERIFY(all.contains(QStringLiteral("Las Vegas, USA")));
        QCOMPARE(kids<TrackView>(&tab).size(), 1);
        QCOMPARE(kids<LightsGantry>(&tab).size(), 1);
    }

    void scheduleTabWithoutRace() {
        Fixture f;
        f.store.setRace(std::nullopt);
        f.store.setStatus(false);
        ScheduleTab tab(&f.ctx);
        QVERIFY(kids<Wrap>(&tab).first()->text() == u"No upcoming race");
    }

    void weatherTabOutlookAndSessions() {
        Fixture f;
        WeatherTab tab(&f.ctx);
        const QStringList all = texts(&tab);
        QVERIFY(std::any_of(all.begin(), all.end(), [](const QString& s) { return s.startsWith(QStringLiteral("ALL SESSIONS · LAS VEGAS")); }));
        QVERIFY(all.contains(QStringLiteral("HOUR")));
        bool credit = false;
        for (Wrap* w : kids<Wrap>(&tab)) credit |= w->text() == u"Weather data by Open-Meteo.com (CC BY 4.0)";
        QVERIFY(credit);
    }

    void weatherTabWithoutForecast() {
        Fixture f;
        f.store.set(QStringLiteral("weather"), DataStore::empty(QStringLiteral("weather")));
        WeatherTab tab(&f.ctx);
        const QStringList all = texts(&tab);
        QVERIFY(all.contains(QStringLiteral("FORECAST · LAS VEGAS")));
    }

    void popupHidesWhenFocusLeaves() {
        Fixture f;
        f.settings.popup_pinned = false;
        TrayPopup popup(&f.ctx, &f.settings);
        popup.show();
        QEvent e(QEvent::ActivationChange);
        QApplication::sendEvent(&popup, &e);   // not the active window -> must hide, not raise
        QVERIFY(!popup.isVisible());
        popup.panel->setPinned(true);
        popup.show();
        QApplication::sendEvent(&popup, &e);
        QVERIFY(popup.isVisible());
    }

    void tableTabsShowTheirData() {
        Fixture f;
        f.store.set(QStringLiteral("constructors"), loaded(parsers::parseConstructorStandings(t::fx("constructor_standings.json"))));
        f.store.set(QStringLiteral("results"), loaded(parsers::parseResults(t::fx("results.json"))));
        {
            DriversTab tab(&f.ctx);
            const QStringList all = texts(&tab);
            QVERIFY(all.contains(QStringLiteral("DRIVER STANDINGS")) && all.contains(QStringLiteral("ANT")) && all.contains(QStringLiteral("390")));
            QVERIFY(all.contains(QStringLiteral("9W")));
        }
        {
            ConstructorsTab tab(&f.ctx);
            QVERIFY(texts(&tab).contains(QStringLiteral("Mercedes")));
        }
        {
            ResultsTab tab(&f.ctx);
            const QStringList all = texts(&tab);
            QVERIFY(all.contains(QStringLiteral("LAST RACE · BRAZILIAN GRAND PRIX")));
            QVERIFY(all.contains(QStringLiteral("+1 Lap")) && all.contains(QStringLiteral("25")));
        }
        f.store.set(QStringLiteral("drivers"), loaded({{"rows", QJsonArray()}, {"failed", true}}));
        DriversTab failed(&f.ctx);
        QCOMPARE(kids<Wrap>(&failed).first()->text(), QStringLiteral("Couldn't reach F1 data right now. Retrying automatically."));
    }

    void gridTabProvisionalAndOfficial() {
        Fixture f;
        int saves = 0;
        f.ctx.saveSettings = [&saves] { ++saves; };
        QJsonObject grid = loaded(parsers::parseQualifying(t::fx("qualifying.json")));
        grid["provisional"_L1] = true;
        f.store.set(QStringLiteral("grid"), grid);
        {
            GridTab tab(&f.ctx);
            bool provisional = false;
            for (Wrap* w : kids<Wrap>(&tab)) provisional |= w->text().startsWith(QStringLiteral("Provisional"));
            QVERIFY(provisional);   // never present the qualifying order as the final grid
        }
        const QString first = grid["rows"_L1][0]["code"_L1].toString();
        const QString second = grid["rows"_L1][1]["code"_L1].toString();
        f.store.set(QStringLiteral("grid_official"),
                    loaded({{"round", "2026-21"},
                            {"rows", QJsonArray{QJsonObject{{"pos", "1"}, {"code", second}, {"moved", -1}},
                                                QJsonObject{{"pos", "2"}, {"code", first}, {"moved", 1}}}}}));
        GridTab tab(&f.ctx);
        QVERIFY(texts(&tab).contains(QStringLiteral("+1 places")));
        QCOMPARE(kids<PillButton>(&tab).size(), 2);   // Official / Provisional switch
        bool note = false;
        for (Wrap* w : kids<Wrap>(&tab)) note |= w->text().contains(QStringLiteral("1 driver starts further back"));
        QVERIFY(note);
        QSignalSpy spy(&f.store, &DataStore::changed);
        Q_EMIT kids<PillButton>(&tab).last()->clicked();   // Provisional
        QVERIFY(!f.settings.grid_official);
        QCOMPARE(saves, 1);
        QCOMPARE(spy.count(), 1);
    }

    void liveTabOutsideASession() {
        Fixture f;
        f.store.set(QStringLiteral("last_session"),
                    loaded({{"session", QJsonObject{{"name", "Race"}, {"location", "Sao Paulo"}, {"start", 1762707600}}},
                            {"rows", parsers::parseOpenf1SessionResult(t::fx("openf1_result_race.json"),
                                                                        parsers::parseOpenf1Drivers(t::fx("openf1_drivers.json")),
                                                                        QStringLiteral("Race"))}}));
        LiveTab tab(&f.ctx);
        const QStringList all = texts(&tab);
        QVERIFY(all.contains(QStringLiteral("LIVE TIMING")));
        QVERIFY(all.contains(QStringLiteral("LAST SESSION · SAO PAULO · RACE")));
        QVERIFY(all.contains(QStringLiteral("1:35:23.504")));
        QVERIFY(liveMessage(QStringLiteral("free")).startsWith(QStringLiteral("Live timing during a")));
    }

    // A finished weekend with five sessions (OpenF1 meeting 1270) for the session pills.
    static QJsonObject weekendWith(const QJsonArray& raceRows, qint64 race) {
        QJsonArray sessions;
        QJsonObject results;
        const char* names[][2] = {{"Practice 1", "Practice"}, {"Practice 2", "Practice"}, {"Practice 3", "Practice"},
                                  {"Qualifying", "Qualifying"}, {"Race", "Race"}};
        for (int i = 0; i < 5; ++i) {
            const qint64 start = race - (4 - i) * 20 * 3600;
            sessions.append(QJsonObject{{"key", 901 + i}, {"meeting", 1270}, {"name", names[i][0]}, {"kind", names[i][1]},
                                        {"location", "Las Vegas"}, {"start", start}, {"end", start + 3600}});
            if (i != 1) results.insert(QString::number(901 + i), QJsonObject{{"rows", raceRows}, {"fetched", race + 86400}});
        }
        return {{"location", "Las Vegas"}, {"sessions", sessions}, {"results", results}};
    }
    static PillButton* pill(QWidget* tab, const QString& label) {
        for (auto* b : tab->findChildren<PillButton*>())
            if (b->accessibleName() == label + QStringLiteral(" tab")) return b;
        return nullptr;
    }

    void liveTabWeekendPills() {
        Fixture f;
        const QJsonArray rows = parsers::parseOpenf1SessionResult(
            t::fx("openf1_result_race.json"), parsers::parseOpenf1Drivers(t::fx("openf1_drivers.json")), QStringLiteral("Race"));
        const qint64 race = nowSec() - 3 * 86400;
        f.store.set(QStringLiteral("last_session"), loaded({{"session", QJsonObject{{"key", 905}, {"meeting", 1270}, {"name", "Race"}}},
                                                           {"rows", rows}}));
        f.store.set(QStringLiteral("weekends"), loaded({{"meetings", QJsonObject{{"1270", weekendWith(rows, race)}}}}));
        auto tab = std::make_unique<LiveTab>(&f.ctx);
        QCOMPARE(tab->findChildren<PillButton*>().size(), 6);   // FP1 FP2 FP3 Quali Grid Race
        QVERIFY(pill(tab.get(), QStringLiteral("Grid")));
        QVERIFY(texts(tab.get()).contains(QStringLiteral("SESSIONS · LAS VEGAS")));
        QVERIFY(texts(tab.get()).contains(QStringLiteral("1:35:23.504")));   // the race, latest with a classification
        QSignalSpy rebuild(tab.get(), &Tab::rebuildRequested);
        Q_EMIT pill(tab.get(), QStringLiteral("FP2"))->clicked();
        QCOMPARE(rebuild.size(), 1);
        QCOMPARE(f.ctx.picks.value(QStringLiteral("live")), QStringLiteral("902"));
        tab = std::make_unique<LiveTab>(&f.ctx);   // what the panel does on rebuildRequested
        bool waiting = false;
        for (auto* w : tab->findChildren<Wrap*>()) waiting |= w->text().contains(QStringLiteral("OpenF1 has no classification for Practice 2."));
        QVERIFY(waiting);   // FP2 ended days ago without a classification
        Q_EMIT pill(tab.get(), QStringLiteral("FP1"))->clicked();
        tab = std::make_unique<LiveTab>(&f.ctx);
        bool fp1 = false;
        for (auto* w : tab->findChildren<Wrap*>()) fp1 |= w->text().startsWith(QStringLiteral("Practice 1 · final classification"));
        QVERIFY(fp1);
        // Grid in Live: OpenF1's starting grid when there is one, with the Official/Provisional switch.
        QJsonArray official = rows;
        QJsonObject moved = official[0].toObject();
        moved["moved"_L1] = 3;
        official[0] = moved;
        f.store.set(QStringLiteral("grid_official"), loaded({{"round", "2025-21"}, {"rows", official}}));
        f.store.set(QStringLiteral("results"), loaded({{"season", "2025"}, {"round", "21"}, {"rows", QJsonArray{QJsonObject{{"pos", "1"}}}}}));
        f.store.set(QStringLiteral("weekends"), loaded({{"rounds", QJsonObject{{"2025-21", 1270}}},
                                                        {"meetings", QJsonObject{{"1270", weekendWith(rows, race)}}}}));
        f.ctx.picks.insert(QStringLiteral("live"), QStringLiteral("grid"));
        f.settings.grid_official = true;
        tab = std::make_unique<LiveTab>(&f.ctx);
        QVERIFY(pill(tab.get(), QStringLiteral("Official")) && pill(tab.get(), QStringLiteral("Provisional")));
        QVERIFY(texts(tab.get()).contains(QStringLiteral("+3 places")));
        Q_EMIT pill(tab.get(), QStringLiteral("Provisional"))->clicked();
        QVERIFY(!f.settings.grid_official);
        tab = std::make_unique<LiveTab>(&f.ctx);
        bool prov = false;
        for (auto* w : tab->findChildren<Wrap*>()) prov |= w->text().startsWith(QStringLiteral("Provisional: the qualifying order"));
        QVERIFY(prov);
        QCOMPARE(tab->findChildren<Chip*>().size(), rows.size());   // OpenF1 rows keep their team colour
    }

    void lastTabWeekendPills() {
        Fixture f;
        QJsonObject res = loaded(parsers::parseResults(t::fx("results.json")));
        const QString round = res["season"_L1].toString() + u'-' + res["round"_L1].toString();
        QJsonObject quali = loaded(parsers::parseQualifying(t::fx("qualifying.json")));
        quali["season"_L1] = res["season"_L1];
        quali["round"_L1] = res["round"_L1];
        f.store.set(QStringLiteral("results"), res);
        f.store.set(QStringLiteral("last_qualifying"), quali);
        const QJsonArray ofRows = parsers::parseOpenf1SessionResult(
            t::fx("openf1_result_fp1.json"), parsers::parseOpenf1Drivers(t::fx("openf1_drivers.json")), QStringLiteral("Practice"));

        // Without OpenF1's list: Jolpica's sessions only (Quali, Grid, Race).
        auto tab = std::make_unique<ResultsTab>(&f.ctx);
        QCOMPARE(tab->findChildren<PillButton*>().size(), 3);
        QVERIFY(pill(tab.get(), QStringLiteral("Grid")) && pill(tab.get(), QStringLiteral("Quali")));

        f.store.set(QStringLiteral("weekends"),
                    loaded({{"rounds", QJsonObject{{round, 1270}}},
                            {"meetings", QJsonObject{{"1270", weekendWith(ofRows, res["date_ts"_L1].toInteger())}}}}));
        tab = std::make_unique<ResultsTab>(&f.ctx);
        QCOMPARE(tab->findChildren<PillButton*>().size(), 6);   // FP1 FP2 FP3 Quali Grid Race
        QVERIFY(texts(tab.get()).contains(QStringLiteral("LAST RACE · BRAZILIAN GRAND PRIX")));
        Q_EMIT pill(tab.get(), QStringLiteral("Grid"))->clicked();
        QCOMPARE(f.ctx.picks.value(QStringLiteral("last")), QStringLiteral("grid"));
        tab = std::make_unique<ResultsTab>(&f.ctx);
        bool grid = false;
        for (auto* w : tab->findChildren<Wrap*>()) grid |= w->text().startsWith(QStringLiteral("The grid the race started from"));
        QVERIFY(grid);
        QVERIFY(pill(tab.get(), QStringLiteral("Official")) && pill(tab.get(), QStringLiteral("Provisional")));
        Q_EMIT pill(tab.get(), QStringLiteral("FP1"))->clicked();
        tab = std::make_unique<ResultsTab>(&f.ctx);
        bool fp1 = false;
        for (auto* w : tab->findChildren<Wrap*>()) fp1 |= w->text().startsWith(QStringLiteral("Practice 1 · final classification"));
        QVERIFY(fp1);
        Q_EMIT pill(tab.get(), QStringLiteral("Quali"))->clicked();   // Jolpica's qualifying
        tab = std::make_unique<ResultsTab>(&f.ctx);
        QVERIFY(texts(tab.get()).contains(quali["rows"_L1][0]["detail"_L1].toString()));
    }

    void liveTabDuringASession() {
        Fixture f;
        f.settings.debug_force_tabs = true;
        const QJsonArray rows{
            QJsonObject{{"pos", 1}, {"code", "VER"}, {"team", "Red Bull"}, {"best", "1:31.0"}, {"gap", ""}, {"pits", 1}},
            QJsonObject{{"pos", 2}, {"code", "NOR"}, {"team", "McLaren"}, {"in_pit", true}, {"best", "+1.2"}, {"gap", "+1.2"}, {"pits", 0}}};
        f.store.set(QStringLiteral("live"),
                    {{"rows", rows}, {"latest_sec", nowSec()}, {"flag", "SAFETY CAR"},
                     {"session", QJsonObject{{"name", "Race"}, {"kind", "Race"}}},
                     {"clock", QJsonObject{{"remaining", 600}, {"at", nowSec()}, {"running", false}}}},
                    false);
        LiveTab tab(&f.ctx);
        const QStringList all = texts(&tab);
        QVERIFY(all.contains(QStringLiteral("● LIVE · Race")));
        QVERIFY(all.contains(QStringLiteral("Safety car")));
        QVERIFY(all.contains(QStringLiteral("10:00 left")));
        QVERIFY(all.contains(QStringLiteral("McLaren  ·  PIT")));
        QVERIFY(all.contains(QStringLiteral("1 pit")));
    }

    void spoilerCoverReplacesResults() {
        Fixture f;
        f.settings.spoiler_free = true;
        f.settings.spoiler_revealed_at = 0;
        int saves = 0;
        f.ctx.saveSettings = [&saves] { ++saves; };
        f.store.set(QStringLiteral("results"), loaded(parsers::parseResults(t::fx("results.json"))));   // a finished race
        F1Panel p(&f.ctx, QStringLiteral("popup"));
        p.resize(500, 640);
        p.setView(QStringLiteral("results"), false);
        QVERIFY(qobject_cast<SpoilerTab*>(p.tabWidget));
        ClickText* show = nullptr;
        for (ClickText* c : kids<ClickText>(p.tabWidget)) show = c;
        QVERIFY(show && show->text() == u"Show results");
        Q_EMIT show->clicked();
        QVERIFY(f.settings.spoiler_revealed_at > 0);
        QCOMPARE(saves, 1);
        QVERIFY(qobject_cast<ResultsTab*>(p.tabWidget));
        p.setView(QStringLiteral("schedule"), false);   // the schedule is never hidden
        QVERIFY(qobject_cast<ScheduleTab*>(p.tabWidget));
    }

    // test_settings_dialog.py::test_live_data_choice_maps_to_settings
    void settingsDialogLiveChoiceMapsToSettings() {
        Fixture f;
        QMap<QString, QString> vault;
        CredentialStore creds;
        creds.get = [&vault](const QString& u) { return vault.contains(u) ? std::optional<QString>(vault[u]) : std::nullopt; };
        creds.set = [&vault](const QString& u, const QString& p) {
            vault[u] = p;
            return true;
        };
        creds.remove = [&vault](const QString& u) { vault.remove(u); };
        const std::tuple<QString, QString, QString> cases[] = {
            {QStringLiteral("free"), QStringLiteral("openf1"), QStringLiteral("free")},
            {QStringLiteral("combo"), QStringLiteral("f1_feed"), QStringLiteral("free")},
            {QStringLiteral("sponsor"), QStringLiteral("openf1"), QStringLiteral("sponsor")}};
        for (const auto& [mode, source, plan] : cases) {
            Settings s;
            int saves = 0;
            SettingsDialog d(&s, &f.store, f.theme, [&saves] { ++saves; }, creds);
            d.liveMode->setCurrentIndex(d.liveMode->findData(mode));
            QCOMPARE(d.user->isEnabled(), mode == u"sponsor");
            if (mode == u"sponsor") {
                d.user->setText(QStringLiteral("me@example.com"));
                d.pw->setText(QStringLiteral("s3cret"));
            }
            d.saveAndClose();
            QCOMPARE(saves, 1);
            QCOMPARE(s.live_source, source);
            QCOMPARE(s.openf1_plan, plan);
            SettingsDialog d2(&s, &f.store, f.theme, [] {}, creds);   // reopens on the same choice
            QCOMPARE(d2.liveMode->currentData().toString(), mode);
            for (auto it = settingsTips().begin(); it != settingsTips().end(); ++it)   // every tooltip has a control
                QVERIFY2(d2.control(it.key()), qPrintable(it.key()));
            if (mode == u"sponsor") {
                QCOMPARE(vault.value(QStringLiteral("me@example.com")), QStringLiteral("s3cret"));
                QVERIFY(!QJsonDocument(s.toJson()).toJson().contains("s3cret"));   // never in settings.json
                QVERIFY(d2.pw->placeholderText().contains(QStringLiteral("stored")));
            }
        }
    }

    void settingsDialogSavesPillAndSpoiler() {
        Fixture f;
        Settings s;
        CredentialStore none{[](const QString&) { return std::optional<QString>(); },
                             [](const QString&, const QString&) { return true; }, [](const QString&) {}};
        SettingsDialog d(&s, &f.store, f.theme, [] {}, none);
        qobject_cast<QComboBox*>(d.control(QStringLiteral("p_preset")))->setCurrentIndex(4);   // Compact (one line)
        qobject_cast<QCheckBox*>(d.control(QStringLiteral("spoiler")))->setChecked(true);
        qobject_cast<QComboBox*>(d.control(QStringLiteral("p_hmode")))->setCurrentIndex(1);    // full height
        d.saveAndClose();
        QCOMPARE(s.pill_lines, 1);
        QCOMPARE(s.pill_top, QStringLiteral("{session} {countdown_short}"));
        QCOMPARE(s.pill_height, -1);
        QVERIFY(s.spoiler_free && s.spoiler_revealed_at > 0);   // turning it on hides only what comes next
    }

    void circuitMappingForWholeSeason() {
        TrackLibrary lib;
        QVERIFY(lib.load(tracksFile()) > 20);
        QStringList missing;
        for (const QJsonValue& r : t::fx("season.json")["MRData"_L1]["RaceTable"_L1]["Races"_L1].toArray()) {
            const QString id = r["Circuit"_L1]["circuitId"_L1].toString();
            if (!lib.points(id)) missing << id;
        }
        QCOMPARE(missing, QStringList());
        QCOMPARE(lib.points(QStringLiteral("las_vegas")), lib.points(QStringLiteral("vegas")));   // alias
        QVERIFY(!lib.points(QStringLiteral("../etc")) && !lib.points(QStringLiteral("unknown_ring")));
        const TrackGeometry g = trackGeometry(*lib.points(QStringLiteral("monza")));
        QVERIFY(g.box.width() > 0 && g.stroke > 0);
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
