import json
import os
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
FIX = Path(__file__).parent / "fixtures"


@pytest.fixture
def fx():
    def load(name):
        return json.loads((FIX / name).read_text(encoding="utf-8-sig"))
    return load


@pytest.fixture(autouse=True)
def isolated_home(tmp_path, monkeypatch):
    monkeypatch.setenv("NOCTALIA_F1_HOME", str(tmp_path / "home"))
    monkeypatch.delenv("OPENF1_USERNAME", raising=False)
    monkeypatch.delenv("OPENF1_PASSWORD", raising=False)
    yield


@pytest.fixture(scope="session")
def qapp():
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PySide6.QtWidgets import QApplication
    app = QApplication.instance() or QApplication([])
    yield app
