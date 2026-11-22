"""Track weather: the Open-Meteo forecast for each session, and live conditions from F1's feed.

Forecast: Open-Meteo (https://open-meteo.com, free, no key, CC BY 4.0) for the circuit's
coordinates, hourly up to 16 days ahead. Live: the WeatherData topic of F1's live timing stream
(air and track temperature, humidity, rain, wind) while a session runs.
"""
from __future__ import annotations

from ..i18n import T

OPEN_METEO = "https://api.open-meteo.com/v1/forecast"
FORECAST_DAYS = 16
PAST_DAYS = 3                   # so sessions earlier in the weekend still show their weather
MAX_HOURS = (FORECAST_DAYS + PAST_DAYS) * 24 + 24


def forecast_url(lat: float, lon: float) -> str:
    return (f"{OPEN_METEO}?latitude={lat:.4f}&longitude={lon:.4f}"
            "&hourly=temperature_2m,precipitation_probability,precipitation,weather_code,wind_speed_10m"
            f"&timeformat=unixtime&timezone=UTC&forecast_days={FORECAST_DAYS}&past_days={PAST_DAYS}")


def _nums(arr, kind=float) -> list:
    out = []
    for v in (arr if isinstance(arr, list) else [])[:MAX_HOURS]:
        try:
            out.append(kind(v) if v is not None else None)
        except (TypeError, ValueError):
            out.append(None)
    return out


def parse_open_meteo(data) -> dict | None:
    """Open-Meteo JSON -> {"times": [...], "temp": [...], ...} (equal-length lists), or None."""
    hourly = data.get("hourly") if isinstance(data, dict) else None
    if not isinstance(hourly, dict):
        return None
    times = _nums(hourly.get("time"), int)
    if not times or any(t is None for t in times):
        return None
    n = len(times)

    def col(key, kind=float):
        v = _nums(hourly.get(key), kind)
        return (v + [None] * n)[:n]

    return {"times": times, "temp": col("temperature_2m"), "rain_prob": col("precipitation_probability"),
            "rain": col("precipitation"), "code": col("weather_code", int), "wind": col("wind_speed_10m")}


def forecast_at(weather: dict | None, ts: int) -> dict | None:
    """The forecast hour nearest to ``ts`` (within 90 minutes), or None when out of range."""
    times = (weather or {}).get("times") or []
    if not times:
        return None
    i = min(range(len(times)), key=lambda k: abs(times[k] - ts))
    if abs(times[i] - ts) > 90 * 60:
        return None
    hour = {k: (weather.get(k) or [None] * len(times))[i] for k in ("temp", "rain_prob", "rain", "code", "wind")}
    return hour if hour["temp"] is not None else None


def forecast_window(weather: dict | None, start: int, end: int) -> dict | None:
    """Summary over a session (every forecast hour from its start to its end): what matters is
    whether it rains at any point, not just at the start."""
    times = (weather or {}).get("times") or []
    idx = [i for i, ts in enumerate(times) if start - 1800 <= ts < end]
    if not idx:
        hour = forecast_at(weather, start)
        return None if hour is None else {**hour, "hours": [{**hour, "ts": start}], "temp_min": hour["temp"],
                                           "temp_max": hour["temp"], "rain_total": hour.get("rain") or 0.0,
                                           "wet_from": start if is_wet(hour) else None}
    hours = []
    for i in idx:
        h = {k: (weather.get(k) or [None] * len(times))[i] for k in ("temp", "rain_prob", "rain", "code", "wind")}
        if h["temp"] is not None:
            hours.append({**h, "ts": times[i]})
    if not hours:
        return None
    temps = [h["temp"] for h in hours]
    worst = max(hours, key=lambda h: (_severity(h.get("code")), h.get("rain_prob") or 0))
    wet = next((h for h in hours if is_wet(h)), None)
    return {"temp": hours[0]["temp"], "temp_min": min(temps), "temp_max": max(temps),
            "rain_prob": max((h.get("rain_prob") or 0) for h in hours),
            "rain": max((h.get("rain") or 0) for h in hours), "rain_total": sum((h.get("rain") or 0) for h in hours),
            "code": worst.get("code"), "wind": max((h.get("wind") or 0) for h in hours),
            "hours": hours, "wet_from": wet["ts"] if wet else None}


def _severity(code) -> int:
    c = int(code or 0)
    for level, codes in ((5, (95, 96, 99)), (4, (65, 67, 82)), (3, (61, 63, 66, 80, 81)),
                         (2, (51, 53, 55, 56, 57, 71, 73, 75, 77, 85, 86)), (1, (3, 45, 48))):
        if c in codes:
            return level
    return 0


def outlook(window: dict | None) -> tuple[str, str]:
    """(verdict, level) for a session window: level is "dry", "risk" or "wet"."""
    if not window:
        return "", ""
    p = window.get("rain_prob") or 0
    if is_wet(window) or (window.get("rain_total") or 0) >= 1.0:
        return T("Wet: rain expected"), "wet"
    if p >= 25 or _severity(window.get("code")) >= 2:
        return T("Rain possible ({chance}%)", chance=round(p)), "risk"
    return T("Dry: rain unlikely ({chance}%)", chance=round(p)), "dry"


# (around a session, during the race weekend, otherwise), in minutes
REFRESH = {"fast": (5, 15, 60), "normal": (10, 30, 240), "slow": (30, 120, 360)}


def timings(speed: str, custom=None) -> tuple[int, int, int]:
    """(around a session, race weekend, otherwise) in minutes for a refresh setting."""
    if speed == "custom" and custom:
        try:
            near, weekend, other = (max(5, int(x)) for x in list(custom)[:3])
            return near, weekend, other
        except (TypeError, ValueError):
            pass
    return REFRESH.get(speed, REFRESH["normal"])


def fmt_minutes(m: int) -> str:
    return T("{hours} h", hours=m // 60) if m >= 60 and m % 60 == 0 else T("{minutes} min", minutes=m)


def refresh_seconds(sessions, now: int, durations: dict, speed: str = "normal", custom=None) -> int | None:
    """How soon to fetch the forecast again (None = never: updates are off)."""
    if speed == "off":
        return None
    near, weekend, other = timings(speed, custom)
    for s in sessions or []:
        end = s.ts + durations.get(s.id, 60) * 60
        if s.ts - 3 * 3600 <= now <= end:
            return near * 60                   # just before and during a session
    if sessions and sessions[0].ts - 24 * 3600 <= now <= sessions[-1].ts + 3 * 3600:
        return weekend * 60                    # race weekend
    return other * 60


def refresh_text(speed: str, custom=None) -> str:
    if speed == "off":
        return T("Forecast updates are off (Settings › Formula 1).")
    near, weekend, other = timings(speed, custom)
    return T("Updated every {near} around sessions, {weekend} during the weekend, {other} otherwise.",
             near=fmt_minutes(near), weekend=fmt_minutes(weekend), other=fmt_minutes(other))


# WMO weather interpretation codes (Open-Meteo), grouped into words that matter at a race track.
def condition(code) -> str:
    if code is None:
        return ""
    c = int(code)
    if c == 0:
        return T("Clear")
    if c in (1, 2):
        return T("Partly cloudy")
    if c == 3:
        return T("Overcast")
    if c in (45, 48):
        return T("Fog")
    if c in (51, 53, 55, 56, 57):
        return T("Drizzle")
    if c in (61, 63, 66, 80, 81):
        return T("Rain")
    if c in (65, 67, 82):
        return T("Heavy rain")
    if c in (71, 73, 75, 77, 85, 86):
        return T("Snow")
    if c in (95, 96, 99):
        return T("Thunderstorm")
    return ""


def is_wet(hour: dict | None) -> bool:
    """Rain likely enough to matter for the session (shown highlighted)."""
    if not hour:
        return False
    return (hour.get("rain_prob") or 0) >= 50 or (hour.get("rain") or 0) >= 0.5 or \
        (hour.get("code") or 0) in (61, 63, 65, 66, 67, 80, 81, 82, 95, 96, 99)


def temp(c: float | None, units: str = "metric", unit: bool = False) -> str:
    """Celsius in, "18°" / "64°" out (with "C"/"F" when ``unit``)."""
    if c is None:
        return ""
    v = c * 9 / 5 + 32 if units == "imperial" else c
    return f"{round(v)}°" + (("F" if units == "imperial" else "C") if unit else "")


def temp_range(lo: float, hi: float, units: str = "metric", unit: bool = False) -> str:
    a, b = temp(lo, units), temp(hi, units)
    suffix = ("F" if units == "imperial" else "C") if unit else ""
    return (f"{a[:-1]}–{b}" if a != b else b) + suffix


def wind(kmh: float | None, units: str = "metric") -> str:
    if kmh is None:
        return ""
    return T("{speed} mph", speed=round(kmh / 1.609)) if units == "imperial" else T("{speed} km/h", speed=round(kmh))


def short(hour: dict | None, units: str = "metric") -> str:
    """"18° · 20%" for the schedule list."""
    if not hour:
        return ""
    bits = [temp(hour['temp'], units)]
    if hour.get("rain_prob") is not None:
        bits.append(f"{round(hour['rain_prob'])}%")
    return " · ".join(bits)


# ── live (F1 timing stream, WeatherData topic) ───────────────────────────────

def _f(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def parse_live_weather(d) -> dict | None:
    """F1 WeatherData -> {"air", "track", "humidity", "rain", "wind", "wind_dir"} (numbers / bool)."""
    if not isinstance(d, dict):
        return None
    w = {"air": _f(d.get("AirTemp")), "track": _f(d.get("TrackTemp")), "humidity": _f(d.get("Humidity")),
         "rain": str(d.get("Rainfall", "0")).strip() not in ("0", "", "False", "false"),
         "wind": _f(d.get("WindSpeed")), "wind_dir": _f(d.get("WindDirection"))}
    return w if w["air"] is not None or w["track"] is not None else None
