#include "popup.h"

#include "../core/i18n.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QScreen>
#include <QVBoxLayout>

namespace f1::ui {

std::pair<QPoint, QString> anchorPosition(const QRect& icon, const QRect& avail, const QRect& full, const QSize& size, int gap) {
    const int w = size.width(), h = size.height();
    QString edge;
    if (avail.bottom() < full.bottom()) edge = QStringLiteral("bottom");
    else if (avail.top() > full.top()) edge = QStringLiteral("top");
    else if (avail.left() > full.left()) edge = QStringLiteral("left");
    else if (avail.right() < full.right()) edge = QStringLiteral("right");
    else edge = QStringLiteral("bottom");   // auto-hidden taskbar: assume the default
    const int cx = icon.isValid() ? icon.center().x() : avail.right();
    const int cy = icon.isValid() ? icon.center().y() : avail.bottom();
    int x, y;
    if (edge == u"bottom") {
        x = cx - w / 2;
        y = avail.bottom() - h - gap + 1;
    } else if (edge == u"top") {
        x = cx - w / 2;
        y = avail.top() + gap;
    } else if (edge == u"left") {
        x = avail.left() + gap;
        y = cy - h / 2;
    } else {
        x = avail.right() - w - gap + 1;
        y = cy - h / 2;
    }
    x = std::max(avail.left() + gap, std::min(x, avail.right() - w - gap + 1));
    y = std::max(avail.top() + gap, std::min(y, avail.bottom() - h - gap + 1));
    return {QPoint(x, y), edge};
}

TrayPopup::TrayPopup(Ctx* ctx, Settings* settings, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint),
      m_settings(settings), m_ctx(ctx) {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(T(QStringLiteral("Formula 1")));
    setAccessibleName(T(QStringLiteral("Formula 1 panel")));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    panel = new F1Panel(ctx, QStringLiteral("popup"), this);
    panel->bgOpacity = settings->popup_opacity / 100.0;
    panel->setPinned(settings->popup_pinned);
    connect(panel, &F1Panel::closeRequested, this, &TrayPopup::hideAnimated);
    connect(panel, &F1Panel::pinToggled, this, [this](bool on) {
        m_settings->popup_pinned = on;
        m_ctx->saveSettings();
    });
    lay->addWidget(panel);
    applySize();
}

void TrayPopup::applySize() {
    const double s = m_ctx->theme->scale;
    resize(qRound(BASE_W * s), qRound(BASE_H * s));
}

QString TrayPopup::place(const QRect& iconRect) {
    QScreen* screen = iconRect.isValid() ? QGuiApplication::screenAt(iconRect.center()) : nullptr;
    if (!screen) screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect avail = screen->availableGeometry();
    applySize();   // never larger than the monitor (high scaling on small screens)
    const int maxW = avail.width() - 2 * GAP, maxH = avail.height() - 2 * GAP;
    if (width() > maxW || height() > maxH) resize(std::min(width(), maxW), std::min(height(), maxH));
    const auto [pos, edge] = anchorPosition(iconRect, avail, screen->geometry(), size());
    move(pos);
    return edge;
}

void TrayPopup::toggle(const QRect& iconRect) {
    if (isVisible()) {
        hideAnimated();
        return;
    }
    // The click that deactivated (and hid) us is the same click on the tray icon.
    if (m_hiddenAt.isValid() && m_hiddenAt.elapsed() < 350) return;
    showAt(iconRect);
}

void TrayPopup::showAt(const QRect& iconRect, int settingsPage) {
    const QString edge = place(iconRect);
    if (settingsPage >= 0) panel->showSettings(settingsPage);
    else panel->openFresh();
    const QPoint end = pos();
    if (m_settings->animations) {
        QPoint d(0, 14);
        if (edge == u"top") d = QPoint(0, -14);
        else if (edge == u"left") d = QPoint(-14, 0);
        else if (edge == u"right") d = QPoint(14, 0);
        setWindowOpacity(0.0);
        move(end + d);
        show();
        animate(end + d, end, 0.0, 1.0, 170);
    } else {
        setWindowOpacity(1.0);
        show();
    }
    raise();
    activateWindow();
    panel->setFocus();
}

void TrayPopup::animate(const QPoint& start, const QPoint& end, double o0, double o1, int ms, std::function<void()> done) {
    if (m_anim) m_anim->stop();
    auto* g = new QParallelAnimationGroup(this);
    auto* a = new QPropertyAnimation(this, "pos", g);
    a->setStartValue(start);
    a->setEndValue(end);
    a->setDuration(ms);
    a->setEasingCurve(QEasingCurve::OutCubic);
    auto* b = new QPropertyAnimation(this, "windowOpacity", g);
    b->setStartValue(o0);
    b->setEndValue(o1);
    b->setDuration(ms);
    g->addAnimation(a);
    g->addAnimation(b);
    if (done) connect(g, &QParallelAnimationGroup::finished, this, done);
    m_anim = g;
    g->start(QAbstractAnimation::DeleteWhenStopped);
}

void TrayPopup::hideAnimated() {
    if (!isVisible()) return;
    m_hiddenAt.start();
    if (m_settings->animations) {
        const QPoint start = pos();
        animate(start, start, windowOpacity(), 0.0, 120, [this, start] {
            hide();
            move(start);
            setWindowOpacity(1.0);
        });
    } else {
        hide();
    }
}

void TrayPopup::changeEvent(QEvent* e) {
    // Stay open for our own pop-ups: a drop-down list, or a colour / file / message box opened from
    // the Settings tab (those set the "f1_hold" property while open).
    if (e->type() == QEvent::ActivationChange && !isActiveWindow() && isVisible() && !panel->pinned &&
        !QApplication::activeModalWidget() && !QApplication::activePopupWidget() && property("f1_hold").toInt() <= 0)
        hideAnimated();
    QWidget::changeEvent(e);
}

}  // namespace f1::ui
