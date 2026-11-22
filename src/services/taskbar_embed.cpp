#include "taskbar_embed.h"

#include <windows.h>

#include <QHash>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcEmbed, "f1.embed")

namespace f1 {

namespace {

const wchar_t* CLASS_NAME = L"F1DockTimersTaskbarPill";   // the floating pill's neighbour scan skips this class
QHash<quintptr, EmbeddedHost*>& hosts() {
    static QHash<quintptr, EmbeddedHost*> h;
    return h;
}

int signed16(qintptr v) {
    const int x = static_cast<int>(v & 0xFFFF);
    return (x & 0x8000) ? x - 0x10000 : x;
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (EmbeddedHost* host = hosts().value(reinterpret_cast<quintptr>(h))) {
        // Never let anything escape into Explorer's wait.
        try {
            if (const auto r = host->message(msg, static_cast<quintptr>(wp), static_cast<qintptr>(lp))) return static_cast<LRESULT>(*r);
        } catch (...) {
            qCWarning(lcEmbed) << "taskbar pill message" << Qt::hex << msg;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool registerClass() {
    static bool done = false;
    if (done) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        qCWarning(lcEmbed) << "RegisterClassExW failed" << GetLastError();
        return false;
    }
    done = true;
    return true;
}

HWND H(quintptr v) { return reinterpret_cast<HWND>(v); }

}  // namespace

quintptr findTaskbar() { return reinterpret_cast<quintptr>(FindWindowW(L"Shell_TrayWnd", nullptr)); }

EmbeddedHost::EmbeddedHost(Callbacks cb) : m_cb(std::move(cb)) {}

EmbeddedHost::~EmbeddedHost() { destroy(); }

bool EmbeddedHost::create() {
    destroy();
    if (!registerClass()) return false;
    const quintptr b = findTaskbar();
    if (!b) return false;
    // A layered child window needs the exe manifest to declare Windows 8 or later (src/app.manifest).
    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, CLASS_NAME, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, H(b),
                             nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!h) {
        const DWORD err = GetLastError();   // read before anything else can reset it
        qCWarning(lcEmbed) << "could not create the taskbar child window" << err;
        return false;
    }
    hwnd = reinterpret_cast<quintptr>(h);
    bar = b;
    hosts().insert(hwnd, this);
    return true;
}

bool EmbeddedHost::alive() const {
    if (!hwnd || !bar) return false;
    return IsWindow(H(hwnd)) && IsWindow(H(bar)) && reinterpret_cast<quintptr>(GetParent(H(hwnd))) == bar &&
           findTaskbar() == bar;   // Explorer restarted -> new taskbar
}

void EmbeddedHost::destroy() {
    if (hwnd) {
        hosts().remove(hwnd);
        if (IsWindow(H(hwnd))) DestroyWindow(H(hwnd));
    }
    hwnd = bar = 0;
    m_tracking = false;
}

void EmbeddedHost::hide() {
    if (hwnd) ShowWindow(H(hwnd), SW_HIDE);
}

std::optional<QRect> EmbeddedHost::barRect() const {
    RECT r{};
    if (bar && GetWindowRect(H(bar), &r)) return QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
    return std::nullopt;
}

std::optional<QRect> EmbeddedHost::windowRect() const {
    RECT r{};
    if (hwnd && GetWindowRect(H(hwnd), &r)) return QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
    return std::nullopt;
}

void EmbeddedHost::showImage(const QImage& image, int screenX, int screenY) {
    if (!hwnd) return;
    POINT p{screenX, screenY};
    ScreenToClient(H(bar), &p);
    SetWindowPos(H(hwnd), HWND_TOP, p.x, p.y, image.width(), image.height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    blit(image);
}

void EmbeddedHost::blit(const QImage& src) {
    const QImage image = src.format() == QImage::Format_ARGB32_Premultiplied ? src : src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = image.width(), h = image.height();
    if (w <= 0 || h <= 0) return;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        for (int row = 0; row < h; ++row) memcpy(static_cast<uchar*>(bits) + row * w * 4, image.constScanLine(row), w * 4);
        HGDIOBJ old = SelectObject(mem, bmp);
        SIZE size{w, h};
        POINT from{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(H(hwnd), screen, nullptr, &size, mem, &from, 0, &blend, ULW_ALPHA);
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void EmbeddedHost::keepOnTop() {
    if (hwnd && GetWindow(H(hwnd), GW_HWNDPREV))
        SetWindowPos(H(hwnd), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

int EmbeddedHost::cursorX() const {
    POINT pt{};
    GetCursorPos(&pt);
    ScreenToClient(H(hwnd), &pt);
    return pt.x;
}

std::optional<long long> EmbeddedHost::message(unsigned msg, quintptr wp, qintptr lp) {
    Q_UNUSED(wp);
    const int x = signed16(lp);
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR: {
        const bool size = m_cb.resizeCursor && m_cb.resizeCursor(cursorX());
        SetCursor(LoadCursorW(nullptr, size ? IDC_SIZEWE : IDC_HAND));
        return 1;
    }
    case WM_MOUSEMOVE:
        if (!m_tracking) {
            TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, H(hwnd), 0};
            TrackMouseEvent(&tme);
            m_tracking = true;
            if (m_cb.onHover) m_cb.onHover(true);
        }
        if (m_cb.onMove) m_cb.onMove(x, GetCapture() == H(hwnd));
        return 0;
    case WM_MOUSELEAVE:
        m_tracking = false;
        if (m_cb.onHover) m_cb.onHover(false);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(H(hwnd));
        if (m_cb.onPress) m_cb.onPress(x);
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == H(hwnd)) ReleaseCapture();
        if (m_cb.onRelease) m_cb.onRelease(x);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (m_cb.onDouble) m_cb.onDouble(x);
        return 0;
    case WM_RBUTTONUP:
        if (m_cb.onRight) m_cb.onRight();
        return 0;
    case WM_DESTROY:
        hosts().remove(hwnd);
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

}  // namespace f1
