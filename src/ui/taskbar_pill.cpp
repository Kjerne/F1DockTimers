#include "taskbar_pill.h"

#include "../core/i18n.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSettings>
#include <QToolTip>
#include <QCursor>
#include <QElapsedTimer>
#include <QImage>

namespace f1::ui {

namespace {

struct WinTheme {
    bool dark = true;
    std::optional<QString> accent;
};

WinTheme windowsTheme() {
    WinTheme out;
    QSettings personalize(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                          QSettings::NativeFormat);
    if (personalize.contains(QStringLiteral("SystemUsesLightTheme")))
        out.dark = personalize.value(QStringLiteral("SystemUsesLightTheme")).toInt() == 0;
    QSettings dwm(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"), QSettings::NativeFormat);
    if (dwm.contains(QStringLiteral("AccentColor"))) {
        const quint32 abgr = dwm.value(QStringLiteral("AccentColor")).toUInt();
        out.accent = QColor(abgr & 0xFF, (abgr >> 8) & 0xFF, (abgr >> 16) & 0xFF).name();
    }
    return out;
}

QColor withAlpha(QColor c, double a) {
    c.setAlphaF(static_cast<float>(a));
    return c;
}

}  // namespace

PillStyle PillStyle::resolve(const Settings& s, const Theme& theme) {
    PillStyle st;
    st.align = s.pill_align == u"center" ? Qt::AlignHCenter : Qt::AlignLeft;
    st.radius = std::max(0, s.pill_radius);
    st.boldTop = s.pill_bold_top;
    st.family = theme.family;
    if (s.pill_style == u"taskbar") {
        const WinTheme win = windowsTheme();
        st.top = QColor::fromString(win.dark ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000"));
        st.bottom = QColor::fromString(win.dark ? QStringLiteral("#C5C5C5") : QStringLiteral("#3A3A3A"));
        st.bg = withAlpha(QColor(0, 0, 0), 0.004);   // invisible, but still clickable
        st.hover = withAlpha(win.dark ? QColor(255, 255, 255) : QColor(0, 0, 0), 0.08);
        st.live = QColor::fromString(win.accent.value_or(theme.accent));
        return st;
    }
    if (s.pill_style == u"custom") {
        st.bg = withAlpha(QColor::fromString(s.pill_bg), std::max(0.004, s.pill_bg_opacity / 100.0));
        if (s.pill_border) st.border = QColor::fromString(s.pill_border_color);
        st.hover = withAlpha(QColor::fromString(s.pill_text), 0.08);
        st.top = QColor::fromString(s.pill_text);
        st.bottom = QColor::fromString(s.pill_text2);
        st.live = QColor::fromString(s.pill_live_color);
        return st;
    }
    st.bg = withAlpha(QColor::fromString(theme.surface), 0.90);
    st.hover = withAlpha(QColor::fromString(theme.onSurface), 0.07);
    if (s.pill_border) st.border = QColor::fromString(theme.outline);
    st.top = QColor::fromString(theme.onSurface);
    st.bottom = QColor::fromString(theme.accent);
    st.live = QColor::fromString(theme.accent);
    return st;
}

QList<QFont> fitFont(const QString& family, const QStringList& lines, double width, double height, double maxPx,
                     bool autofit, bool boldFirst) {
    auto fonts = [&](double px) {
        QList<QFont> out;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            QFont f(family);
            f.setPixelSize(std::max(6, static_cast<int>(std::nearbyint(px))));
            f.setBold(boldFirst && i == 0);
            f.setHintingPreference(QFont::PreferNoHinting);
            out.append(f);
        }
        return out;
    };
    double px = maxPx;
    if (!autofit) return fonts(px);
    while (px > 7) {
        const QList<QFont> fs = fonts(px);
        double tall = 0, wide = 0;
        for (qsizetype i = 0; i < fs.size(); ++i) {
            const QFontMetricsF fm(fs[i]);
            tall += fm.height();
            wide = std::max(wide, fm.horizontalAdvance(lines[i]));
        }
        if (tall * 0.92 <= height && wide <= width) return fs;
        px -= 0.5;
    }
    return fonts(7);
}

void paintPill(QPainter& p, const QRectF& rect, const PillStyle& style, const QString& top, const QString& bottom,
               bool live, bool hover, double maxPx, bool autofit, double pad) {
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    const double rad = std::min(style.radius, r.height() / 2);
    if (rad > 0) path.addRoundedRect(r, rad, rad);
    else path.addRect(r);
    p.fillPath(path, style.bg);
    if (hover) p.fillPath(path, style.hover);
    const std::optional<QColor> border = live ? std::optional<QColor>(style.live) : style.border;
    if (border) {
        p.setPen(*border);
        p.drawPath(path);
    }
    QStringList lines{top};
    if (!bottom.isEmpty()) lines << bottom;
    const double innerW = rect.width() - 2 * pad, innerH = rect.height() - 2;
    const QList<QFont> fonts = fitFont(style.family, lines, innerW, innerH, maxPx, autofit, style.boldTop);
    QList<double> heights;
    double total = 0;
    for (const QFont& f : fonts) {
        heights << QFontMetricsF(f).height() * (lines.size() > 1 ? 0.92 : 1.0);
        total += heights.last();
    }
    double y = rect.top() + (rect.height() - total) / 2;
    const QColor colors[2] = {live ? style.live : style.top, live ? style.top : style.bottom};
    for (qsizetype i = 0; i < lines.size(); ++i) {
        p.setFont(fonts[i]);
        p.setPen(colors[i]);
        const QString shown = QFontMetricsF(fonts[i]).elidedText(lines[i], Qt::ElideRight, innerW);
        p.drawText(QRectF(rect.left() + pad, y, innerW, heights[i]), int(style.align | Qt::AlignVCenter), shown);
        y += heights[i];
    }
}

// ── preview ──────────────────────────────────────────────────────────────────

PillPreview::PillPreview(QWidget* parent) : QWidget(parent) { setMinimumHeight(56); }

void PillPreview::setPreview(const PillStyle& style, const pill::State& lines, QSize size, double maxPx, bool autofit,
                             const QColor& bar) {
    m_style = style;
    m_lines = lines;
    m_size = size;
    m_maxPx = maxPx;
    m_autofit = autofit;
    m_bar = bar;
    update();
}

void PillPreview::paintEvent(QPaintEvent*) {
    if (!m_style) return;
    QPainter p(this);
    p.fillRect(rect(), m_bar);
    const double w = m_size.width(), h = m_size.height();
    const QRectF r(width() - w - 96, (height() - h) / 2, w, h);
    const double pad = m_style->radius > 0 ? std::max(4.0, std::min(14.0, h * 0.3)) : 6.0;
    paintPill(p, r, *m_style, m_lines.top, m_lines.bottom, m_lines.live, false, m_maxPx, m_autofit, pad);
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255));
    p.drawText(QRectF(width() - 92, 0, 88, height()), Qt::AlignCenter, QStringLiteral("^  ENG  19:10"));
}

// ── window ───────────────────────────────────────────────────────────────────

TaskbarPill::TaskbarPill(DataStore* store, Settings* settings, const Theme* theme, const Zone* zone,
                         std::function<QRect()> trayRect, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint |
                          Qt::WindowDoesNotAcceptFocus),
      m_store(store), m_settings(settings), m_theme(theme), m_zone(zone), m_trayRect(std::move(trayRect)) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setAccessibleName(T(QStringLiteral("Formula 1 countdown")));
    m_style = PillStyle::resolve(*settings, *theme);
    m_guard.setInterval(2000);
    connect(&m_guard, &QTimer::timeout, this, &TaskbarPill::guard);
    // Clicking the taskbar lifts it over us; re-raise (cheap SetWindowPos only).
    m_raiser.setInterval(2000);
    connect(&m_raiser, &QTimer::timeout, this, &TaskbarPill::raiseTop);
}

TaskbarPill::~TaskbarPill() { dropEmbed(); }

void TaskbarPill::start() {
    setMode();
    restyle();
    if (!m_embed) {
        show();
        m_raiser.start();
    }
    m_guard.start();
    if (!m_fgWatch) m_fgWatch = std::make_unique<winshell::ForegroundWatch>([this] { foregroundChanged(); });
    QTimer::singleShot(50, this, &TaskbarPill::guard);
}

void TaskbarPill::stop() {
    m_guard.stop();
    m_raiser.stop();
    dropEmbed();   // embedded: leave the taskbar as we found it
    m_fgWatch.reset();
    hide();
}

void TaskbarPill::foregroundChanged() {
    // The taskbar (or its flyout) raises itself right away and again after its animation.
    if (m_embed) {
        QTimer::singleShot(150, this, &TaskbarPill::guard);   // inside the taskbar: one look is enough
        return;
    }
    for (int ms : {0, 150, 500}) QTimer::singleShot(ms, this, &TaskbarPill::guard);
}

void TaskbarPill::raiseTop() {
    if (m_embed) {
        m_embed->keepOnTop();
        return;
    }
    // Never jump over an open menu (ours or another app's).
    if (isVisible() && !m_press && !QApplication::activePopupWidget() && !winshell::menuIsOpen())
        winshell::keepTopmost(static_cast<quintptr>(winId()));
}

// ── embedded mode ────────────────────────────────────────────────────────────

double TaskbarPill::dpr() const {
    QScreen* sc = QGuiApplication::primaryScreen();
    return sc ? sc->devicePixelRatio() : 1.0;
}

void TaskbarPill::setMode() {
    // Switch between floating and embedded to match the setting (falls back to floating).
    const bool want = m_settings->pill_embed;
    if (want && !m_embed && !m_embedFailed) {
        EmbeddedHost::Callbacks cb;
        cb.onPress = [this](int x) {
            QToolTip::hideText();
            pressAt(QCursor::pos(), QPoint(qRound(x / dpr()), 0));
        };
        cb.onMove = [this](int, bool) { moveAt(QCursor::pos()); };
        cb.onRelease = [this](int) { releaseAt(Qt::LeftButton, QCursor::pos()); };
        cb.onDouble = [this](int x) { doubleAt(QPoint(qRound(x / dpr()), 0)); };
        cb.onRight = [this] {
            QToolTip::hideText();
            Q_EMIT menuRequested(QCursor::pos());
        };
        cb.onHover = [this](bool on) {
            m_hover = on;
            renderEmbed();
            if (on) QTimer::singleShot(700, this, &TaskbarPill::embedTooltip);
            else QToolTip::hideText();
        };
        cb.resizeCursor = [this](int x) { return onEdge(QPoint(qRound(x / dpr()), 0)); };
        auto host = std::make_unique<EmbeddedHost>(std::move(cb));
        if (host->create()) {
            m_embed = std::move(host);
            m_embedKey.clear();
            m_guard.setInterval(3000);   // follows other widgets' width changes
            m_raiser.stop();
            m_fsHidden = false;
            if (isVisible()) hide();
        } else {
            m_embedFailed = true;   // no retry every 2 s; floating mode instead
        }
    } else if (!want && m_embed) {
        dropEmbed();
        m_guard.setInterval(2000);
        if (m_settings->taskbar_pill && m_guard.isActive()) {
            show();
            m_raiser.start();
        }
    }
}

void TaskbarPill::dropEmbed() {
    if (m_embed) {
        winshell::releaseTaskButtons(m_embed->bar, m_embed->windowRect());
        m_embed->destroy();
        m_embed.reset();
    }
    QToolTip::hideText();
}

void TaskbarPill::refresh() {
    if (m_embed) renderEmbed();
    else update();
}

void TaskbarPill::renderEmbed(bool force) {
    if (!m_embed) return;
    const QRect g = geometry();
    QScreen* sc = QGuiApplication::primaryScreen();
    const double d = sc->devicePixelRatio();
    const QRect sg = sc->geometry();
    const QString key = QStringLiteral("%1,%2,%3,%4,%5|").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()).arg(d) + m_state.top +
                        u'|' + m_state.bottom + u'|' + QString::number(m_state.live) + QString::number(m_hover) +
                        m_style.bg.name(QColor::HexArgb) + m_style.top.name() + m_style.bottom.name() + m_style.live.name() +
                        QString::number(m_style.radius) + QString::number(maxPx());
    if (!force && key == m_embedKey) return;
    m_embedKey = key;
    QImage img(std::max(1, qRound(g.width() * d)), std::max(1, qRound(g.height() * d)), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(d);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        paintPill(p, QRectF(0, 0, g.width(), g.height()), m_style, m_state.top, m_state.bottom, m_state.live, m_hover, maxPx(),
                  m_settings->pill_autofit, pad());
    }
    const int x = qRound(sg.x() + (g.x() - sg.x()) * d), y = qRound(sg.y() + (g.y() - sg.y()) * d);
    m_embed->showImage(img, x, y);
    fitButtons(x);
}

void TaskbarPill::fitButtons(std::optional<int> x) {
    // Classic taskbar: keep the app buttons from sliding under us (Windows 11 is left alone).
    if (!m_embed) return;
    if (!x) {
        const auto r = m_embed->windowRect();
        if (!r) return;
        x = r->left();
    }
    winshell::fitTaskButtons(m_embed->bar, *x - 2);
}

void TaskbarPill::embedTooltip() {
    if (m_embed && m_hover && !m_press) QToolTip::showText(QCursor::pos(), toolTip());
}

void TaskbarPill::restyle() {
    setMode();
    m_style = PillStyle::resolve(*m_settings, *m_theme);
    tick(nowSec(), true);
    place();
    refresh();
}

void TaskbarPill::tick(qint64 now, bool force) {
    const pill::State st = pill::state(m_store->race(), m_store->get(QStringLiteral("live")), *m_settings, now, *m_zone,
                                       m_store->forecast());
    if (!force && st == m_state) return;
    m_state = st;
    const Race* race = m_store->race();
    setToolTip((race ? T(QStringLiteral("{name}\n"), {{"name", race->name}}) : QString()) +
               (m_state.top + u'\n' + m_state.bottom).trimmed() +
               T(QStringLiteral("\n\nClick: panel · Drag: move · Drag left edge: resize · Right-click: menu")));
    if (m_settings->pill_width <= 0) place();
    refresh();
}

// ── geometry ─────────────────────────────────────────────────────────────────

std::optional<TaskbarPill::Anchor> TaskbarPill::anchor() const {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return std::nullopt;
    if (auto rects = winshell::taskbarRects(myRect())) {
        if (rects->anchor.isValid()) {
            static QElapsedTimer clock;
            if (!clock.isValid()) clock.start();
            rects->anchor = steadyAnchor(rects->bar, rects->anchor, clock.elapsed() / 1000.0);
        }
        const double d = screen->devicePixelRatio();
        const QRect sg = screen->geometry();
        auto logical = [&](const QRect& r) {
            return QRect(qRound(sg.x() + (r.x() - sg.x()) / d), qRound(sg.y() + (r.y() - sg.y()) / d), qRound(r.width() / d),
                         qRound(r.height() / d));
        };
        const QRect bar = logical(rects->bar);
        // "anchor" = the tray plus other apps' widgets packed beside it: we go next to them.
        const QRect edge = m_settings->pill_auto_dock && rects->anchor.isValid() ? rects->anchor : rects->tray;
        QRect tray = edge.isValid() ? logical(edge) : QRect();
        if (!tray.isValid()) {
            const QRect icon = m_trayRect();
            tray = icon.isValid() ? icon : QRect(bar.right() - 200, bar.top(), 200, bar.height());
        }
        return Anchor{bar, tray, bar.height() > bar.width()};
    }
    const QRect icon = m_trayRect();
    const QRect avail = screen->availableGeometry(), full = screen->geometry();
    if (avail.bottom() < full.bottom()) {
        const QRect bar(full.left(), avail.bottom() + 1, full.width(), full.bottom() - avail.bottom());
        return Anchor{bar, icon.isValid() ? icon : QRect(bar.right() - 260, bar.top(), 260, bar.height()), false};
    }
    return std::nullopt;
}

std::optional<QRect> TaskbarPill::myRect() const {
    if (m_embed) return winshell::windowVisible(m_embed->hwnd) ? m_embed->windowRect() : std::nullopt;
    return isVisible() ? winshell::windowRect(static_cast<quintptr>(winId())) : std::nullopt;
}

QRect TaskbarPill::steadyAnchor(const QRect& bar, const QRect& anchor, double now) const {
    const bool vertical = bar.height() > bar.width();   // the tray is right (or at the bottom)
    const int edge = vertical ? anchor.top() : anchor.left();
    if (!m_dockEdge || m_dockEdge->first != bar || edge <= m_dockEdge->second || m_press) {
        m_dockEdge = std::pair{bar, edge};   // first time, pushed outward, taskbar changed, or being dragged
        m_dockSince.reset();
        return anchor;
    }
    if (!m_dockSince) m_dockSince = now;
    if (now - *m_dockSince >= DOCK_SETTLE_S) {   // the gap stayed open: move into it
        m_dockEdge = std::pair{bar, edge};
        m_dockSince.reset();
        return anchor;
    }
    QRect held = anchor;
    if (vertical) held.setTop(m_dockEdge->second);
    else held.setLeft(m_dockEdge->second);
    return held;
}

double TaskbarPill::maxPx() const { return m_settings->pill_font_px * m_theme->scale; }

double TaskbarPill::pad() const { return m_style.radius > 0 ? std::max(4.0, std::min(14.0, height() * 0.3)) : 6.0; }

int TaskbarPill::autoWidth(int h) const {
    QStringList lines{m_state.top};
    if (!m_state.bottom.isEmpty()) lines << m_state.bottom;
    const QList<QFont> fonts = fitFont(m_style.family, lines, 10'000, h - 2, maxPx(), m_settings->pill_autofit, m_style.boldTop);
    double wide = 0;
    for (qsizetype i = 0; i < fonts.size(); ++i) wide = std::max(wide, QFontMetricsF(fonts[i]).horizontalAdvance(lines[i]));
    return qRound(wide + 2 * pad() + 2);
}

int TaskbarPill::offset() const {
    // Docking beside another widget ignores negative offsets: that widget would move away and the
    // two would chase each other every second.
    const int off = m_settings->taskbar_pill_offset;
    return m_settings->pill_auto_dock ? std::max(0, off) : off;
}

void TaskbarPill::place() {
    const Settings& s = *m_settings;
    const auto a = anchor();
    if (!a) {
        resize(std::max(60, s.pill_width ? s.pill_width : 160), std::max(20, s.pill_height ? s.pill_height : 34));
        return;
    }
    const QRect& bar = a->bar;
    if (a->vertical) {
        const int h = s.pill_height > 0 ? s.pill_height : 40;
        const int w = std::max(40, bar.width() - 6);
        const int y = std::max(bar.top() + 2, std::min(bar.bottom() - h - 1, a->tray.top() - h - 6 - offset()));
        setGeometry(bar.left() + 3, y, w, h);
        renderEmbed();
        return;
    }
    int h;
    if (s.pill_height == -1) h = bar.height();
    else if (s.pill_height > 0) h = std::min(s.pill_height, bar.height());
    else h = std::max(20, std::min(44, bar.height() - 6));
    int w = s.pill_width > 0 ? s.pill_width : autoWidth(h);
    w = std::max(40, std::min(w, bar.width() / 2));
    const int x = std::max(bar.left() + 2, std::min(bar.right() - w - 1, a->tray.left() - w - 6 - offset()));
    setGeometry(x, bar.top() + (bar.height() - h) / 2, w, h);
    renderEmbed();
}

bool TaskbarPill::taskbarUnderMe() const {
    const QRect g = geometry();
    QScreen* sc = screen() ? screen() : QGuiApplication::primaryScreen();
    const double d = sc->devicePixelRatio();
    const QRect sg = sc->geometry();
    return winshell::taskbarIsVisibleAt(qRound(sg.x() + (g.center().x() - sg.x()) * d), qRound(sg.y() + (g.center().y() - sg.y()) * d));
}

void TaskbarPill::guard() {
    if (!m_settings->taskbar_pill || m_press) return;
    if (m_embed) {
        // Inside the taskbar Windows hides us with it (fullscreen, auto-hide); just stay alive across
        // Explorer restarts, follow the tray's width and stay above our siblings.
        if (!m_embed->alive()) {
            m_embed->destroy();
            if (!m_embed->create()) {
                m_embed.reset();
                m_embedFailed = true;
                show();
                m_raiser.start();
                return;
            }
            m_embedKey.clear();
        }
        place();
        fitButtons();   // Explorer re-widens them on its own layout passes
        m_embed->keepOnTop();
        return;
    }
    const bool covered = winshell::foregroundIsFullscreen() || !taskbarUnderMe();
    if (covered) {
        if (isVisible()) {
            m_fsHidden = true;
            hide();
        }
        return;
    }
    if (m_fsHidden) {
        m_fsHidden = false;
        show();
    }
    if (isVisible()) {
        place();
        raiseTop();
    }
}

// ── input ────────────────────────────────────────────────────────────────────

void TaskbarPill::enterEvent(QEnterEvent*) {
    m_hover = true;
    update();
}

void TaskbarPill::leaveEvent(QEvent*) {
    m_hover = false;
    unsetCursor();
    update();
}

void TaskbarPill::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) pressAt(e->globalPosition().toPoint(), e->position().toPoint());
}

void TaskbarPill::mouseMoveEvent(QMouseEvent* e) {
    if (!m_press) {
        setCursor(onEdge(e->position().toPoint()) ? Qt::SizeHorCursor : Qt::PointingHandCursor);
        return;
    }
    moveAt(e->globalPosition().toPoint());
}

void TaskbarPill::mouseReleaseEvent(QMouseEvent* e) { releaseAt(e->button(), e->globalPosition().toPoint()); }

void TaskbarPill::mouseDoubleClickEvent(QMouseEvent* e) { doubleAt(e->position().toPoint()); }

void TaskbarPill::pressAt(const QPoint& global, const QPoint& local) {
    m_press = global;
    m_pressOffset = m_settings->taskbar_pill_offset;
    m_pressWidth = width();
    m_mode = onEdge(local) ? QStringLiteral("resize") : QStringLiteral("move");
    m_moved = false;
}

void TaskbarPill::moveAt(const QPoint& global) {
    if (!m_press) return;
    const QPoint d = global - *m_press;
    if (std::abs(d.x()) > 3 || std::abs(d.y()) > 3) m_moved = true;
    if (!m_moved) return;
    const auto a = anchor();
    const bool vertical = a && a->vertical;
    if (m_mode == u"resize" && !vertical) {
        // Left edge drag: the right edge stays put, the width follows the mouse.
        m_settings->pill_width = std::max(40, std::min(900, m_pressWidth - d.x()));
    } else {
        const int delta = vertical ? -d.y() : -d.x();
        const int low = m_settings->pill_auto_dock ? 0 : -400;
        m_settings->taskbar_pill_offset = std::max(low, std::min(1600, m_pressOffset + delta));
    }
    place();
}

void TaskbarPill::releaseAt(Qt::MouseButton button, const QPoint& global) {
    if (button == Qt::LeftButton && m_press) {
        if (m_moved) Q_EMIT offsetChanged();
        else Q_EMIT clicked();
    } else if (button == Qt::RightButton) {
        Q_EMIT menuRequested(global);
    }
    m_press.reset();
}

void TaskbarPill::doubleAt(const QPoint& local) {
    if (onEdge(local)) {
        m_settings->pill_width = 0;   // back to automatic width
        place();
        Q_EMIT offsetChanged();
    }
}

void TaskbarPill::paintEvent(QPaintEvent*) {
    QPainter p(this);
    paintPill(p, QRectF(rect()), m_style, m_state.top, m_state.bottom, m_state.live, m_hover, maxPx(), m_settings->pill_autofit, pad());
}

}  // namespace f1::ui
