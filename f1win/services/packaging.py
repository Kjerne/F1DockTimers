"""Is this copy the Microsoft Store (MSIX) version?

The Store version is signed by Microsoft and updated by the Store, runs in an app container
(its AppData is redirected by Windows) and starts with Windows through a StartupTask declared in
its package manifest instead of the Run registry key.
"""
from __future__ import annotations

import ctypes
import functools
import sys

STARTUP_TASK_ID = "F1DockTimersStartup"     # must match build/store/AppxManifest.xml
APPMODEL_ERROR_NO_PACKAGE = 15700


@functools.lru_cache(maxsize=1)
def package_family_name() -> str:
    """The package family name when running packaged, else ""."""
    if sys.platform != "win32":
        return ""
    try:
        k32 = ctypes.windll.kernel32
        n = ctypes.c_uint32(0)
        rc = k32.GetCurrentPackageFamilyName(ctypes.byref(n), None)
        if rc == APPMODEL_ERROR_NO_PACKAGE or n.value == 0:
            return ""
        buf = ctypes.create_unicode_buffer(n.value)
        if k32.GetCurrentPackageFamilyName(ctypes.byref(n), buf) != 0:
            return ""
        return buf.value
    except (AttributeError, OSError):
        return ""


def is_packaged() -> bool:
    return bool(package_family_name())


def startup_task_enabled() -> bool:
    """State of the package's StartupTask as Windows stores it (2 = enabled, 4 = enabled by policy)."""
    family = package_family_name()
    if not family:
        return False
    try:
        import winreg
        key = (r"Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel"
               rf"\SystemAppData\{family}\{STARTUP_TASK_ID}")
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key) as k:
            state, _ = winreg.QueryValueEx(k, "State")
        return int(state) in (2, 4)
    except OSError:
        return True          # never toggled yet: the manifest enables it after the first launch


def open_startup_settings() -> None:
    """Windows owns the Store app's start-up switch: Settings > Apps > Startup."""
    import os
    try:
        os.startfile("ms-settings:startupapps")
    except OSError:
        pass
