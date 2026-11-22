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

std::optional<TaskbarRects> taskbarRects();
// The tray rect grown to include add-ons packed right next to it (physical px), so we go beside them.
QRect withNeighbours(const QRect& bar, const QRect& tray, const QList<QRect>& others);
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
