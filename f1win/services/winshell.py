"""Small Win32 helpers (ctypes) for the taskbar pill. All fail soft off Windows."""
from __future__ import annotations

import ctypes
import sys
from ctypes import wintypes

IS_WIN = sys.platform == "win32"

if IS_WIN:
    user32 = ctypes.windll.user32
    user32.FindWindowW.restype = wintypes.HWND
    user32.FindWindowExW.restype = wintypes.HWND
    user32.GetForegroundWindow.restype = wintypes.HWND
    user32.MonitorFromWindow.restype = wintypes.HANDLE

HWND_TOPMOST = -1
SWP_NOSIZE, SWP_NOMOVE, SWP_NOACTIVATE = 0x0001, 0x0002, 0x0010
MONITOR_DEFAULTTONEAREST = 2


class MONITORINFO(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("rcMonitor", wintypes.RECT),
                ("rcWork", wintypes.RECT), ("dwFlags", wintypes.DWORD)]


def _rect(hwnd) -> tuple[int, int, int, int] | None:
    r = wintypes.RECT()
    if hwnd and user32.GetWindowRect(hwnd, ctypes.byref(r)):
        return r.left, r.top, r.right, r.bottom
    return None


def taskbar_rects() -> dict | None:
    """Physical-pixel rects of the primary taskbar and its notification area."""
    if not IS_WIN:
        return None
    try:
        bar = user32.FindWindowW("Shell_TrayWnd", None)
        if not bar:
            return None
        tray = user32.FindWindowExW(bar, None, "TrayNotifyWnd", None)
        return {"bar": _rect(bar), "tray": _rect(tray)}
    except Exception:
        return None


def foreground_is_fullscreen() -> bool:
    """True when a game/video covers its whole monitor (then the pill hides)."""
    if not IS_WIN:
        return False
    try:
        fg = user32.GetForegroundWindow()
        if not fg:
            return False
        cls = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(fg, cls, 64)
        if cls.value in ("Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd"):
            return False
        r = _rect(fg)
        mon = user32.MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST)
        mi = MONITORINFO()
        mi.cbSize = ctypes.sizeof(MONITORINFO)
        if not r or not user32.GetMonitorInfoW(mon, ctypes.byref(mi)):
            return False
        m = mi.rcMonitor
        return r[0] <= m.left and r[1] <= m.top and r[2] >= m.right and r[3] >= m.bottom
    except Exception:
        return False


GW_HWNDNEXT = 2
DWMWA_CLOAKED = 14
_SKIP_CLASSES = {"tooltips_class32", "#32768", "Xaml_WindowedPopupClass", "NotifyIconOverflowWindow",
                 "TopLevelWindowForOverflowXamlIsland"}
_TASKBAR_CLASSES = {"Shell_TrayWnd", "Shell_SecondaryTrayWnd"}


def taskbar_is_visible_at(x: int, y: int) -> bool:
    """Is the taskbar really what sits under physical point (x, y)?

    Walks the top-level windows in z-order (our own process skipped) and checks
    the first visible one covering the point. A fullscreen or borderless game,
    a video, or any window over the taskbar there means the pill must hide,
    even when that window is not focused (e.g. game on this monitor while you
    type on another). Costs a few hundred cheap calls; it runs every 2 s.
    """
    if not IS_WIN:
        return True
    try:
        import os
        own = os.getpid()
        pid = wintypes.DWORD()
        cls = ctypes.create_unicode_buffer(64)
        cloaked = ctypes.c_int(0)
        dwm = ctypes.windll.dwmapi
        user32.GetTopWindow.restype = wintypes.HWND
        user32.GetWindow.restype = wintypes.HWND
        h = user32.GetTopWindow(None)
        for _ in range(2000):
            if not h:
                break
            if user32.IsWindowVisible(h) and not user32.IsIconic(h):
                user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
                r = _rect(h)
                if pid.value != own and r and r[0] <= x < r[2] and r[1] <= y < r[3]:
                    cloaked.value = 0
                    dwm.DwmGetWindowAttribute(h, DWMWA_CLOAKED, ctypes.byref(cloaked), ctypes.sizeof(cloaked))
                    user32.GetClassNameW(h, cls, 64)
                    if not cloaked.value and cls.value not in _SKIP_CLASSES:
                        return cls.value in _TASKBAR_CLASSES
            h = user32.GetWindow(h, GW_HWNDNEXT)
        return True
    except Exception:
        return True


HWND_NOTOPMOST = -2
SWP_SHOWWINDOW = 0x0040


def bring_to_front(hwnd: int) -> None:
    """Raise a normal window above everything once, then make it normal again."""
    if not IS_WIN or not hwnd:
        return
    try:
        h = wintypes.HWND(hwnd)
        flags = SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW
        user32.SetWindowPos(h, wintypes.HWND(HWND_TOPMOST), 0, 0, 0, 0, flags)
        user32.SetWindowPos(h, wintypes.HWND(HWND_NOTOPMOST), 0, 0, 0, 0, flags)
        user32.SetForegroundWindow(h)
    except Exception:
        pass


EVENT_SYSTEM_FOREGROUND = 0x0003
WINEVENT_OUTOFCONTEXT = 0x0000
WINEVENT_SKIPOWNPROCESS = 0x0002

if IS_WIN:
    WinEventProc = ctypes.WINFUNCTYPE(None, wintypes.HANDLE, wintypes.DWORD, wintypes.HWND, wintypes.LONG,
                                      wintypes.LONG, wintypes.DWORD, wintypes.DWORD)
    user32.SetWinEventHook.restype = wintypes.HANDLE
    user32.SetWinEventHook.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.HMODULE, WinEventProc,
                                       wintypes.DWORD, wintypes.DWORD, wintypes.DWORD]
    user32.UnhookWinEvent.argtypes = [wintypes.HANDLE]


class ForegroundWatch:
    """Calls `callback()` whenever another app's window becomes the foreground one.

    Clicking the taskbar or its tray chevron makes the taskbar foreground and lifts it
    above our topmost pill; this lets the pill raise itself again at once. The hook is
    out-of-context, so it runs on the Qt thread's message loop and costs nothing idle.
    """

    def __init__(self, callback) -> None:
        self._cb = callback
        self._proc = None
        self._hook = None
        if not IS_WIN:
            return
        try:
            self._proc = WinEventProc(lambda *_a: self._fire())
            self._hook = user32.SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, None, self._proc,
                                                0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS)
        except Exception:
            self._hook = None

    def _fire(self) -> None:
        try:
            self._cb()
        except Exception:
            pass

    @property
    def active(self) -> bool:
        return bool(self._hook)

    def close(self) -> None:
        if self._hook:
            try:
                user32.UnhookWinEvent(self._hook)
            except Exception:
                pass
        self._hook = None


def menu_is_open() -> bool:
    """A classic Win32 popup menu (class #32768, e.g. another app's tray menu) is on screen."""
    if not IS_WIN:
        return False
    try:
        h = user32.FindWindowW("#32768", None)
        return bool(h and user32.IsWindowVisible(h))
    except Exception:
        return False


def keep_topmost(hwnd: int) -> None:
    """Re-assert topmost: clicking the taskbar lifts it above other topmost windows."""
    if not IS_WIN or not hwnd:
        return
    try:
        user32.SetWindowPos(wintypes.HWND(hwnd), wintypes.HWND(HWND_TOPMOST), 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)
    except Exception:
        pass
