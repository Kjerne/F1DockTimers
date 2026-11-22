#pragma once
// Where is the Windows 11 Widgets (weather) button? (port of services/widgets_button.py)
//
// On a left-aligned Windows 11 taskbar it sits right next to the tray, exactly where the countdown
// goes. It is drawn by XAML, so only UI Automation knows its position. UI Automation calls wait for
// Explorer's UI thread, and in embedded mode Explorer in turn waits for ours, so the lookup runs on
// its own thread and the UI only ever reads the last result (never blocks).

#include <QRect>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace f1 {

class WidgetsButtonWatch {
public:
    WidgetsButtonWatch() = default;
    ~WidgetsButtonWatch();
    void start();
    void stop();
    std::optional<QRect> rect() const;   // physical pixels; nullopt when hidden, off or unknown

private:
    void run();
    mutable std::mutex m_lock;
    std::optional<QRect> m_rect;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

// Last known rectangle. Starts the background lookup on first use.
std::optional<QRect> widgetsButtonRect();

}  // namespace f1
