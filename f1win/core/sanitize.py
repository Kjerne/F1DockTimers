"""Remote-data hygiene, ported from shared.luau (safe, bounded, openLink)."""
from __future__ import annotations

import re
from urllib.parse import urlsplit

MAX_FIELD_CHARS = 72
MAX_BODY_BYTES = 1_048_576

# C0/C1 controls, DEL, Arabic letter mark, zero-width and bidi controls, BOM.
_STRIP = re.compile(
    "[\x00-\x1f\x7f\x80-\x9f؜​-‏‪-‮⁠-⁤⁦-⁯﻿]"
)

_LINK_HOSTS = {"en.wikipedia.org", "www.wikipedia.org", "wikipedia.org"}


def safe(value, limit: int = MAX_FIELD_CHARS) -> str:
    """Clamp a remote value to printable text of at most ``limit`` characters."""
    text = "" if value is None else str(value)
    text = _STRIP.sub(" ", text).strip()
    if len(text) > limit:
        text = text[:limit] + "…"
    return text


def bounded(items, cap: int) -> list:
    if not isinstance(items, list):
        return []
    return items[:cap]


def path(obj, *keys):
    """Safe nested read: path(d, "a", "b") -> d["a"]["b"] or None."""
    cur = obj
    for key in keys:
        if isinstance(cur, dict):
            cur = cur.get(key)
        elif isinstance(cur, list) and isinstance(key, int) and -len(cur) <= key < len(cur):
            cur = cur[key]
        else:
            return None
    return cur


def safe_link(url) -> str | None:
    """Wikipedia links only, https (http upgraded), no userinfo or port."""
    raw = str(url or "")
    if len(raw) > 400 or re.search(r"[\x00-\x20]", raw):
        return None
    if raw.startswith("http://"):
        raw = "https://" + raw[7:]
    if not raw.startswith("https://"):
        return None
    try:
        parts = urlsplit(raw)
    except ValueError:
        return None
    host = parts.netloc
    if not host or "@" in host or ":" in host:
        return None
    if host.lower() not in _LINK_HOSTS:
        return None
    return raw


def hex_color(raw) -> str | None:
    """OpenF1 / F1 feed bare hex "27F4D2" -> "#27F4D2"."""
    h = str(raw or "").strip().lstrip("#")
    if re.fullmatch(r"[0-9a-fA-F]{6}", h):
        return "#" + h.upper()
    return None
