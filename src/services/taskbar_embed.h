#pragma once
// A layered child window inside the Windows taskbar (Shell_TrayWnd) (port of
// services/taskbar_embed.py).
//
// Living inside the taskbar instead of floating over it means Windows handles the hard cases: the
// countdown hides with the taskbar (fullscreen games, auto-hide), menus and flyouts always open
// above it, and clicking the taskbar can't bury it. The window is plain Win32; the picture is
// painted with Qt into a QImage and handed over with UpdateLayeredWindow, so the look is identical.
//
// Caveat: a child window of another process shares its input queue with Explorer, so Explorer
// waits for our UI thread whenever it talks to the window. Everything here is short and never
// blocks.

#include <QImage>
#include <QRect>
#include <functional>
#include <optional>

namespace f1 {

class EmbeddedHost {
public:
    // Callbacks get the client x in physical pixels.
    struct Callbacks {
        std::function<void(int x)> onPress, onRelease, onDouble;
        std::function<void(int x, bool dragging)> onMove;
        std::function<void()> onRight;
        std::function<void(bool on)> onHover;
        std::function<bool(int x)> resizeCursor;   // true: the sizing cursor, false: the hand
    };

    explicit EmbeddedHost(Callbacks cb);
    ~EmbeddedHost();
    EmbeddedHost(const EmbeddedHost&) = delete;
    EmbeddedHost& operator=(const EmbeddedHost&) = delete;

    bool create();
    bool alive() const;    // false after Explorer restarted (new taskbar) or our window vanished
    void destroy();
    void hide();
    // `image` in Format_ARGB32_Premultiplied; position in physical screen pixels.
    void showImage(const QImage& image, int screenX, int screenY);
    void keepOnTop();      // above the taskbar's own content, only when something covers us
    std::optional<QRect> barRect() const;
    std::optional<QRect> windowRect() const;

    quintptr hwnd = 0;
    quintptr bar = 0;

    // Internal: the window procedure forwards here.
    std::optional<long long> message(unsigned msg, quintptr wp, qintptr lp);

private:
    void blit(const QImage& image);
    int cursorX() const;
    Callbacks m_cb;
    bool m_tracking = false;
};

quintptr findTaskbar();

}  // namespace f1
