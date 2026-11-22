"""Weather forecast, live track weather, the official grid and the weather tokens."""
from types import SimpleNamespace

from f1win.core import weather
from f1win.core.parsers import parse_openf1_starting_grid, parse_race
from f1win.core.sessions import DURATION_MIN, Session

H = 3600
T0 = 1_800_000_000 - (1_800_000_000 % H)


def meteo(hours=6, rain_at=None):
    return {"hourly": {
        "time": [T0 + i * H for i in range(hours)],
        "temperature_2m": [10 + i for i in range(hours)],
        "precipitation_probability": [80 if i == rain_at else 5 for i in range(hours)],
        "precipitation": [2.0 if i == rain_at else 0.0 for i in range(hours)],
        "weather_code": [63 if i == rain_at else 3 for i in range(hours)],
        "wind_speed_10m": [12.0] * hours}}


def test_parse_and_point_forecast():
    fc = weather.parse_open_meteo(meteo())
    assert len(fc["times"]) == 6 and fc["temp"][0] == 10
    assert weather.forecast_at(fc, T0 + 20 * 60)["temp"] == 10
    assert weather.forecast_at(fc, T0 + 30 * H) is None            # out of range
    assert weather.parse_open_meteo({"nope": 1}) is None


def test_session_window_sees_rain_after_the_start():
    fc = weather.parse_open_meteo(meteo(rain_at=1))
    win = weather.forecast_window(fc, T0, T0 + DURATION_MIN["race"] * 60)
    assert win["rain_prob"] == 80 and win["wet_from"] == T0 + H        # dry start, rain in the second hour
    assert (win["temp_min"], win["temp_max"]) == (10, 11)                # the two hours of a 2 h race
    verdict, level = weather.outlook(win)
    assert level == "wet"
    dry = weather.forecast_window(weather.parse_open_meteo(meteo()), T0, T0 + 2 * H)
    assert weather.outlook(dry)[1] == "dry"


def test_units_and_refresh():
    assert weather.temp(20, "imperial") == "68°" and weather.temp(20, "metric", True) == "20°C"
    assert weather.temp_range(10.2, 12.4, "metric") == "10–12°"
    assert weather.wind(16.09, "imperial").startswith("10")
    s = [Session("fp1", T0), Session("race", T0 + 2 * 86400)]
    assert weather.refresh_seconds(s, T0 - H, DURATION_MIN, "normal") == 10 * 60        # just before FP1
    assert weather.refresh_seconds(s, T0 - H, DURATION_MIN, "fast") == 5 * 60
    assert weather.refresh_seconds(s, T0 + 86400, DURATION_MIN, "normal") == 30 * 60    # weekend
    assert weather.refresh_seconds(s, T0 - 9 * 86400, DURATION_MIN, "slow") == 360 * 60
    assert weather.refresh_seconds(s, T0, DURATION_MIN, "custom", [7, 40, 300]) == 7 * 60
    assert weather.refresh_seconds(s, T0, DURATION_MIN, "off") is None


def test_live_weather_from_the_f1_feed():
    w = weather.parse_live_weather({"AirTemp": "18.4", "TrackTemp": "31.2", "Humidity": "40",
                                    "Rainfall": "1", "WindSpeed": "2.5"})
    assert w["air"] == 18.4 and w["track"] == 31.2 and w["rain"] is True
    assert weather.parse_live_weather({"Humidity": "40"}) is None


def test_race_keeps_circuit_coordinates():
    r = parse_race({"season": "2026", "round": "21", "raceName": "Las Vegas Grand Prix", "date": "2026-11-22",
                    "time": "04:00:00Z", "Circuit": {"circuitId": "vegas", "circuitName": "Las Vegas Strip",
                                                     "Location": {"lat": "36.1147", "long": "-115.173",
                                                                  "locality": "Las Vegas", "country": "USA"}}})
    assert (r.lat, r.lon) == (36.1147, -115.173)
    from f1win.core.sessions import Race
    assert Race.from_dict(r.to_dict()).lat == 36.1147


def test_official_grid_marks_penalties():
    provisional = [{"pos": "1", "code": "ANT", "team": "Mercedes", "name": "Antonelli"},
                   {"pos": "2", "code": "VER", "team": "Red Bull"}, {"pos": "3", "code": "RUS", "team": "Mercedes"}]
    drivers = {"12": {"acr": "ANT", "team": "Mercedes"}, "3": {"acr": "VER", "team": "Red Bull"},
               "63": {"acr": "RUS", "team": "Mercedes"}}
    grid = [{"position": 1, "driver_number": 3, "lap_duration": 94.151},
            {"position": 2, "driver_number": 63, "lap_duration": 94.224},
            {"position": 3, "driver_number": 12, "lap_duration": 93.893}]
    rows = parse_openf1_starting_grid(grid, drivers, provisional)
    assert [r["code"] for r in rows] == ["VER", "RUS", "ANT"]
    assert rows[2]["moved"] == 2 and rows[2]["name"] == "Antonelli" and rows[0]["detail"] == "1:34.151"


def test_weather_tokens_for_the_taskbar():
    from f1win.ui.taskbar_pill import weather_fields
    race = SimpleNamespace(season="2026", round="21", sessions=[Session("race", T0 + H)])
    fc = {"round": "2026-21", **weather.parse_open_meteo(meteo(rain_at=2))}
    s = SimpleNamespace(units="metric")
    f = weather_fields(race, None, fc, s, T0)
    assert f["temp"] == "11°" and f["rain"] == "80%" and f["wx"] == "11° · 80%"
    live = {"weather": {"air": 20.0, "track": 35.0, "rain": False}}
    f = weather_fields(race, live, fc, s, T0 + H + 60)                    # during the race: live values
    assert f["temp"] == "20°" and f["track_temp"] == "35°"


def test_weather_survives_a_restart(tmp_path):
    from f1win.services.store import DataStore
    a = DataStore(tmp_path / "c.json")
    a.set("weather", {"loaded": True, "round": "2026-21", **weather.parse_open_meteo(meteo())})
    a.set("measured_weather", {"loaded": True, "round": "2026-21", "sessions": {"fp1": {"air_min": 11.6, "air_max": 13,
                                                                                        "rain": True}}})
    a.save_cache()
    b = DataStore(tmp_path / "c.json")
    assert b.load_cache()
    assert b.get("weather")["times"] and b.get("measured_weather")["sessions"]["fp1"]["rain"] is True
