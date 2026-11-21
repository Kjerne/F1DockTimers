"""A layered child window inside the Windows taskbar (Shell_TrayWnd).

Living inside the taskbar instead of floating over it means Windows handles the
hard cases for us: the countdown hides with the taskbar (fullscreen games, auto-
hide), menus and flyouts always open above it, and clicking the taskbar can't
bury it. The window is plain Win32 (ctypes); the picture is painted with Qt into
a QImage and handed over with UpdateLayeredWindow, so the look stays identical.

Caveat that shapes this module: a child window of another process shares its
input queue with Explorer, so Explorer waits for our UI thread whenever it talks
to the window. Everything here is short and non-blocking, and slow work in the
app (update verification) runs on worker threads.
"""
from __future__ import annotations

import ctypes
import logging
import sys
from ctypes import wintypes

log = logging.getLogger(__name__)

IS_WIN = sys.platform == "win32"

WS_CHILD, WS_CLIPSIBLINGS = 0x40000000, 0x04000000
WS_EX_LAYERED, WS_EX_NOACTIVATE, WS_EX_TRANSPARENT = 0x00080000, 0x08000000, 0x00000020
CS_DBLCLKS = 0x0008
SWP_NOSIZE, SWP_NOMOVE, SWP_NOACTIVATE, SWP_SHOWWINDOW = 0x0001, 0x0002, 0x0010, 0x0040
SW_HIDE = 0
GW_HWNDPREV = 3
ULW_ALPHA = 0x02
AC_SRC_OVER, AC_SRC_ALPHA = 0x00, 0x01
DIB_RGB_COLORS = 0
TME_LEAVE = 0x02
HWND_TOP = 0

WM_DESTROY = 0x0002
WM_SETCURSOR = 0x0020
WM_MOUSEACTIVATE = 0x0021
WM_MOUSEMOVE = 0x0200
WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK = 0x0201, 0x0202, 0x0203
WM_RBUTTONUP = 0x0205
WM_MOUSELEAVE = 0x02A3
WM_CAPTURECHANGED = 0x0215
MA_NOACTIVATE = 3
IDC_ARROW, IDC_HAND, IDC_SIZEWE = 32512, 32649, 32644

LRESULT = ctypes.c_ssize_t

if IS_WIN:
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    WNDPROC = ctypes.WINFUNCTYPE(LRESULT, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)

    class WNDCLASSEXW(ctypes.Structure):
        _fields_ = [("cbSize", wintypes.UINT), ("style", wintypes.UINT), ("lpfnWndProc", WNDPROC),
                    ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int), ("hInstance", wintypes.HINSTANCE),
                    ("hIcon", wintypes.HICON), ("hCursor", wintypes.HANDLE), ("hbrBackground", wintypes.HBRUSH),
                    ("lpszMenuName", wintypes.LPCWSTR), ("lpszClassName", wintypes.LPCWSTR),
                    ("hIconSm", wintypes.HICON)]

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG),
                    ("biPlanes", wintypes.WORD), ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                    ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                    ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                    ("biClrImportant", wintypes.DWORD)]

    class BLENDFUNCTION(ctypes.Structure):
        _fields_ = [("BlendOp", ctypes.c_ubyte), ("BlendFlags", ctypes.c_ubyte),
                    ("SourceConstantAlpha", ctypes.c_ubyte), ("AlphaFormat", ctypes.c_ubyte)]

    class TRACKMOUSEEVENT(ctypes.Structure):
        _fields_ = [("cbSize", wintypes.DWORD), ("dwFlags", wintypes.DWORD), ("hwndTrack", wintypes.HWND),
                    ("dwHoverTime", wintypes.DWORD)]

    user32.DefWindowProcW.restype = LRESULT
    user32.DefWindowProcW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.RegisterClassExW.restype = wintypes.ATOM
    user32.RegisterClassExW.argtypes = [ctypes.POINTER(WNDCLASSEXW)]
    user32.CreateWindowExW.restype = wintypes.HWND
    user32.CreateWindowExW.argtypes = [wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD,
                                       ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.HWND,
                                       wintypes.HMENU, wintypes.HINSTANCE, wintypes.LPVOID]
    user32.DestroyWindow.argtypes = [wintypes.HWND]
    user32.IsWindow.argtypes = [wintypes.HWND]
    user32.GetParent.restype = wintypes.HWND
    user32.GetParent.argtypes = [wintypes.HWND]
    user32.GetWindow.restype = wintypes.HWND
    user32.GetWindow.argtypes = [wintypes.HWND, wintypes.UINT]
    user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                    ctypes.c_int, wintypes.UINT]
    user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    user32.FindWindowW.restype = wintypes.HWND
    user32.FindWindowExW.restype = wintypes.HWND
    user32.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
    user32.GetDC.restype = wintypes.HDC
    user32.GetDC.argtypes = [wintypes.HWND]
    user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    user32.UpdateLayeredWindow.argtypes = [wintypes.HWND, wintypes.HDC, ctypes.POINTER(wintypes.POINT),
                                           ctypes.POINTER(wintypes.SIZE), wintypes.HDC,
                                           ctypes.POINTER(wintypes.POINT), wintypes.COLORREF,
                                           ctypes.POINTER(BLENDFUNCTION), wintypes.DWORD]
    user32.ScreenToClient.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user32.LoadCursorW.restype = wintypes.HANDLE
    user32.LoadCursorW.argtypes = [wintypes.HINSTANCE, wintypes.LPVOID]
    user32.SetCursor.argtypes = [wintypes.HANDLE]
    user32.SetCapture.argtypes = [wintypes.HWND]
    user32.GetCapture.restype = wintypes.HWND
    user32.TrackMouseEvent.argtypes = [ctypes.POINTER(TRACKMOUSEEVENT)]
    gdi32.CreateCompatibleDC.restype = wintypes.HDC
    gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
    gdi32.CreateDIBSection.restype = wintypes.HBITMAP
    gdi32.CreateDIBSection.argtypes = [wintypes.HDC, ctypes.POINTER(BITMAPINFOHEADER), wintypes.UINT,
                                       ctypes.POINTER(ctypes.c_void_p), wintypes.HANDLE, wintypes.DWORD]
    gdi32.SelectObject.restype = wintypes.HGDIOBJ
    gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
    gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
    gdi32.DeleteDC.argtypes = [wintypes.HDC]
    kernel32.GetModuleHandleW.restype = wintypes.HMODULE
    kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]

CLASS_NAME = "F1DockTimersTaskbarPill"
_proc_ref = None            # the one WNDPROC thunk; must outlive every window of the class
_hosts: dict[int, "EmbeddedHost"] = {}


def _signed16(v: int) -> int:
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def _wndproc(hwnd, msg, wp, lp):
    host = _hosts.get(int(hwnd or 0))
    if host is not None:
        try:
            r = host._message(msg, wp, lp)
            if r is not None:
                return r
        except Exception:                       # never let an exception cross into Explorer's wait
            log.exception("taskbar pill message %#x", msg)
    return user32.DefWindowProcW(hwnd, msg, wp, lp)


def _register() -> bool:
    global _proc_ref
    if _proc_ref is not None:
        return True
    proc = WNDPROC(_wndproc)
    wc = WNDCLASSEXW()
    wc.cbSize = ctypes.sizeof(WNDCLASSEXW)
    wc.style = CS_DBLCLKS
    wc.lpfnWndProc = proc
    wc.hInstance = kernel32.GetModuleHandleW(None)
    wc.hCursor = user32.LoadCursorW(None, ctypes.c_void_p(IDC_ARROW))
    wc.lpszClassName = CLASS_NAME
    if not user32.RegisterClassExW(ctypes.byref(wc)) and ctypes.get_last_error() != 1410:  # class exists
        log.warning("RegisterClassExW failed: %s", ctypes.get_last_error())
        return False
    _proc_ref = proc
    return True


def find_taskbar() -> int:
    if not IS_WIN:
        return 0
    bar = user32.FindWindowW("Shell_TrayWnd", None)
    return int(bar or 0)


class EmbeddedHost:
    """One layered child window in the taskbar. Callbacks get client x in physical px.

    on_press(x), on_move(x, dragging), on_release(x), on_double(x), on_right(),
    on_hover(bool), cursor(x) -> "hand" | "size".
    """

    def __init__(self, callbacks: dict) -> None:
        self.cb = callbacks
        self.hwnd = 0
        self.bar = 0
        self._tracking = False
        self._size = (0, 0)

    # ── lifetime ────────────────────────────────────────────────────────────

    def create(self) -> bool:
        self.destroy()
        if not IS_WIN or not _register():
            return False
        bar = find_taskbar()
        if not bar:
            return False
        h = user32.CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, CLASS_NAME, "", WS_CHILD | WS_CLIPSIBLINGS,
                                   0, 0, 1, 1, wintypes.HWND(bar), None, kernel32.GetModuleHandleW(None), None)
        if not h:
            log.warning("could not create the taskbar child window (%s)", ctypes.get_last_error())
            return False
        self.hwnd, self.bar = int(h), bar
        _hosts[self.hwnd] = self
        return True

    def alive(self) -> bool:
        if not (IS_WIN and self.hwnd and self.bar):
            return False
        return bool(user32.IsWindow(self.hwnd) and user32.IsWindow(self.bar)
                    and int(user32.GetParent(self.hwnd) or 0) == self.bar
                    and find_taskbar() == self.bar)          # Explorer restarted -> new taskbar

    def destroy(self) -> None:
        if self.hwnd:
            _hosts.pop(self.hwnd, None)
            if IS_WIN and user32.IsWindow(self.hwnd):
                user32.DestroyWindow(self.hwnd)
        self.hwnd = self.bar = 0
        self._tracking = False

    def hide(self) -> None:
        if self.hwnd:
            user32.ShowWindow(self.hwnd, SW_HIDE)

    # ── geometry & picture ──────────────────────────────────────────────────

    def bar_rect(self) -> tuple[int, int, int, int] | None:
        r = wintypes.RECT()
        if self.bar and user32.GetWindowRect(self.bar, ctypes.byref(r)):
            return r.left, r.top, r.right, r.bottom
        return None

    def show_image(self, image, screen_x: int, screen_y: int) -> None:
        """image: QImage in Format_ARGB32_Premultiplied; position in physical screen px."""
        if not self.hwnd:
            return
        p = wintypes.POINT(screen_x, screen_y)
        user32.ScreenToClient(self.bar, ctypes.byref(p))
        w, h = image.width(), image.height()
        user32.SetWindowPos(self.hwnd, wintypes.HWND(HWND_TOP), p.x, p.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW)
        self._size = (w, h)
        self._blit(image)

    def _blit(self, image) -> None:
        w, h = image.width(), image.height()
        if w <= 0 or h <= 0:
            return
        screen = user32.GetDC(None)
        mem = gdi32.CreateCompatibleDC(screen)
        bi = BITMAPINFOHEADER()
        bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bi.biWidth, bi.biHeight, bi.biPlanes, bi.biBitCount = w, -h, 1, 32
        bits = ctypes.c_void_p()
        bmp = gdi32.CreateDIBSection(mem, ctypes.byref(bi), DIB_RGB_COLORS, ctypes.byref(bits), None, 0)
        try:
            if not bmp or not bits.value:
                return
            stride = image.bytesPerLine()
            data = bytes(image.constBits())
            if stride == w * 4:
                ctypes.memmove(bits, data, w * h * 4)
            else:
                for row in range(h):
                    ctypes.memmove(bits.value + row * w * 4, data[row * stride:row * stride + w * 4], w * 4)
            old = gdi32.SelectObject(mem, bmp)
            size = wintypes.SIZE(w, h)
            src = wintypes.POINT(0, 0)
            blend = BLENDFUNCTION(AC_SRC_OVER, 0, 255, AC_SRC_ALPHA)
            user32.UpdateLayeredWindow(self.hwnd, screen, None, ctypes.byref(size), mem, ctypes.byref(src), 0,
                                       ctypes.byref(blend), ULW_ALPHA)
            gdi32.SelectObject(mem, old)
        finally:
            if bmp:
                gdi32.DeleteObject(bmp)
            gdi32.DeleteDC(mem)
            user32.ReleaseDC(None, screen)

    def keep_on_top(self) -> None:
        """Stay above the taskbar's own content (our siblings), only when something covers us."""
        if self.hwnd and user32.GetWindow(self.hwnd, GW_HWNDPREV):
            user32.SetWindowPos(self.hwnd, wintypes.HWND(HWND_TOP), 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)

    # ── input ───────────────────────────────────────────────────────────────

    def _message(self, msg, wp, lp):
        x = _signed16(lp)
        cb = self.cb
        if msg == WM_MOUSEACTIVATE:
            return MA_NOACTIVATE
        if msg == WM_SETCURSOR:
            from_cb = cb.get("cursor", lambda _x: "hand")(self._cursor_x())
            cid = IDC_SIZEWE if from_cb == "size" else IDC_HAND
            user32.SetCursor(user32.LoadCursorW(None, ctypes.c_void_p(cid)))
            return 1
        if msg == WM_MOUSEMOVE:
            if not self._tracking:
                tme = TRACKMOUSEEVENT(ctypes.sizeof(TRACKMOUSEEVENT), TME_LEAVE, self.hwnd, 0)
                user32.TrackMouseEvent(ctypes.byref(tme))
                self._tracking = True
                cb["on_hover"](True)
            cb["on_move"](x, int(user32.GetCapture() or 0) == self.hwnd)
            return 0
        if msg == WM_MOUSELEAVE:
            self._tracking = False
            cb["on_hover"](False)
            return 0
        if msg == WM_LBUTTONDOWN:
            user32.SetCapture(self.hwnd)
            cb["on_press"](x)
            return 0
        if msg == WM_LBUTTONUP:
            captured = int(user32.GetCapture() or 0) == self.hwnd
            if captured:
                user32.ReleaseCapture()
            cb["on_release"](x)
            return 0
        if msg == WM_LBUTTONDBLCLK:
            cb["on_double"](x)
            return 0
        if msg == WM_RBUTTONUP:
            cb["on_right"]()
            return 0
        if msg == WM_DESTROY:
            _hosts.pop(self.hwnd, None)
            return None
        return None

    def _cursor_x(self) -> int:
        pt = wintypes.POINT()
        user32.GetCursorPos(ctypes.byref(pt))
        user32.ScreenToClient(self.hwnd, ctypes.byref(pt))
        return pt.x
