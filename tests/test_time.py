from datetime import datetime, timezone

from f1win.core import timeutil as tu


def utc(*a):
    return int(datetime(*a, tzinfo=timezone.utc).timestamp())


def test_parse_utc_jolpica():
    assert tu.parse_utc("2026-11-22", "04:00:00Z") == utc(2026, 11, 22, 4)
    assert tu.parse_utc("2026-11-22", None) == utc(2026, 11, 22)
    assert tu.parse_utc("bad", "04:00:00Z") is None


def test_parse_iso_with_offset():
    assert tu.parse_iso("2026-10-10T08:17:42.627000+00:00") == utc(2026, 10, 10, 8, 17, 42)
    assert tu.parse_iso("2026-10-10T10:00:00+02:00") == utc(2026, 10, 10, 8)
    assert tu.parse_iso("2026-10-10T03:00:00-05:00") == utc(2026, 10, 10, 8)


def test_parse_local_feed():
    # São Paulo race: 14:00 local at GMT-3 is 17:00 UTC.
    assert tu.parse_local("2026-11-08T14:00:00", "-03:00:00") == utc(2026, 11, 8, 17)
    assert tu.parse_local("2026-10-10T21:30:00", "08:00:00") == utc(2026, 10, 10, 13, 30)
    assert tu.parse_local("2026-10-10T21:30:00", "garbage") is None


def test_countdown_formats():
    assert tu.fmt_long(4 * 86400 + 10 * 3600 + 50 * 60 + 30) == "in 4d 10h 50m"
    assert tu.fmt_long(4 * 3600) == "in 4h 0m"
    assert tu.fmt_long(59) == "in 0m"
    assert tu.fmt_long(0) == "live now"
    assert tu.fmt_short(3 * 86400) == "3d"
    assert tu.fmt_short(5 * 3600 + 1) == "5h"
    assert tu.fmt_short(30) == "<1m"
    assert tu.fmt_short(-5) == "live"


def test_copenhagen_conversion_and_dst():
    cph = tu.zone_for("Europe/Copenhagen")
    # Winter (CET, UTC+1): Las Vegas FP1 00:30Z -> Fri 01:30
    assert tu.fmt_datetime(utc(2026, 11, 20, 0, 30), cph, False) == "Fri 01:30"
    # Summer (CEST, UTC+2): 13:00Z -> 15:00
    assert tu.fmt_datetime(utc(2026, 7, 5, 13), cph, False) == "Sun 15:00"
    # Across the DST change on 2026-10-25 01:00Z
    assert tu.fmt_datetime(utc(2026, 10, 25, 0, 30), cph, False) == "Sun 02:30"
    assert tu.fmt_datetime(utc(2026, 10, 25, 1, 30), cph, False) == "Sun 02:30"
    assert tu.utc_offset_label(utc(2026, 7, 1), cph) == "UTC+02:00"
    assert tu.utc_offset_label(utc(2026, 12, 1), cph) == "UTC+01:00"


def test_twelve_hour_and_long():
    cph = tu.zone_for("Europe/Copenhagen")
    assert tu.fmt_datetime(utc(2026, 11, 22, 4), cph, True) == "Sun 05:00 AM"
    assert tu.fmt_datetime(utc(2026, 11, 22, 13), cph, True, long=True) == "Sun 22 Nov, 02:00 PM"


def test_other_zones_and_fallback():
    ny = tu.zone_for("America/New_York")
    assert tu.fmt_datetime(utc(2026, 11, 22, 4), ny, False) == "Sat 23:00"
    assert tu.zone_for("auto") is None
    assert tu.zone_for("Not/AZone") is None
    # System zone path works and returns a string
    assert isinstance(tu.fmt_datetime(utc(2026, 11, 22, 4), None, False), str)


def test_clock():
    assert tu.fmt_clock(7 * 60 + 45) == "07:45"
    assert tu.fmt_clock(3725) == "1:02:05"
    assert tu.fmt_clock(-3) == "00:00"
