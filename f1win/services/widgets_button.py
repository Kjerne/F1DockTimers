"""Where is the Windows 11 Widgets (weather) button?

On a left-aligned Windows 11 taskbar it sits right next to the tray, exactly where the countdown
goes. It is drawn by XAML, so only UI Automation knows its position. UI Automation calls go to
Explorer and wait for its UI thread, and in embedded mode Explorer in turn waits for ours, so the
lookup runs on its own thread and the UI only ever reads the last result (never blocks).

Plain ctypes COM (no extra dependency): IUIAutomation -> ElementFromHandle(taskbar) ->
FindFirst(AutomationId == "WidgetsButton") -> CurrentBoundingRectangle, every few seconds.
"""
from __future__ import annotations

import ctypes
import logging
import sys
import threading
from ctypes import wintypes

log = logging.getLogger(__name__)

IS_WIN = sys.platform == "win32"
UIA_AutomationIdPropertyId = 30011
TreeScope_Descendants = 4
VT_BSTR = 8
COINIT_MULTITHREADED = 0
CLSCTX_INPROC_SERVER = 1
POLL_S = 5.0

# vtable slots (UIAutomationClient.h)
IUIA_ElementFromHandle, IUIA_CreatePropertyCondition = 6, 23
ELEM_FindFirst, ELEM_CurrentBoundingRectangle = 5, 43
RELEASE = 2


class GUID(ctypes.Structure):
    _fields_ = [("d1", wintypes.DWORD), ("d2", wintypes.WORD), ("d3", wintypes.WORD), ("d4", ctypes.c_ubyte * 8)]

    @classmethod
    def parse(cls, s: str) -> "GUID":
        import uuid
        u = uuid.UUID(s)
        g = cls(u.time_low, u.time_mid, u.time_hi_version)
        g.d4[:] = list(u.bytes[8:])
        return g


class VARIANT(ctypes.Structure):
    _fields_ = [("vt", ctypes.c_ushort), ("r1", ctypes.c_ushort), ("r2", ctypes.c_ushort), ("r3", ctypes.c_ushort),
                ("val", ctypes.c_void_p), ("pad", ctypes.c_void_p)]


CLSID_CUIAutomation = "ff48dba4-60ef-4201-aa87-54103eef594e"
IID_IUIAutomation = "30cbe57d-d9d0-452a-ab13-7ac5ac4825ee"


def _method(obj, slot, restype, *argtypes):
    vtbl = ctypes.cast(obj, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    return ctypes.WINFUNCTYPE(restype, ctypes.c_void_p, *argtypes)(vtbl[slot])


def _release(obj) -> None:
    if obj:
        _method(obj, RELEASE, ctypes.c_ulong)(obj)


class WidgetsButtonWatch:
    """Background lookup of the Widgets button rectangle (physical px) on the primary taskbar."""

    def __init__(self) -> None:
        self._rect: tuple[int, int, int, int] | None = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        if not IS_WIN or self._thread is not None:
            return
        self._thread = threading.Thread(target=self._run, name="widgets-button", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def rect(self) -> tuple[int, int, int, int] | None:
        with self._lock:
            return self._rect

    def _set(self, r) -> None:
        with self._lock:
            self._rect = r

    def _run(self) -> None:
        ole32, oleaut32, user32 = ctypes.windll.ole32, ctypes.windll.oleaut32, ctypes.windll.user32
        user32.FindWindowW.restype = wintypes.HWND
        oleaut32.SysAllocString.restype = ctypes.c_void_p
        oleaut32.SysAllocString.argtypes = [wintypes.LPCWSTR]
        oleaut32.SysFreeString.argtypes = [ctypes.c_void_p]
        ole32.CoInitializeEx(None, COINIT_MULTITHREADED)
        uia = ctypes.c_void_p()
        hr = ole32.CoCreateInstance(ctypes.byref(GUID.parse(CLSID_CUIAutomation)), None, CLSCTX_INPROC_SERVER,
                                    ctypes.byref(GUID.parse(IID_IUIAutomation)), ctypes.byref(uia))
        if hr != 0 or not uia:
            log.info("UI Automation unavailable (%#x); Widgets button not tracked", hr & 0xFFFFFFFF)
            return
        bstr = oleaut32.SysAllocString("WidgetsButton")
        var = VARIANT(VT_BSTR, 0, 0, 0, bstr, None)
        cond = ctypes.c_void_p()
        try:
            hr = _method(uia, IUIA_CreatePropertyCondition, ctypes.c_long, ctypes.c_int, VARIANT,
                         ctypes.POINTER(ctypes.c_void_p))(uia, UIA_AutomationIdPropertyId, var, ctypes.byref(cond))
            if hr != 0 or not cond:
                return
            while not self._stop.is_set():
                self._set(self._lookup(uia, cond, user32))
                self._stop.wait(POLL_S)
        except Exception:                                   # never take the app down for this
            log.exception("Widgets button lookup failed")
        finally:
            _release(cond)
            oleaut32.SysFreeString(bstr)
            _release(uia)

    @staticmethod
    def _lookup(uia, cond, user32):
        bar = user32.FindWindowW("Shell_TrayWnd", None)
        if not bar:
            return None
        root, btn = ctypes.c_void_p(), ctypes.c_void_p()
        try:
            if _method(uia, IUIA_ElementFromHandle, ctypes.c_long, wintypes.HWND,
                       ctypes.POINTER(ctypes.c_void_p))(uia, bar, ctypes.byref(root)) != 0 or not root:
                return None
            if _method(root, ELEM_FindFirst, ctypes.c_long, ctypes.c_int, ctypes.c_void_p,
                       ctypes.POINTER(ctypes.c_void_p))(root, TreeScope_Descendants, cond, ctypes.byref(btn)) != 0 \
                    or not btn:
                return None
            r = wintypes.RECT()
            if _method(btn, ELEM_CurrentBoundingRectangle, ctypes.c_long,
                       ctypes.POINTER(wintypes.RECT))(btn, ctypes.byref(r)) != 0:
                return None
            if r.right <= r.left or r.bottom <= r.top:
                return None                                  # hidden / turned off
            return r.left, r.top, r.right, r.bottom
        finally:
            _release(btn)
            _release(root)


_watch: WidgetsButtonWatch | None = None


def widgets_button_rect() -> tuple[int, int, int, int] | None:
    """Last known rectangle (or None). Starts the background lookup on first use."""
    global _watch
    if _watch is None:
        _watch = WidgetsButtonWatch()
        _watch.start()
    return _watch.rect()
