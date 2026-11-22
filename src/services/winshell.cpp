#include "winshell.h"

#include "widgets_button.h"

#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>

#include <QList>
#include <QString>
#include <QStringList>

namespace f1::winshell {

namespace {

std::optional<QRect> rectOf(HWND h) {
    RECT r{};
    if (h && GetWindowRect(h, &r)) return QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
    return std::nullopt;
}

QString className(HWND h) {
    wchar_t buf[128] = {};
    GetClassNameW(h, buf, 128);
    return QString::fromWCharArray(buf);
}

// Explorer's own parts of the taskbar; anything else inside it is another app's add-on.
const QStringList& taskbarCore() {
    static const QStringList c = {
        QStringLiteral("TrayNotifyWnd"), QStringLiteral("ReBarWindow32"), QStringLiteral("Start"),
        QStringLiteral("TrayDummySearchControl"), QStringLiteral("TrayButton"),
        QStringLiteral("Windows.UI.Composition.DesktopWindowContentBridge"),
        QStringLiteral("Windows.UI.Input.InputSite.WindowClass"), QStringLiteral("MSTaskSwWClass"),
        QStringLiteral("TrayShowDesktopButtonWClass"), QStringLiteral("F1DockTimersTaskbarPill")};
    return c;
}

QList<QRect> neighbours(HWND bar) {
    QList<QRect> out;
    const DWORD own = GetCurrentProcessId();
    HWND h = GetWindow(bar, GW_CHILD);
    for (int i = 0; i < 200 && h; ++i, h = GetWindow(h, GW_HWNDNEXT)) {
        if (!IsWindowVisible(h)) continue;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        const auto r = rectOf(h);
        if (pid != own && !taskbarCore().contains(className(h)) && r && r->width() > 0 && r->height() > 0)
            out.append(*r);
    }
    return out;
}

}  // namespace

// The tray rect grown to include add-ons packed right next to it, so we go beside them.
// Works on exclusive right/bottom edges like the Python version.
QRect withNeighbours(const QRect& barQ, const QRect& trayQ, const QList<QRect>& others) {
    const int bl = barQ.left(), bt = barQ.top(), br = barQ.right() + 1, bb = barQ.bottom() + 1;
    const bool vertical = (bb - bt) > (br - bl);
    int left = trayQ.left(), top = trayQ.top();
    const int right = trayQ.right() + 1, bottom = trayQ.bottom() + 1;
    const int limit = vertical ? (bb - bt) / 3 : (br - bl) / 3;
    for (int pass = 0; pass < 8; ++pass) {
        bool moved = false;
        for (const QRect& q : others) {
            const int l = q.left(), t = q.top(), r = q.right() + 1, b = q.bottom() + 1;
            if (vertical) {
                if (b - t < limit && l < br && r > bl && top - 16 <= b && b <= top + 4 && t < top) {
                    top = t;
                    moved = true;
                }
            } else if (r - l < limit && t < bb && b > bt && left - 16 <= r && r <= left + 4 && l < left) {
                left = l;
                moved = true;
            }
        }
        if (!moved) break;
    }
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
}

namespace {
std::function<void()>* g_foreground = nullptr;

void CALLBACK winEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (g_foreground && *g_foreground) (*g_foreground)();
}

}  // namespace

std::optional<TaskbarRects> taskbarRects() {
    HWND bar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!bar) return std::nullopt;
    HWND tray = FindWindowExW(bar, nullptr, L"TrayNotifyWnd", nullptr);
    const auto b = rectOf(bar);
    if (!b) return std::nullopt;
    TaskbarRects out;
    out.bar = *b;
    out.tray = rectOf(tray).value_or(QRect());
    QList<QRect> others = neighbours(bar);
    if (FindWindowExW(bar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)) {
        // Windows 11 taskbar: the Widgets (weather) button can sit next to the tray (left-aligned
        // taskbar). Its position comes from a background UI Automation lookup; never blocks.
        if (const auto wb = widgetsButtonRect()) others.append(*wb);
    }
    out.anchor = out.tray.isValid() ? withNeighbours(out.bar, out.tray, others) : out.tray;
    return out;
}

bool foregroundIsFullscreen() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    const QString cls = className(fg);
    if (cls == u"Progman" || cls == u"WorkerW" || cls == u"Shell_TrayWnd" || cls == u"Shell_SecondaryTrayWnd") return false;
    RECT r{};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetWindowRect(fg, &r) || !GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    const RECT& m = mi.rcMonitor;
    return r.left <= m.left && r.top <= m.top && r.right >= m.right && r.bottom >= m.bottom;
}

bool taskbarIsVisibleAt(int x, int y) {
    static const QStringList skip = {QStringLiteral("tooltips_class32"), QStringLiteral("#32768"),
                                     QStringLiteral("Xaml_WindowedPopupClass"), QStringLiteral("NotifyIconOverflowWindow"),
                                     QStringLiteral("TopLevelWindowForOverflowXamlIsland")};
    const DWORD own = GetCurrentProcessId();
    HWND h = GetTopWindow(nullptr);
    for (int i = 0; i < 2000 && h; ++i, h = GetWindow(h, GW_HWNDNEXT)) {
        if (!IsWindowVisible(h) || IsIconic(h)) continue;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        RECT r{};
        if (pid == own || !GetWindowRect(h, &r)) continue;
        if (!(r.left <= x && x < r.right && r.top <= y && y < r.bottom)) continue;
        int cloaked = 0;
        DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        const QString cls = className(h);
        if (!cloaked && !skip.contains(cls)) return cls == u"Shell_TrayWnd" || cls == u"Shell_SecondaryTrayWnd";
    }
    return true;
}

void keepTopmost(quintptr hwnd) {
    if (hwnd)
        SetWindowPos(reinterpret_cast<HWND>(hwnd), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void bringToFront(quintptr hwnd) {
    if (!hwnd) return;
    HWND h = reinterpret_cast<HWND>(hwnd);
    const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW;
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, flags);
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
    SetForegroundWindow(h);
}

void fitTaskButtons(quintptr barId, int rightEdge) {
    HWND bar = reinterpret_cast<HWND>(barId);
    if (!bar) return;
    HWND rebar = FindWindowExW(bar, nullptr, L"ReBarWindow32", nullptr);
    if (!rebar || FindWindowExW(bar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)) return;
    RECT r{}, b{};
    if (!GetWindowRect(rebar, &r) || !GetWindowRect(bar, &b) || (b.bottom - b.top) > (b.right - b.left)) return;   // vertical: leave it
    if (r.left < rightEdge && rightEdge < r.right) {
        POINT p{r.left, r.top};
        ScreenToClient(bar, &p);
        MoveWindow(rebar, p.x, p.y, rightEdge - r.left, r.bottom - r.top, TRUE);
    }
}

void releaseTaskButtons(quintptr barId, std::optional<QRect> ourRect) {
    HWND bar = reinterpret_cast<HWND>(barId);
    if (!bar) return;
    HWND rebar = FindWindowExW(bar, nullptr, L"ReBarWindow32", nullptr);
    if (!rebar || FindWindowExW(bar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)) return;
    const auto b = rectOf(bar), r = rectOf(rebar), tray = rectOf(FindWindowExW(bar, nullptr, L"TrayNotifyWnd", nullptr));
    if (!b || !r || !tray) return;
    const int edge = withNeighbours(*b, *tray, neighbours(bar)).left();
    const int rRight = r->right() + 1;
    if (rRight < edge && (!ourRect || rRight <= ourRect->left() + 2)) {
        POINT p{r->left(), r->top()};
        ScreenToClient(bar, &p);
        MoveWindow(rebar, p.x, p.y, edge - r->left(), r->height(), TRUE);
    }
}

bool menuIsOpen() {
    HWND h = FindWindowW(L"#32768", nullptr);
    return h && IsWindowVisible(h);
}

ForegroundWatch::ForegroundWatch(std::function<void()> callback) {
    if (g_foreground) return;   // one watch per process is all the pill needs
    g_foreground = new std::function<void()>(std::move(callback));
    m_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, winEventProc, 0, 0,
                             WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

ForegroundWatch::~ForegroundWatch() {
    if (m_hook) {
        UnhookWinEvent(static_cast<HWINEVENTHOOK>(m_hook));
        delete g_foreground;
        g_foreground = nullptr;
    }
}

double workingSetMb() {
    PROCESS_MEMORY_COUNTERS c{};
    c.cb = sizeof(c);
    return GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c)) ? c.WorkingSetSize / (1024.0 * 1024.0) : 0.0;
}

double privateBytesMb() {
    PROCESS_MEMORY_COUNTERS_EX c{};
    c.cb = sizeof(c);
    return GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c), sizeof(c))
               ? c.PrivateUsage / (1024.0 * 1024.0)
               : 0.0;
}

bool trimWorkingSet() { return EmptyWorkingSet(GetCurrentProcess()) != 0; }

}  // namespace f1::winshell
