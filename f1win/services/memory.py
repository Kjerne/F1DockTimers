"""Give idle memory back to Windows.

Python and Qt touch a lot of memory while starting up, loading data or showing the panel, and
Windows keeps those pages in the app's working set (what Task Manager shows) until it needs them
elsewhere. Once the app sits idle in the tray, EmptyWorkingSet hands them back right away: Task
Manager drops from ~55 MB to under 10 MB, and the few pages the countdown needs come back on their
own. Nothing is lost; opening the panel just pages its parts back in.
"""
from __future__ import annotations

import ctypes
import logging
import sys

log = logging.getLogger(__name__)


class _Counters(ctypes.Structure):
    _fields_ = [("cb", ctypes.c_uint32), ("PageFaultCount", ctypes.c_uint32), ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t)]


def working_set_mb() -> float:
    """Current working set in MB (0 when unknown)."""
    if sys.platform != "win32":
        return 0.0
    try:
        k32, psapi = ctypes.windll.kernel32, ctypes.windll.psapi
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        psapi.GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.POINTER(_Counters), ctypes.c_uint32]
        c = _Counters()
        c.cb = ctypes.sizeof(c)
        if psapi.GetProcessMemoryInfo(k32.GetCurrentProcess(), ctypes.byref(c), c.cb):
            return c.WorkingSetSize / (1024 * 1024)
    except (AttributeError, OSError):
        pass
    return 0.0


def trim() -> bool:
    if sys.platform != "win32":
        return False
    try:
        k32, psapi = ctypes.windll.kernel32, ctypes.windll.psapi
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        psapi.EmptyWorkingSet.argtypes = [ctypes.c_void_p]
        before = working_set_mb()
        ok = bool(psapi.EmptyWorkingSet(k32.GetCurrentProcess()))
        log.info("memory trimmed: %.0f MB -> %.0f MB", before, working_set_mb())
        return ok
    except (AttributeError, OSError):
        return False
