"""The whole app starts (tray, panel, timers, services) and renders a tab without errors."""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_app_starts_and_renders(tmp_path):
    env = dict(os.environ, NOCTALIA_F1_HOME=str(tmp_path / "home"), QT_QPA_PLATFORM="offscreen")
    out = tmp_path / "shots"
    r = subprocess.run([sys.executable, str(ROOT / "launch.pyw"), "--screenshots", str(out), "--tabs", "schedule"],
                       env=env, capture_output=True, text=True, timeout=120)
    log = (tmp_path / "home" / "cache" / "f1.log")
    text = log.read_text(encoding="utf-8") if log.exists() else ""
    assert r.returncode == 0, r.stderr[-2000:]
    assert "uncaught error" not in text, text[-2000:]
    assert (out / "schedule.png").exists()
