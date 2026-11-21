"""Translations: every text the user sees goes through T().

A language is one UTF-8 text file: "English text = Translation" per line, plus "@code" and
"@name" lines (see lang/_template.txt). Built-in languages live in the app's `lang` folder; anyone
can add one by dropping a file into %APPDATA%\\F1DockTimers\\lang (Settings > General > Language >
Open language folder). Lines left empty, or texts missing from a file, stay English.

Placeholders like {round} are filled in by T(text, round=...), after translating, so a translator
can move them around but must keep their names. "\\n" in a file is a line break.

The language is read once at start-up (before the UI modules build their texts); changing it in
Settings takes effect after a restart.
"""
from __future__ import annotations

import json
import locale
import logging
import os
import sys
from pathlib import Path

log = logging.getLogger(__name__)

_table: dict[str, str] = {}
_code = "en"


def T(english: str, /, **values) -> str:
    """Translated text, with {placeholders} filled in from ``values``."""
    out = _table.get(english) or english
    if values:
        try:
            out = out.format(**values)
        except (KeyError, IndexError, ValueError):     # a broken translation never breaks the UI
            out = english.format(**values)
    return out


def current() -> str:
    return _code


# ── files ────────────────────────────────────────────────────────────────────

def builtin_dir() -> Path:
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent.parent))
    return base / "lang"


def user_dir() -> Path:
    from .paths import config_dir
    return config_dir() / "lang"


def parse(text: str) -> tuple[dict[str, str], dict[str, str]]:
    """(meta, table) from a language file's contents."""
    meta: dict[str, str] = {}
    table: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.rstrip("\r")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line.startswith("@"):
            k, _, v = line[1:].partition("=")
            meta[k.strip()] = v.strip()
            continue
        k, sep, v = line.partition(" = ")
        if not sep:
            k, sep, v = line.partition("=")
        k, v = k.strip(), v.strip()
        if sep and k and v:
            table[k.replace("\\n", "\n")] = v.replace("\\n", "\n")
    return meta, table


def available() -> list[tuple[str, str, Path]]:
    """[(code, name, file)] of every language file found (user files override built-in ones)."""
    found: dict[str, tuple[str, str, Path]] = {}
    for folder in (builtin_dir(), user_dir()):
        try:
            files = sorted(folder.glob("*.txt"))
        except OSError:
            continue
        for f in files:
            if f.name.startswith("_"):
                continue                              # the template
            try:
                meta, _ = parse(f.read_text(encoding="utf-8-sig"))
            except OSError:
                continue
            code = meta.get("code") or f.stem
            found[code.lower()] = (code, meta.get("name") or code, f)
    return sorted(found.values(), key=lambda x: x[1].lower())


def windows_language() -> str:
    try:
        lang = locale.getlocale()[0] or ""
    except ValueError:
        lang = ""
    return lang.replace("_", "-").split("-")[0].lower() if lang else "en"


def load(choice: str) -> str:
    """Activate ``choice`` ("auto", "" or a language code). Returns the code in use."""
    global _table, _code
    _table, _code = {}, "en"
    want = (choice or "auto").lower()
    langs = available()
    pick = None
    if want != "auto":
        pick = next((x for x in langs if x[0].lower() == want), None)
    else:
        win = windows_language()
        pick = next((x for x in langs if x[0].lower() == win), None) or \
            next((x for x in langs if x[0].lower().split("-")[0] == win), None)
    if pick is None or pick[0].lower() == "en":
        return _code
    try:
        _, table = parse(pick[2].read_text(encoding="utf-8-sig"))
    except OSError:
        log.warning("could not read language file %s", pick[2])
        return _code
    _table, _code = table, pick[0]
    log.info("language: %s (%d texts)", pick[0], len(table))
    return _code


def _boot() -> None:
    """Read the chosen language straight from the settings file at import time."""
    try:
        from .paths import config_dir
        data = json.loads((config_dir() / "settings.json").read_text(encoding="utf-8"))
        load(str(data.get("language", "auto")))
    except (OSError, ValueError, TypeError):
        load("auto")


if not os.environ.get("F1DT_NO_I18N"):
    _boot()
