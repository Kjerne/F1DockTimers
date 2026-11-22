#pragma once
// Taskbar countdown pill, floating mode (port of ui/taskbar_pill.py).
//
// Windows 11 has no supported way to put custom content inside the taskbar, so this is a small
// frameless, topmost, non-activating window over the empty taskbar area just left of the
// notification icons. Every 2 s it checks what is under it and hides when anything (a fullscreen
// game, a video) covers the taskbar there; otherwise it re-asserts topmost. The embedded mode
// (a child window inside the taskbar) is not ported yet.

#include "../core/pilltext.h"
#include "../services/store.h"
#include "../services/taskbar_embed.h"
#include "../services/winshell.h"
#include "theme.h"

#include <QColor>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

namespace f1::ui {

struct PillStyle {
    QColor bg;
    std::optional<QColor> border;
    QColor top, bottom, live, hover;
    double radius = 7;
    bool boldTop = true;
    Qt::Alignment align = Qt::AlignLeft;
    QString family;

    static PillStyle resolve(const Settings& s, const Theme& theme);
};

QList<QFont> fitFont(const QString& family, const QStringList& lines, double width, double height, double maxPx,
                     bool autofit, bool boldFirst);
void paintPill(QPainter& p, const QRectF& rect, const PillStyle& style, const QString& top, const QString& bottom,
               bool live, bool hover, double maxPx, bool autofit, double pad);

// The same painter on a fake taskbar strip, for the settings dialog.
class PillPreview : public QWidget {
    Q_OBJECT
public:
    explicit PillPreview(QWidget* parent = nullptr);
    void setPreview(const PillStyle& style, const pill::State& lines, QSize size, double maxPx, bool autofit, const QColor& bar);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::optional<PillStyle> m_style;
    pill::State m_lines;
    QSize m_size;
    double m_maxPx = 11;
    bool m_autofit = true;
    QColor m_bar;
};

class TaskbarPill : public QWidget {
    Q_OBJECT
public:
    TaskbarPill(DataStore* store, Settings* settings, const Theme* theme, const Zone* zone,
                std::function<QRect()> trayRect, QWidget* parent = nullptr);
    ~TaskbarPill() override;
    void start();
    void stop();
    void restyle();
    void tick(qint64 now, bool force = false);
    void place();
    void guard();
    bool isShown() const { return m_embed != nullptr || isVisible(); }
    bool embedded() const { return m_embed != nullptr; }
    bool embedFailed() const { return m_embedFailed; }
    const EmbeddedHost* embedHost() const { return m_embed.get(); }
    int effectiveOffset() const { return offset(); }

Q_SIGNALS:
    void clicked();
    void menuRequested(const QPoint& globalPos);
    void offsetChanged();   // the user moved or resized the countdown: save the settings

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    struct Anchor {
        QRect bar, tray;
        bool vertical;
    };
    std::optional<Anchor> anchor() const;
    int autoWidth(int height) const;
    double maxPx() const;
    double pad() const;
    int offset() const;
    bool taskbarUnderMe() const;
    void raiseTop();
    void foregroundChanged();
    bool onEdge(const QPoint& p) const { return p.x() <= 6; }
    double dpr() const;
    // embedded mode (a child window inside the taskbar)
    void setMode();
    void dropEmbed();
    void refresh();
    void renderEmbed(bool force = false);
    void fitButtons(std::optional<int> x = std::nullopt);
    void embedTooltip();
    // input shared by both modes
    void pressAt(const QPoint& global, const QPoint& local);
    void moveAt(const QPoint& global);
    void releaseAt(Qt::MouseButton button, const QPoint& global);
    void doubleAt(const QPoint& local);

    DataStore* m_store;
    Settings* m_settings;
    const Theme* m_theme;
    const Zone* m_zone;
    std::function<QRect()> m_trayRect;
    PillStyle m_style;
    pill::State m_state{QStringLiteral("F1"), QString(), false};
    bool m_hover = false;
    std::optional<QPoint> m_press;
    QString m_mode;
    int m_pressOffset = 0, m_pressWidth = 0;
    bool m_moved = false, m_fsHidden = false;
    QTimer m_guard, m_raiser;
    std::unique_ptr<winshell::ForegroundWatch> m_fgWatch;
    std::unique_ptr<EmbeddedHost> m_embed;
    QString m_embedKey;
    bool m_embedFailed = false;
};

}  // namespace f1::ui
