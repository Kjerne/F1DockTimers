"""Filesystem locations. Settings roam (%APPDATA%), cache stays local (%LOCALAPPDATA%)."""
from __future__ import annotations

import os
import sys
from pathlib import Path

APP_NAME = "F1DockTimers"
LEGACY_NAME = "NoctaliaF1"   # before the rename; migrated once


def _base(env: str, fallback: str) -> Path:
    root = Path(os.environ.get(env) or os.path.join(os.path.expanduser("~"), fallback))
    new, old = root / APP_NAME, root / LEGACY_NAME
    if not new.exists() and old.is_dir():
        # Renamed from "Noctalia F1": keep the user's settings and cache.
        import shutil
        try:
            shutil.copytree(old, new)
        except OSError:
            pass
    return new


def config_dir() -> Path:
    override = os.environ.get("NOCTALIA_F1_HOME")
    p = Path(override) / "config" if override else _base("APPDATA", "AppData/Roaming")
    p.mkdir(parents=True, exist_ok=True)
    return p


def cache_dir() -> Path:
    override = os.environ.get("NOCTALIA_F1_HOME")
    p = Path(override) / "cache" if override else _base("LOCALAPPDATA", "AppData/Local")
    p.mkdir(parents=True, exist_ok=True)
    return p


def resource_dir() -> Path:
    """Bundled assets: next to the frozen exe (PyInstaller) or inside the package."""
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent)) / "f1win" / "assets"
    return Path(__file__).resolve().parent / "assets"
