"""Time parsing and formatting, ported from shared.luau.

All instants are handled as integer Unix seconds (UTC). Conversion to the
user's zone happens only at display time, through ``zone_for``.
"""
from __future__ import annotations

from ..i18n import T

import re
import time
from datetime import datetime, timedelta, timezone, tzinfo

try:
    from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
except ImportError:  # pragma: no cover
    ZoneInfo = None  # type: ignore
    ZoneInfoNotFoundError = Exception  # type: ignore

_DATE = re.compile(r"^(\d{4})-(\d{2})-(\d{2})")
_TIME = re.compile(r"^(\d{2}):(\d{2}):?(\d*)")
_ISO = re.compile(r"^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})")
_OFFSET_TAIL = re.compile(r"([+-])(\d{2}):(\d{2})$")
_GMT = re.compile(r"^([+-]?)(\d{2}):(\d{2})")

WEEKDAYS = [T("Mon"), T("Tue"), T("Wed"), T("Thu"), T("Fri"), T("Sat"), T("Sun")]
MONTHS = [T("Jan"), T("Feb"), T("Mar"), T("Apr"), T("May"), T("Jun"), T("Jul"), T("Aug"), T("Sep"), T("Oct"),
          T("Nov"), T("Dec")]


def now_sec() -> int:
    return int(time.time())


def _epoch(y: int, mo: int, d: int, hh: int = 0, mm: int = 0, ss: int = 0) -> int:
    return int(datetime(y, mo, d, hh, mm, ss, tzinfo=timezone.utc).timestamp())


def parse_utc(date_str, time_str) -> int | None:
    """Jolpica "2026-10-11" + "12:00:00Z" -> Unix seconds. Missing time = midnight UTC."""
    m = _DATE.match(str(date_str or ""))
    if not m:
        return None
    t = _TIME.match(str(time_str or "00:00:00Z"))
    hh, mm, ss = (int(t.group(1)), int(t.group(2)), int(t.group(3) or 0)) if t else (0, 0, 0)
    try:
        return _epoch(int(m.group(1)), int(m.group(2)), int(m.group(3)), hh, mm, ss)
    except ValueError:
        return None


def parse_iso(text) -> int | None:
    """OpenF1 "2026-10-10T08:17:42.627000+00:00" -> Unix seconds."""
    s = str(text or "")
    m = _ISO.match(s)
    if not m:
        return None
    try:
        t = _epoch(*(int(g) for g in m.groups()))
    except ValueError:
        return None
    off = _OFFSET_TAIL.search(s)
    if off:
        offset = int(off.group(2)) * 3600 + int(off.group(3)) * 60
        t = t - offset if off.group(1) == "+" else t + offset
    return t


def parse_local(date_time, gmt_offset) -> int | None:
    """F1 timing feed: local wall clock + circuit GMT offset -> Unix seconds."""
    m = _ISO.match(str(date_time or ""))
    o = _GMT.match(str(gmt_offset or ""))
    if not m or not o:
        return None
    offset = int(o.group(2)) * 3600 + int(o.group(3)) * 60
    if o.group(1) == "-":
        offset = -offset
    try:
        return _epoch(*(int(g) for g in m.groups())) - offset
    except ValueError:
        return None


def _split(seconds: float) -> tuple[int, int, int]:
    s = int(seconds)
    d, s = divmod(s, 86400)
    h, s = divmod(s, 3600)
    return d, h, s // 60


def fmt_short(seconds: float) -> str:
    """Bar pill form: 4d, 3h, 12m, <1m, live."""
    if seconds <= 0:
        return T("live")
    d, h, m = _split(seconds)
    if d > 0:
        return T("{days}d", days=d)
    if h > 0:
        return T("{hours}h", hours=h)
    if m > 0:
        return T("{minutes}m", minutes=m)
    return T("<1m")


def fmt_long(seconds: float) -> str:
    """"in 4d 21h 36m", or "live now" once the time has come."""
    if seconds <= 0:
        return T("live now")
    d, h, m = _split(seconds)
    parts = []
    if d > 0:
        parts.append(T("{days}d", days=d))
    if h > 0:
        parts.append(T("{hours}h", hours=h))
    parts.append(T("{minutes}m", minutes=m))
    return T("in {time}", time=" ".join(parts))


def fmt_clock(seconds: float) -> str:
    s = max(0, int(seconds))
    if s >= 3600:
        return f"{s // 3600}:{(s % 3600) // 60:02d}:{s % 60:02d}"
    return f"{s // 60:02d}:{s % 60:02d}"


# ── Zones ────────────────────────────────────────────────────────────────────

def system_zone() -> tzinfo:
    """The Windows zone, including its DST rules, via the C runtime."""
    return datetime.now().astimezone().tzinfo or timezone.utc


def system_zone_name() -> str:
    """IANA name of the Windows zone, e.g. "Europe/Copenhagen" (not "Romance Standard Time")."""
    try:
        from PySide6.QtCore import QTimeZone
        name = bytes(QTimeZone.systemTimeZoneId()).decode()
        if name:
            return name
    except Exception:
        pass
    try:
        import tzlocal  # optional
        return str(tzlocal.get_localzone_name())
    except Exception:
        pass
    tz = datetime.now().astimezone().tzinfo
    return str(tz.tzname(None) if tz else "UTC")


def zone_for(name: str | None) -> tzinfo | None:
    """None means "use the system zone" (handled per timestamp for correct DST)."""
    if not name or name == "auto":
        return None
    if ZoneInfo is None:
        return None
    try:
        return ZoneInfo(name)
    except (ZoneInfoNotFoundError, ValueError, OSError):
        return None


def to_local(ts: int, zone: tzinfo | None) -> datetime:
    utc = datetime.fromtimestamp(ts, tz=timezone.utc)
    if zone is None:
        return utc.astimezone()  # system zone, DST resolved for that instant
    return utc.astimezone(zone)


def fmt_datetime(ts: int, zone: tzinfo | None, twelve: bool, long: bool = False) -> str:
    """"Fri 10:30" / "Fri 10:30 AM"; long adds the date: "Fri 20 Nov, 10:30"."""
    dt = to_local(ts, zone)
    wd = WEEKDAYS[dt.weekday()]
    if twelve:
        h12 = dt.hour % 12 or 12
        clock = f"{h12:02d}:{dt.minute:02d} {'AM' if dt.hour < 12 else 'PM'}"
    else:
        clock = f"{dt.hour:02d}:{dt.minute:02d}"
    if long:
        return f"{wd} {dt.day:02d} {MONTHS[dt.month - 1]}, {clock}"
    return f"{wd} {clock}"


def fmt_clock_time(ts: int, zone: tzinfo | None, twelve: bool) -> str:
    """"14:00" / "2:00 PM"."""
    dt = to_local(ts, zone)
    if twelve:
        return f"{dt.hour % 12 or 12}:{dt.minute:02d} {'AM' if dt.hour < 12 else 'PM'}"
    return f"{dt.hour:02d}:{dt.minute:02d}"


def fmt_date(ts: int, zone: tzinfo | None) -> str:
    dt = to_local(ts, zone)
    return f"{WEEKDAYS[dt.weekday()]} {dt.day} {MONTHS[dt.month - 1]} {dt.year}"


def fmt_stamp(ts: float, zone: tzinfo | None, twelve: bool) -> str:
    """Short "updated" stamp: "18:42" today, else "17 Nov 18:42"."""
    if not ts:
        return T("never")
    dt = to_local(int(ts), zone)
    now = to_local(now_sec(), zone)
    if twelve:
        clock = f"{dt.hour % 12 or 12}:{dt.minute:02d} {'AM' if dt.hour < 12 else 'PM'}"
    else:
        clock = f"{dt.hour:02d}:{dt.minute:02d}"
    if dt.date() == now.date():
        return clock
    return f"{dt.day} {MONTHS[dt.month - 1]} {clock}"


def utc_offset_label(ts: int, zone: tzinfo | None) -> str:
    dt = to_local(ts, zone)
    off = dt.utcoffset() or timedelta(0)
    mins = int(off.total_seconds() // 60)
    sign = "+" if mins >= 0 else "-"
    mins = abs(mins)
    return f"UTC{sign}{mins // 60:02d}:{mins % 60:02d}"
