#pragma once
// Persistent desktop widget: the same panel on the desktop, draggable, resizable, remembers its
// place (port of ui/desktop_widget.py).

#include "panel.h"

#include <QJsonArray>
#include <QTimer>
#include <QWidget>
#include <optional>

namespace f1::ui {

inline constexpr int WIDGET_EDGE = 7, WIDGET_MIN_W = 300, WIDGET_MIN_H = 260;

// Saved [x, y, w, h] if at least 40x40 px of it is on a current screen. Pure.
std::optional<QRect> restoreRect(const QJsonValue& saved, const QList<QRect>& screens);

class DesktopWidget : public QWidget {
    Q_OBJECT
public:
    DesktopWidget(Ctx* ctx, QWidget* parent = nullptr);
    void applySettings(bool first = false);
    void restoreGeometry();
    F1Panel* panel;

Q_SIGNALS:
    // refresh | settings | panel | hide | toggle_top | toggle_click | toggle_lock
    void action(const QString& what);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void moveEvent(QMoveEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    Qt::Edges edges(const QPoint& pos) const;
    bool interactive(const QPoint& pos) const;
    void showMenu(const QPoint& global);
    void persist();

    Ctx* m_ctx;
    QTimer m_save;
};

}  // namespace f1::ui
