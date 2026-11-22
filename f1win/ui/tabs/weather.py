"""Weather tab: live track conditions, the outlook hour by hour, and every session's forecast.

Every table row uses the same five columns, so sky, temperature and rain line up from the hourly
strip down to the session list: [name or hour] [start] [sky ........] [temp] [rain].
"""
from __future__ import annotations

from ...core import weather
from ...core.sessions import DURATION_MIN, current_session, live_active, next_session
from ...core.timeutil import fmt_clock_time, fmt_datetime, now_sec
from ...i18n import T
from ..widgets import Row, Text, with_help
from .common import RIGHT, Tab

TEMP_W, RAIN_W = 60, 44


def _window(fc, s):
    return weather.forecast_window(fc, s.ts, s.ts + DURATION_MIN.get(s.id, 60) * 60)


def measured_for(store, race) -> dict:
    """{session id: measured summary} for this round (OpenF1 /weather after each session)."""
    m = store.get("measured_weather")
    return (m.get("sessions") or {}) if race and m.get("round") == f"{race.season}-{race.round}" else {}


def session_cells(tab, r: Row, s, fc, measured: dict, now: int, size: int = 13) -> str:
    """Add the SKY / TEMP / RAIN cells for session ``s`` to row ``r``; returns the hover text.
    Finished sessions show what F1 measured at the track; others show the forecast."""
    t, units = tab.t, tab.ctx.settings.units
    win = _window(fc, s) if fc else None
    m = measured.get(s.id)
    if m:
        rain = m.get("rain")
        r.cell(Text(T("Rained") if rain else T("Dry"), t, size, rain, t.error if rain else t.on_surface), None, 1)
        r.cell(Text(weather.temp_range(m["air_min"], m["air_max"], units), t, size, True, t.on_surface, RIGHT), TEMP_W)
        r.cell(Text(T("Wet") if rain else T("Dry"), t, size - 1, rain, t.error if rain else t.muted, RIGHT), RAIN_W)
        tip = [T("Measured at the track by F1 during the session:"),
               T("Air {low} to {high}", low=weather.temp(m["air_min"], units, True),
                 high=weather.temp(m["air_max"], units, True))]
        if m.get("track_max") is not None:
            tip.append(T("Track up to {temp}", temp=weather.temp(m["track_max"], units, True)))
        if m.get("humidity") is not None:
            tip.append(T("Humidity {value}%", value=round(m["humidity"])))
        tip.append(T("Rain fell: yes") if rain else T("Rain fell: no"))
        if win:
            tip.append(T("The forecast was: {sky}, {chance}% chance of rain.", sky=weather.condition(win.get("code")),
                         chance=round(win.get("rain_prob") or 0)))
        return "\n".join(tip)
    if win is None:
        r.cell(Text(T("not yet") if s.ts > now else "", t, size - 1, False, t.muted), None, 1)
        r.cell(Text("", t, size), TEMP_W)
        r.cell(Text("", t, size), RAIN_W)
        return ""
    verdict, level = weather.outlook(win)
    color = t.error if level == "wet" else (t.secondary if level == "risk" else t.muted)
    r.cell(Text(weather.condition(win.get("code")), t, size), None, 1)
    r.cell(Text(weather.temp_range(win["temp_min"], win["temp_max"], units), t, size, True, t.on_surface, RIGHT), TEMP_W)
    r.cell(Text(T("{chance}%", chance=round(win.get("rain_prob") or 0)), t, size - 1, False, color, RIGHT), RAIN_W)
    tip = [T("Forecast for the session:"), verdict, weather.condition(win.get("code"))]
    if win.get("rain_total"):
        tip.append(T("{mm} mm rain expected", mm=round(win["rain_total"], 1)))
    if win.get("wind"):
        tip.append(T("Wind up to {speed}", speed=weather.wind(win["wind"], units)))
    if s.ts + DURATION_MIN.get(s.id, 60) * 60 <= now:
        tip.append(T("Measured values appear here shortly after the session (OpenF1)."))
    return "\n".join(x for x in tip if x)


class WeatherTab(Tab):
    def __init__(self, ctx, parent=None) -> None:
        super().__init__(ctx, parent)
        t, store, now = self.t, ctx.store, now_sec()
        self.name_w = 112 if ctx.compact else 128
        self.start_w = 92
        race = store.race
        self._live(store, race, now)
        if race is None:
            self.hint(T("No upcoming race"))
            self.finish()
            return
        fc = store.get("weather")
        if not (fc.get("round") == f"{race.season}-{race.round}" and fc.get("times")):
            first = race.sessions[0].ts if race.sessions else 0
            self.header(T("FORECAST · {place}", place=(race.locality or race.circuit_name).upper()))
            if race.lat is None:
                self.hint(T("No location known for this circuit."))
            elif first - now > weather.FORECAST_DAYS * 86400:
                self.hint(T("The forecast appears about two weeks before the race weekend."))
            else:
                self.hint(T("Loading the forecast…"))
            self.finish()
            return

        # The session that matters most right now: the running one, else the next, else the race.
        focus = current_session(race.sessions, now) or next_session(race.sessions, now) or race.sessions[-1]
        self._outlook(fc, focus, now)
        self.lay.addSpacing(t.px(12))

        self.header(T("ALL SESSIONS · {place}", place=(race.locality or race.circuit_name).upper()))
        self._columns(T("SESSION"), T("START"),
                      T("Sky, temperature and the highest chance of rain during each session. Hover a row for details."))
        measured = measured_for(store, race)
        for s in race.sessions:
            r = self._row(Text(s.name, t, 13, s is focus, t.accent if s is focus else t.on_surface),
                          Text(fmt_datetime(s.ts, ctx.zone, ctx.twelve), t, 12, False, t.muted))
            r.setToolTip(session_cells(self, r, s, fc, measured, now))
            self.lay.addWidget(r)
        self.lay.addSpacing(t.px(8))
        self.hint(weather.refresh_text(ctx.settings.weather_refresh, ctx.settings.weather_custom), t.muted)
        self.hint(T("Weather data by Open-Meteo.com (CC BY 4.0)"), t.muted)
        self.finish()

    # ── the shared column grid ───────────────────────────────────────────────

    def _row(self, first, second=None) -> Row:
        r = Row(self.t)
        r.cell(first, self.name_w)
        r.cell(second if second is not None else Text("", self.t, 12), self.start_w)
        return r

    def _values(self, r: Row, sky: str, temp: str, chance: int, color, size: int, bold_rain: bool = False) -> None:
        t = self.t
        r.cell(Text(sky, t, size), None, 1)
        r.cell(Text(temp, t, size, True, t.on_surface, RIGHT), TEMP_W)
        r.cell(Text(T("{chance}%", chance=chance), t, size - 1, bold_rain, color, RIGHT), RAIN_W)

    def _columns(self, first: str, second: str, tip: str) -> None:
        """Small column labels above a table, with the explanation on hover."""
        t = self.t
        r = self._row(with_help(Text(first, t, 10, True, t.muted, tracking=0.3), t),
                      Text(second, t, 10, True, t.muted, tracking=0.3))
        r.lay.setContentsMargins(t.px(4), 0, t.px(4), 0)
        r.cell(Text(T("SKY"), t, 10, True, t.muted, tracking=0.3), None, 1)
        r.cell(Text(T("TEMP"), t, 10, True, t.muted, RIGHT, tracking=0.3), TEMP_W)
        r.cell(Text(T("RAIN"), t, 10, True, t.muted, RIGHT, tracking=0.3), RAIN_W)
        r.setToolTip(tip)
        self.lay.addWidget(r)

    # ── parts ────────────────────────────────────────────────────────────────

    def _live(self, store, race, now) -> None:
        t = self.t
        live = store.get("live")
        lw = live.get("weather") if isinstance(live, dict) else None
        if not (lw and live_active(live, race.sessions if race else [], now, self.ctx.settings.debug_force_tabs)):
            return
        self.header(T("TRACK NOW"))
        parts = []
        if lw.get("air") is not None:
            parts.append(T("Air {temp}", temp=weather.temp(lw["air"], self.ctx.settings.units, True)))
        if lw.get("track") is not None:
            parts.append(T("Track {temp}", temp=weather.temp(lw["track"], self.ctx.settings.units, True)))
        row = Row(t)
        row.cell(with_help(Text("  ·  ".join(parts), t, 15, True, t.on_surface), t), None, 1)
        row.cell(Text(T("Raining") if lw.get("rain") else T("Dry"), t, 13, True,
                      t.error if lw.get("rain") else t.accent, RIGHT), TEMP_W + RAIN_W)
        row.setToolTip(T("Measured at the track by F1 and updated live during the session.\n"
                         "Raining / Dry: whether F1's track sensors report rain right now."))
        self.lay.addWidget(row)
        extra = []
        if lw.get("humidity") is not None:
            extra.append(T("Humidity {value}%", value=round(lw["humidity"])))
        if lw.get("wind") is not None:
            extra.append(T("Wind {speed}", speed=weather.wind(lw["wind"] * 3.6, self.ctx.settings.units)))
        if extra:
            self.hint("  ·  ".join(extra))
        self.hint(T("Live from F1's timing feed."), t.muted)
        self.lay.addSpacing(t.px(12))

    def _outlook(self, fc, s, now) -> None:
        t, ctx = self.t, self.ctx
        units = ctx.settings.units
        win = _window(fc, s)
        running = s.ts <= now < s.ts + DURATION_MIN.get(s.id, 60) * 60
        self.header(T("{session} · NOW", session=s.name.upper()) if running
                    else T("{session} OUTLOOK", session=s.name.upper()))
        if win is None:
            self.hint(T("No forecast for this session yet."))
            return
        verdict, level = weather.outlook(win)
        verdict_color = t.error if level == "wet" else (t.secondary if level == "risk" else t.accent)
        # Line 1: sky and temperature, with the temperature in the table's temperature column.
        top = Row(t)
        top.cell(with_help(Text(weather.condition(win.get("code")), t, 15, True, t.on_surface), t), None, 1)
        top.cell(Text(weather.temp_range(win["temp_min"], win["temp_max"], units, True), t, 15, True,
                      t.on_surface, RIGHT), TEMP_W + RAIN_W)
        top.setToolTip(T("The forecast for the whole session, from lights out to the flag.\n"
                         "Air temperature from lowest to highest; the track is usually 10 to 20 °C warmer in the sun."))
        self.lay.addWidget(top)
        # Line 2: the rain verdict in colour, then the details (same indent as the rows).
        bits = [verdict]
        if win.get("wet_from"):
            bits.append(T("rain likely from {time}", time=fmt_clock_time(win["wet_from"], ctx.zone, ctx.twelve)))
        if win.get("rain_total"):
            bits.append(T("{mm} mm expected", mm=round(win["rain_total"], 1)))
        if win.get("wind"):
            bits.append(T("wind up to {speed}", speed=weather.wind(win["wind"], units)))
        line = Row(t)
        line.lay.setContentsMargins(t.px(4), 0, t.px(4), t.px(2))
        line.cell(Text(" · ".join(b for b in bits if b), t, 12, False, verdict_color), None, 1)
        line.setToolTip(T("Dry: rain unlikely (under 25%).\nRain possible: showers may come during the session.\n"
                          "Wet: rain expected at some point during the session."))
        self.lay.addWidget(line)
        # Hour by hour, from an hour before the session to an hour after it, in the same columns as the
        # session list below. The extra hours are greyed out; the verdict above counts the session only.
        s_end = s.ts + DURATION_MIN.get(s.id, 60) * 60
        hours = self._hours(fc, s.ts - 3600, s_end + 3600)[:8]
        if len(hours) > 1:
            self.lay.addSpacing(t.px(6))
            self._columns(T("HOUR"), "", T("Hour by hour, from an hour before the session to an hour after it. "
                                           "Greyed-out hours are outside the session."))
            for h in hours:
                inside = s.ts - 1800 <= h["ts"] < s_end
                past = h["ts"] + 3600 <= now
                dim = past or not inside
                r = self._row(Text(fmt_clock_time(h["ts"], ctx.zone, ctx.twelve), t, 12, inside,
                                   t.muted if dim else t.on_surface))
                wet = weather.is_wet(h)
                self._values(r, weather.condition(h.get("code")), weather.temp(h["temp"], units),
                             round(h.get("rain_prob") or 0), t.error if wet else t.muted, 12, wet)
                if not inside:                           # outside the session: clearly fainter
                    faint = "#80" + t.muted.lstrip("#")[-6:]
                    for i in range(r.lay.count()):
                        w = r.lay.itemAt(i).widget()
                        if w is not None and hasattr(w, "setColor") and not wet:
                            w.setColor(faint)
                tip = []
                if not inside:
                    tip.append(T("Before the session") if h["ts"] < s.ts else T("After the session"))
                tip.append(T("Chance of rain in this hour: {chance}%", chance=round(h.get("rain_prob") or 0)))
                if h.get("rain"):
                    tip.append(T("{mm} mm rain", mm=round(h["rain"], 1)))
                if h.get("wind") is not None:
                    tip.append(T("Wind {speed}", speed=weather.wind(h["wind"], units)))
                r.setToolTip("\n".join(tip))
                self.lay.addWidget(r)

    @staticmethod
    def _hours(fc, start: int, end: int) -> list[dict]:
        times = fc.get("times") or []
        out = []
        for i, ts in enumerate(times):
            if start - 1800 <= ts < end:
                h = {k: (fc.get(k) or [None] * len(times))[i] for k in ("temp", "rain_prob", "rain", "code", "wind")}
                if h["temp"] is not None:
                    out.append({**h, "ts": ts})
        return out

    @staticmethod
    def _tip(win, verdict, units) -> str:
        lines = [verdict, weather.condition(win.get("code"))]
        if win.get("rain_total"):
            lines.append(T("{mm} mm rain expected", mm=round(win["rain_total"], 1)))
        if win.get("wind"):
            lines.append(T("Wind up to {speed}", speed=weather.wind(win["wind"], units)))
        return "\n".join(x for x in lines if x)
