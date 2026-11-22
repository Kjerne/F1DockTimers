#pragma once
// Small Win32 helpers for the taskbar pill (port of services/winshell.py) and the idle memory
// trim (services/memory.py). All fail soft.

#include <QList>
#include <QRect>
#include <functional>
#include <optional>

namespace f1::winshell {

struct TaskbarRects {
    QRect bar, tray, anchor;   // physical pixels; anchor = tray grown over other apps' add-ons
};

// `mine`: where the countdown sits now (physical px), if shown: widgets on its far side from the
// tray then don't push it (see withNeighbours).
std::optional<TaskbarRects> taskbarRects(std::optional<QRect> mine = std::nullopt);
// The tray rect grown to include add-ons packed right next to it (physical px), so we go beside them.
// With `mine`, only add-ons on the tray side of it count.
QRect withNeighbours(const QRect& bar, const QRect& tray, const QList<QRect>& others,
                     std::optional<QRect> mine = std::nullopt);
// A window's rect in physical px.
std::optional<QRect> windowRect(quintptr hwnd);
bool windowVisible(quintptr hwnd);
// Classic (Windows 10 style) taskbar: stop the app buttons at `rightEdge` so they never slide under
// the embedded countdown. Windows 11's XAML taskbar has no such window and is left alone.
void fitTaskButtons(quintptr bar, int rightEdge);
// Give the app buttons back the room we took (up to the next add-on or the tray).
void releaseTaskButtons(quintptr bar, std::optional<QRect> ourRect = std::nullopt);
bool foregroundIsFullscreen();
// Is the taskbar really what sits under physical point (x, y)?
bool taskbarIsVisibleAt(int x, int y);
void keepTopmost(quintptr hwnd);
// Raise a normal window above everything once, then make it normal again.
void bringToFront(quintptr hwnd);
bool menuIsOpen();

// Calls `callback` whenever another app's window becomes the foreground one.
class ForegroundWatch {
public:
    explicit ForegroundWatch(std::function<void()> callback);
    ~ForegroundWatch();
    ForegroundWatch(const ForegroundWatch&) = delete;
    ForegroundWatch& operator=(const ForegroundWatch&) = delete;

private:
    void* m_hook = nullptr;
};

// Working set (what Task Manager calls "Memory") in MB, and EmptyWorkingSet.
double workingSetMb();
double privateBytesMb();
bool trimWorkingSet();

}  // namespace f1::winshell
