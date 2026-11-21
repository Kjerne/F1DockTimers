"""Writes lang/_template.txt: every text the app shows, ready to translate.

    python tools/make_lang_template.py           (rewrite the template)
    python tools/make_lang_template.py --check   (exit 1 if it is out of date; used by the tests)

Texts are found by reading the source for T("...") calls, so a new text in the code only needs to
go through T() and this script picks it up.
"""
from __future__ import annotations

import ast
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


def texts() -> list[tuple[str, list[str]]]:
    """[(text, [file, ...])] in first-seen order (files sorted, then source order)."""
    seen: dict[str, list[str]] = {}
    for path in sorted((ROOT / "f1win").rglob("*.py")):
        rel = path.relative_to(ROOT).as_posix()
        tree = ast.parse(path.read_text(encoding="utf-8"))
        calls = [n for n in ast.walk(tree) if isinstance(n, ast.Call) and getattr(n.func, "id", "") == "T"
                 and n.args and isinstance(n.args[0], ast.Constant) and isinstance(n.args[0].value, str)]
        for n in sorted(calls, key=lambda c: (c.lineno, c.col_offset)):
            seen.setdefault(n.args[0].value, [])
            if rel not in seen[n.args[0].value]:
                seen[n.args[0].value].append(rel)
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
    if "--check" in sys.argv:
        old = TEMPLATE.read_text(encoding="utf-8") if TEMPLATE.exists() else ""
        if old != new:
            print("lang/_template.txt is out of date: run python tools/make_lang_template.py")
            return 1
        return 0
    TEMPLATE.parent.mkdir(exist_ok=True)
    TEMPLATE.write_text(new, encoding="utf-8", newline="\n")
    print(f"wrote {TEMPLATE.relative_to(ROOT)} ({len(texts())} texts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
