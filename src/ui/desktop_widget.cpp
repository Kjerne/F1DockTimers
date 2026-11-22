#include "desktop_widget.h"

#include "../core/i18n.h"
#include "widgets.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWindow>
#include <cmath>

namespace f1::ui {

std::optional<QRect> restoreRect(const QJsonValue& saved, const QList<QRect>& screens) {
    if (!saved.isArray()) return std::nullopt;
    const QJsonArray a = saved.toArray();
    if (a.size() != 4) return std::nullopt;
    for (const QJsonValue& v : a)
        if (!v.isDouble()) return std::nullopt;
    auto i = [&](int k) { return static_cast<int>(std::trunc(a[k].toDouble())); };   // int(v)
    const QRect r(i(0), i(1), std::max(WIDGET_MIN_W, i(2)), std::max(WIDGET_MIN_H, i(3)));
    for (const QRect& s : screens) {
        const QRect inter = r.intersected(s);
        if (inter.width() >= 40 && inter.height() >= 40) return r;
    }
    return std::nullopt;
}

namespace {
constexpr Qt::WindowFlags BASE_FLAGS = Qt::FramelessWindowHint | Qt::Tool | Qt::NoDropShadowWindowHint;
}

DesktopWidget::DesktopWidget(Ctx* ctx, QWidget* parent) : QWidget(parent, BASE_FLAGS), m_ctx(ctx) {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(T(QStringLiteral("Formula 1 widget")));
    setAccessibleName(T(QStringLiteral("Formula 1 desktop widget")));
    setMinimumSize(WIDGET_MIN_W, WIDGET_MIN_H);
    setMouseTracking(true);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    panel = new F1Panel(ctx, QStringLiteral("widget"), this);
    panel->setMouseTracking(true);   // Esc does not hide a desktop widget: closeRequested stays unconnected
    lay->addWidget(panel);
    panel->installEventFilter(this);
    m_save.setSingleShot(true);
    m_save.setInterval(600);
    connect(&m_save, &QTimer::timeout, this, &DesktopWidget::persist);
    applySettings(true);
}

void DesktopWidget::applySettings(bool first) {
    const Settings& s = *m_ctx->settings;
    panel->bgOpacity = s.widget_opacity / 100.0;
    const bool wasVisible = isVisible();
    Qt::WindowFlags flags = BASE_FLAGS;
    if (s.widget_always_on_top) flags |= Qt::WindowStaysOnTopHint;
    if (s.widget_click_through) flags |= Qt::WindowTransparentForInput;
    if (flags != windowFlags()) {
        const QRect geo = geometry();
        setWindowFlags(flags);
        if (!first) setGeometry(geo);
    }
    if (first) restoreGeometry();
    if (wasVisible && !isVisible()) show();
    panel->update();
}

void DesktopWidget::restoreGeometry() {
    QList<QRect> screens;
    for (QScreen* sc : QGuiApplication::screens()) screens << sc->availableGeometry();
    const double scale = m_ctx->theme->scale;
    std::optional<QRect> rect;
    if (m_ctx->settings->widget_remember_position) rect = restoreRect(m_ctx->settings->widget_geometry, screens);
    if (!rect) {
        const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        const int w = static_cast<int>(std::nearbyint(420 * scale)), h = static_cast<int>(std::nearbyint(560 * scale));
        rect = QRect(avail.right() - w - 24, avail.top() + 24, w, h);
    }
    setGeometry(*rect);
}

void DesktopWidget::persist() {
    if (!m_ctx->settings->widget_remember_position) return;
    const QRect g = geometry();
    m_ctx->settings->widget_geometry = QJsonArray{g.x(), g.y(), g.width(), g.height()};
    m_ctx->saveSettings();
}

void DesktopWidget::moveEvent(QMoveEvent* e) {
    QWidget::moveEvent(e);
    m_save.start();
}

void DesktopWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    m_save.start();
}

Qt::Edges DesktopWidget::edges(const QPoint& pos) const {
    Qt::Edges out;
    const QRect r = rect();
    if (pos.x() <= WIDGET_EDGE) out |= Qt::LeftEdge;
    if (pos.x() >= r.width() - WIDGET_EDGE) out |= Qt::RightEdge;
    if (pos.y() <= WIDGET_EDGE) out |= Qt::TopEdge;
    if (pos.y() >= r.height() - WIDGET_EDGE) out |= Qt::BottomEdge;
    return out;
}

bool DesktopWidget::interactive(const QPoint& pos) const {
    for (QWidget* w = childAt(pos); w && w != this; w = w->parentWidget()) {
        if (qobject_cast<PillButton*>(w) || qobject_cast<IconButton*>(w) || qobject_cast<QScrollBar*>(w)) return true;
        if (auto* row = qobject_cast<Row*>(w); row && !row->url().isEmpty()) return true;
    }
    return false;
}

bool DesktopWidget::eventFilter(QObject* obj, QEvent* e) {
    const Settings& s = *m_ctx->settings;
    switch (e->type()) {
    case QEvent::MouseMove: {
        if (s.widget_locked) break;
        const Qt::Edges ed = edges(static_cast<QMouseEvent*>(e)->position().toPoint());
        const bool l = ed & Qt::LeftEdge, r = ed & Qt::RightEdge, t = ed & Qt::TopEdge, b = ed & Qt::BottomEdge;
        if ((l && t) || (r && b)) panel->setCursor(Qt::SizeFDiagCursor);
        else if ((r && t) || (l && b)) panel->setCursor(Qt::SizeBDiagCursor);
        else if (l || r) panel->setCursor(Qt::SizeHorCursor);
        else if (t || b) panel->setCursor(Qt::SizeVerCursor);
        else panel->unsetCursor();
        break;
    }
    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() != Qt::LeftButton) break;
        if (s.widget_locked) return false;
        const QPoint pos = me->position().toPoint();
        const Qt::Edges ed = edges(pos);
        QWindow* handle = windowHandle();
        if (ed && handle) {
            handle->startSystemResize(ed);
            return true;
        }
        if (!interactive(pos) && handle) {
            handle->startSystemMove();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonDblClick: {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() != Qt::LeftButton) break;
        if (!interactive(me->position().toPoint())) {
            if (s.widget_double_click != u"none") Q_EMIT action(s.widget_double_click);
            return true;
        }
        break;
    }
    case QEvent::ContextMenu:
        showMenu(static_cast<QContextMenuEvent*>(e)->globalPos());
        return true;
    default:
        break;
    }
    return QWidget::eventFilter(obj, e);
}

void DesktopWidget::showMenu(const QPoint& global) {
    const Settings& s = *m_ctx->settings;
    QMenu m(this);
    m.addAction(T(QStringLiteral("Refresh")), this, [this] { Q_EMIT action(QStringLiteral("refresh")); });
    m.addAction(T(QStringLiteral("Open panel")), this, [this] { Q_EMIT action(QStringLiteral("panel")); });
    m.addSeparator();
    const struct {
        const char* label;
        bool on;
        const char* act;
    } toggles[] = {
        {F1_TR_NOOP("Lock position and size"), s.widget_locked, "toggle_lock"},
        {F1_TR_NOOP("Always on top"), s.widget_always_on_top, "toggle_top"},
        {F1_TR_NOOP("Click-through (turn off from the tray menu)"), s.widget_click_through, "toggle_click"},
    };
    for (const auto& t : toggles) {
        auto* a = m.addAction(T(QString::fromUtf8(t.label)));
        a->setCheckable(true);
        a->setChecked(t.on);
        const QString act = QString::fromLatin1(t.act);
        connect(a, &QAction::triggered, this, [this, act] { Q_EMIT action(act); });
    }
    m.addSeparator();
    m.addAction(T(QStringLiteral("Settings…")), this, [this] { Q_EMIT action(QStringLiteral("settings")); });
    m.addAction(T(QStringLiteral("Hide widget")), this, [this] { Q_EMIT action(QStringLiteral("hide")); });
    m.exec(global);
}

void DesktopWidget::keyPressEvent(QKeyEvent* e) {
    panel->keyPressEvent(e);   // as the Python widget: keys go to the panel
}

}  // namespace f1::ui
