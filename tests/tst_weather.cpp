// Port of reference-python/tests/test_weather.py
#include "common.h"
#include "core/pilltext.h"
#include "core/weather.h"
#include "services/store.h"

#include <QTemporaryDir>

using namespace f1;
namespace wx = f1::weather;

namespace {
constexpr qint64 H = 3600;
constexpr qint64 T0 = 1'800'000'000 - (1'800'000'000 % H);

QJsonObject meteo(int hours = 6, int rainAt = -1) {
    QJsonArray time, temp, prob, rain, code, wind;
    for (int i = 0; i < hours; ++i) {
        time.append(T0 + i * H);
        temp.append(10 + i);
        prob.append(i == rainAt ? 80 : 5);
        rain.append(i == rainAt ? 2.0 : 0.0);
        code.append(i == rainAt ? 63 : 3);
        wind.append(12.0);
    }
    return {{"hourly", QJsonObject{{"time", time}, {"temperature_2m", temp}, {"precipitation_probability", prob},
                                   {"precipitation", rain}, {"weather_code", code}, {"wind_speed_10m", wind}}}};
}
}  // namespace

class TestWeather : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parseAndPointForecast() {
        const auto fc = wx::parseOpenMeteo(meteo());
        QVERIFY(fc);
        QCOMPARE(fc->times.size(), 6);
        QCOMPARE(*fc->temp[0], 10.0);
        QCOMPARE(*wx::forecastAt(&*fc, T0 + 20 * 60)->temp, 10.0);
        QVERIFY(!wx::forecastAt(&*fc, T0 + 30 * H));   // out of range
        QVERIFY(!wx::parseOpenMeteo(QJsonObject{{"nope", 1}}));
    }
    void sessionWindowSeesRainAfterTheStart() {
        const auto fc = wx::parseOpenMeteo(meteo(6, 1));
        const auto win = wx::forecastWindow(&*fc, T0, T0 + durationMin(QStringLiteral("race")) * 60);
        QVERIFY(win);
        QCOMPARE(*win->rainProb, 80.0);
        QCOMPARE(win->wetFrom.value(), T0 + H);   // dry start, rain in the second hour
        QCOMPARE(win->tempMin, 10.0);
        QCOMPARE(win->tempMax, 11.0);             // the two hours of a 2 h race
        QCOMPARE(wx::outlook(win).second, QStringLiteral("wet"));
        const auto dryFc = wx::parseOpenMeteo(meteo());
        QCOMPARE(wx::outlook(wx::forecastWindow(&*dryFc, T0, T0 + 2 * H)).second, QStringLiteral("dry"));
    }
    void unitsAndRefresh() {
        QCOMPARE(wx::temp(20, QStringLiteral("imperial")), QStringLiteral("68°"));
        QCOMPARE(wx::temp(20, QStringLiteral("metric"), true), QStringLiteral("20°C"));
        QCOMPARE(wx::tempRange(10.2, 12.4, QStringLiteral("metric")), QStringLiteral("10–12°"));
        QVERIFY(wx::wind(16.09, QStringLiteral("imperial")).startsWith(QStringLiteral("10")));
        const Sessions s{Session{QStringLiteral("fp1"), T0}, Session{QStringLiteral("race"), T0 + 2 * 86400}};
        const auto& d = durationTable();
        QCOMPARE(wx::refreshSeconds(s, T0 - H, d, QStringLiteral("normal")).value(), 10 * 60);   // just before FP1
        QCOMPARE(wx::refreshSeconds(s, T0 - H, d, QStringLiteral("fast")).value(), 5 * 60);
        QCOMPARE(wx::refreshSeconds(s, T0 + 86400, d, QStringLiteral("normal")).value(), 30 * 60);   // weekend
        QCOMPARE(wx::refreshSeconds(s, T0 - 9 * 86400, d, QStringLiteral("slow")).value(), 360 * 60);
        QCOMPARE(wx::refreshSeconds(s, T0, d, QStringLiteral("custom"), QJsonArray{7, 40, 300}).value(), 7 * 60);
        QVERIFY(!wx::refreshSeconds(s, T0, d, QStringLiteral("off")));
    }
    void bankersRoundingLikePython() {
        QCOMPARE(wx::temp(20.5), QStringLiteral("20°"));   // round(20.5) == 20 in Python
        QCOMPARE(wx::temp(21.5), QStringLiteral("22°"));
    }
    void liveWeatherFromTheF1Feed() {
        const auto w = wx::parseLiveWeather(QJsonObject{{"AirTemp", "18.4"}, {"TrackTemp", "31.2"}, {"Humidity", "40"},
                                                        {"Rainfall", "1"}, {"WindSpeed", "2.5"}});
        QVERIFY(w);
        QCOMPARE(*w->air, 18.4);
        QCOMPARE(*w->track, 31.2);
        QVERIFY(w->rain);
        QVERIFY(!wx::parseLiveWeather(QJsonObject{{"Humidity", "40"}}));
    }
    void raceKeepsCircuitCoordinates() {
        const QJsonObject raw{{"season", "2026"}, {"round", "21"}, {"raceName", "Las Vegas Grand Prix"}, {"date", "2026-11-22"},
                              {"time", "04:00:00Z"},
                              {"Circuit", QJsonObject{{"circuitId", "vegas"}, {"circuitName", "Las Vegas Strip"},
                                                      {"Location", QJsonObject{{"lat", "36.1147"}, {"long", "-115.173"},
                                                                               {"locality", "Las Vegas"}, {"country", "USA"}}}}}};
        const auto r = parsers::parseRace(raw);
        QCOMPARE(r->lat.value(), 36.1147);
        QCOMPARE(r->lon.value(), -115.173);
        QCOMPARE(Race::fromJson(r->toJson()).lat.value(), 36.1147);
    }
    void officialGridMarksPenalties() {
        const QJsonArray provisional{QJsonObject{{"pos", "1"}, {"code", "ANT"}, {"team", "Mercedes"}, {"name", "Antonelli"}},
                                     QJsonObject{{"pos", "2"}, {"code", "VER"}, {"team", "Red Bull"}},
                                     QJsonObject{{"pos", "3"}, {"code", "RUS"}, {"team", "Mercedes"}}};
        const parsers::Drivers drivers{{"12", QJsonObject{{"acr", "ANT"}, {"team", "Mercedes"}}},
                                       {"3", QJsonObject{{"acr", "VER"}, {"team", "Red Bull"}}},
                                       {"63", QJsonObject{{"acr", "RUS"}, {"team", "Mercedes"}}}};
        const QJsonArray grid{QJsonObject{{"position", 1}, {"driver_number", 3}, {"lap_duration", 94.151}},
                              QJsonObject{{"position", 2}, {"driver_number", 63}, {"lap_duration", 94.224}},
                              QJsonObject{{"position", 3}, {"driver_number", 12}, {"lap_duration", 93.893}}};
        const QJsonArray rows = parsers::parseOpenf1StartingGrid(grid, drivers, provisional);
        QCOMPARE(rows[0]["code"_L1].toString(), QStringLiteral("VER"));
        QCOMPARE(rows[1]["code"_L1].toString(), QStringLiteral("RUS"));
        QCOMPARE(rows[2]["code"_L1].toString(), QStringLiteral("ANT"));
        QCOMPARE(rows[2]["moved"_L1].toInteger(), 2);
        QCOMPARE(rows[2]["name"_L1].toString(), QStringLiteral("Antonelli"));
        QCOMPARE(rows[0]["detail"_L1].toString(), QStringLiteral("1:34.151"));
    }
    void weatherTokensForTheTaskbar() {
        Race race;
        race.season = QStringLiteral("2026");
        race.round = QStringLiteral("21");
        race.sessions = {Session{QStringLiteral("race"), T0 + H}};
        auto fc = wx::parseOpenMeteo(meteo(6, 2));
        fc->round = QStringLiteral("2026-21");
        const Settings s;
        auto f = pill::weatherFields(&race, {}, &*fc, s, T0);
        QCOMPARE(f["temp"_L1], QStringLiteral("11°"));
        QCOMPARE(f["rain"_L1], QStringLiteral("80%"));
        QCOMPARE(f["wx"_L1], QStringLiteral("11° · 80%"));
        const QJsonObject live{{"weather", QJsonObject{{"air", 20.0}, {"track", 35.0}, {"rain", false}}}};
        f = pill::weatherFields(&race, live, &*fc, s, T0 + H + 60);   // during the race: live values
        QCOMPARE(f["temp"_L1], QStringLiteral("20°"));
        QCOMPARE(f["track_temp"_L1], QStringLiteral("35°"));
    }
    void weatherSurvivesARestart() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("c.json"));
        DataStore a(file, file);
        QJsonObject w = wx::parseOpenMeteo(meteo())->toJson();
        w["loaded"_L1] = true;
        w["round"_L1] = "2026-21";
        a.set(QStringLiteral("weather"), w);
        a.set(QStringLiteral("measured_weather"),
              QJsonObject{{"loaded", true}, {"round", "2026-21"},
                          {"sessions", QJsonObject{{"fp1", QJsonObject{{"air_min", 11.6}, {"air_max", 13}, {"rain", true}}}}}});
        QVERIFY(a.saveCache());
        DataStore b(file, file);
        QVERIFY(b.loadCache());
        QVERIFY(!b.get(QStringLiteral("weather"))["times"_L1].toArray().isEmpty());
        QCOMPARE(b.get(QStringLiteral("measured_weather"))["sessions"_L1]["fp1"_L1]["rain"_L1].toBool(), true);
        QVERIFY(b.forecast() && b.forecast()->round == u"2026-21");
    }
};

QTEST_GUILESS_MAIN(TestWeather)
#include "tst_weather.moc"
