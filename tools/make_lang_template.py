"""Writes lang/_template.txt: every text the app shows, ready to translate.

    python tools/make_lang_template.py           (rewrite the template)
    python tools/make_lang_template.py --check   (exit 1 if it is out of date; run by ctest)

Port of the Python app's tool for the C++ sources. Texts are found by reading src/ for
T(QStringLiteral("...")), the settings dialog's tr("...") and F1_TR_NOOP("...") (texts kept in
arrays that reach T() later), so a new text only needs to go through one of those.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEMPLATE = ROOT / "lang" / "_template.txt"

HEADER = """\
# F1DockTimers translation template.
#
# To add a language:
#  1. Copy this file to <code>.txt, for example "da.txt" or "pt-BR.txt".
#  2. Fill in @code (the language code) and @name (the language's name in its own words).
#  3. Write the translation after " = " on each line. Lines left empty stay English.
#  Keep {placeholders} exactly as they are (you may move them); the app fills them in.
#  "\\n" is a line break. The file is plain UTF-8 text.
#
# Try it: put the file in %APPDATA%\\F1DockTimers\\lang (Settings > General > Language >
# Open language folder), pick the language and restart the app.
# Share it as a pull request (into the lang folder) so everyone gets it.
@code =
@name =
"""

LIT = r'"(?:[^"\\\n]|\\.)*"'
# One or more adjacent literals ("a" "b" is one string in C++).
LITS = rf'((?:{LIT}\s*)+)'
CALL = re.compile(rf'(?:\bT\(\s*QStringLiteral\(\s*|(?<![\w:])tr\(\s*|\bF1_TR_NOOP\(\s*){LITS}\)')
ESCAPES = {"n": "\n", "t": "\t", '"': '"', "'": "'", "\\": "\\"}


def unescape(lits: str) -> str:
    out = []
    for lit in re.findall(LIT, lits):
        body, i = lit[1:-1], 0
        while i < len(body):
            c = body[i]
            if c != "\\":
                out.append(c)
                i += 1
                continue
            nxt = body[i + 1]
            if nxt == "u":
                out.append(chr(int(body[i + 2:i + 6], 16)))
                i += 6
            else:
                out.append(ESCAPES.get(nxt, nxt))
                i += 2
    return "".join(out)


def strip_comments(src: str) -> str:
    """Blank out // and /* */ comments (keeping string literals and positions intact)."""
    pattern = re.compile(rf'{LIT}|//[^\n]*|/\*.*?\*/', re.S)
    return pattern.sub(lambda m: m.group(0) if m.group(0).startswith('"') else " " * len(m.group(0)), src)


def texts() -> list[tuple[str, list[str]]]:
    """[(text, [file, ...])] in first-seen order (files sorted, then source order)."""
    seen: dict[str, list[str]] = {}
    files = sorted(p for p in (ROOT / "src").rglob("*") if p.suffix in (".cpp", ".h"))
    for path in files:
        rel = path.relative_to(ROOT).as_posix()
        src = strip_comments(path.read_text(encoding="utf-8"))
        for m in CALL.finditer(src):
            text = unescape(m.group(1))
            if path.name == "i18n.h":   # the marker's own definition
                continue
            seen.setdefault(text, [])
            if rel not in seen[text]:
                seen[text].append(rel)
    return list(seen.items())


def render() -> str:
    out = [HEADER]
    last_file = None
    for text, files in texts():
        if files[0] != last_file:
            last_file = files[0]
            out.append(f"\n# {last_file}\n")
        out.append(text.replace("\n", "\\n") + " = \n")
    return "".join(out)


def main() -> int:
    new = render()
    old = TEMPLATE.read_text(encoding="utf-8") if TEMPLATE.exists() else ""
    if "--check" in sys.argv:
        if new != old:
            print("lang/_template.txt is out of date: run python tools/make_lang_template.py")
            return 1
        return 0
    TEMPLATE.write_text(new, encoding="utf-8", newline="\n")
    print(f"wrote {TEMPLATE.relative_to(ROOT)} ({len(texts())} texts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
