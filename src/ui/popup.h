#pragma once
// Tray popup: frameless panel anchored above the tray icon, animated, dismissed by an outside
// click unless pinned (port of ui/popup.py).

#include "panel.h"

#include <QElapsedTimer>
#include <QParallelAnimationGroup>
#include <QPointer>
#include <QWidget>

namespace f1::ui {

inline constexpr int BASE_W = 500, BASE_H = 640, GAP = 10;

// Where the popup goes for a tray icon rect, and the taskbar edge. Clamped inside the available
// area (never under the taskbar).
std::pair<QPoint, QString> anchorPosition(const QRect& icon, const QRect& avail, const QRect& full, const QSize& size,
                                          int gap = GAP);

class TrayPopup : public QWidget {
    Q_OBJECT
public:
    TrayPopup(Ctx* ctx, Settings* settings, QWidget* parent = nullptr);
    void toggle(const QRect& iconRect);
    // Open above the tray icon; with `settingsPage` >= 0 on that page of the Settings tab.
    void showAt(const QRect& iconRect, int settingsPage = -1);
    void hideAnimated();
    F1Panel* panel;

protected:
    void changeEvent(QEvent* e) override;

private:
    void applySize();
    QString place(const QRect& iconRect);
    void animate(const QPoint& start, const QPoint& end, double o0, double o1, int ms, std::function<void()> done = {});

    Settings* m_settings;
    Ctx* m_ctx;
    QElapsedTimer m_hiddenAt;
    QPointer<QParallelAnimationGroup> m_anim;
};

}  // namespace f1::ui
