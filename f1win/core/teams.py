"""Team colours and flag names (shared.luau)."""
from __future__ import annotations

from ..i18n import T

from .sanitize import safe

# Approximate 2026 constructor colours keyed by Jolpica constructorId.
TEAM_COLORS = {
    "mercedes": "#27F4D2",
    "ferrari": "#E8002D",
    "mclaren": "#FF8000",
    "red_bull": "#3671C6",
    "rb": "#6692FF",
    "alpine": "#00A1E8",
    "haas": "#B6BABD",
    "audi": "#BB0A30",
    "williams": "#64C4FF",
    "aston_martin": "#229971",
    "cadillac": "#C4A24A",
    # historical / fallbacks
    "sauber": "#52E252",
    "alphatauri": "#5E8FAA",
    "alfa": "#C92D4B",
    "renault": "#FFF500",
}

FALLBACK = "#7A7D85"


def team_color(constructor_id: str, fallback: str = FALLBACK) -> str:
    return TEAM_COLORS.get(constructor_id or "", fallback)


FLAGS = {
    "GREEN": T('Green flag'),
    "CLEAR": T('Track clear'),
    "YELLOW": T('Yellow flag'),
    "DOUBLE YELLOW": T('Double yellow'),
    "RED": T('Red flag'),
    "CHEQUERED": T('Chequered flag'),
    "BLUE": T('Blue flag'),
    "SAFETY CAR": T('Safety car'),
    "VSC": T('Virtual safety car'),
    "VSC ENDING": "VSC ending",
}


def friendly_flag(flag: str | None) -> str:
    if not flag:
        return ""
    return FLAGS.get(flag, safe(flag, 24))
