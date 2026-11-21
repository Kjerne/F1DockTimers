"""Start with Windows via HKCU\\...\\Run (per user, no admin rights)."""
from __future__ import annotations

import os
import sys

RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
VALUE = "F1DockTimers"
LEGACY_VALUE = "NoctaliaF1"


def command() -> str:
    if getattr(sys, "frozen", False):
        return f'"{sys.executable}" --minimized'
    exe = sys.executable
    pyw = os.path.join(os.path.dirname(exe), "pythonw.exe")
    launcher = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                            "launch.pyw")
    return f'"{pyw if os.path.exists(pyw) else exe}" "{launcher}" --minimized'


def _has(name: str) -> bool:
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as k:
            winreg.QueryValueEx(k, name)
            return True
    except OSError:
        return False


def is_enabled() -> bool:
    if sys.platform != "win32":
        return False
    from . import packaging
    if packaging.is_packaged():
        return packaging.startup_task_enabled()
    return _has(VALUE) or _has(LEGACY_VALUE)


def set_enabled(on: bool, interactive: bool = False) -> bool:
    """Turn start-up on/off. ``interactive``: the user asked (Store version: open Windows' page)."""
    if sys.platform != "win32":
        return False
    from . import packaging
    if packaging.is_packaged():
        if interactive and on != is_enabled():
            packaging.open_startup_settings()
        return True
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY, 0, winreg.KEY_SET_VALUE) as k:
            try:
                winreg.DeleteValue(k, LEGACY_VALUE)   # old name, replaced below
            except FileNotFoundError:
                pass
            if on:
                winreg.SetValueEx(k, VALUE, 0, winreg.REG_SZ, command())
            else:
                try:
                    winreg.DeleteValue(k, VALUE)
                except FileNotFoundError:
                    pass
        return True
    except OSError:
        return False
