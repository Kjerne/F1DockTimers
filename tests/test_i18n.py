"""Language files, T() and the template."""
import subprocess
import sys
from pathlib import Path

from f1win import i18n

ROOT = Path(__file__).resolve().parents[1]


def test_parse_language_file():
    meta, table = i18n.parse("# comment\n@code = xx\n@name = Testish\nSchedule = Skema\nEmpty = \n"
                             "in {time} = om {time}\n" r"Two\nlines = To\nlinjer" "\n")
    assert meta == {"code": "xx", "name": "Testish"}
    assert table == {"Schedule": "Skema", "in {time}": "om {time}", "Two\nlines": "To\nlinjer"}   # empty = English


def test_translate_with_placeholders_and_fallbacks(monkeypatch):
    monkeypatch.setattr(i18n, "_table", {"in {time}": "om {time}", "Broken {x}": "Ødelagt {y}"})
    assert i18n.T("in {time}", time="2d 3h") == "om 2d 3h"
    assert i18n.T("Untranslated {n}", n=5) == "Untranslated 5"
    assert i18n.T("Broken {x}", x=1) == "Broken 1"          # a bad translation never breaks the UI


def test_user_language_file_is_found_and_loaded(tmp_path, monkeypatch):
    monkeypatch.setattr(i18n, "user_dir", lambda: tmp_path)
    (tmp_path / "xx.txt").write_text("@code = xx\n@name = Testish\nSchedule = Skema\n", encoding="utf-8")
    assert ("xx", "Testish") in [(c, n) for c, n, _ in i18n.available()]
    try:
        assert i18n.load("xx") == "xx" and i18n.T("Schedule") == "Skema"
        assert i18n.load("auto") in ("en", "xx")
    finally:
        i18n.load("en")
    assert i18n.T("Schedule") == "Schedule"


def test_only_english_is_built_in():
    codes = [c for c, _, f in i18n.available() if f.parent == i18n.builtin_dir()]
    assert codes == ["en"]


def test_template_is_up_to_date():
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "make_lang_template.py"), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout
