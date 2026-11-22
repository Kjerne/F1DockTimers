#include "widgets.h"

#include "../core/i18n.h"

#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

namespace f1::ui {

const char* const FLAG_SVG =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" stroke-width=\"2\" "
    "stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M5 5a5 5 0 0 1 7 0a5 5 0 0 0 7 0v9a5 5 0 0 1 -7 0a5 5 0 0 0 "
    "-7 0v-9z\"/><path d=\"M5 21v-7\"/></svg>";
const char* const PIN_SVG =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" stroke-width=\"2\" "
    "stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M15 4.5l-4 4l-4 1.5l-1.5 1.5l7 7l1.5 -1.5l1.5 -4l4 -4\"/>"
    "<path d=\"M9 15l-4.5 4.5\"/><path d=\"M14.5 4l5.5 5.5\"/></svg>";
const char* const GEAR_SVG =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" stroke-width=\"2\" "
    "stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M10.325 4.317c.426 -1.756 2.924 -1.756 3.35 0a1.724 "
    "1.724 0 0 0 2.573 1.066c1.543 -.94 3.31 .826 2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756 .426 1.756 2.924 0 "
    "3.35a1.724 1.724 0 0 0 -1.066 2.573c.94 1.543 -.826 3.31 -2.37 2.37a1.724 1.724 0 0 0 -2.572 1.065c-.426 1.756 "
    "-2.924 1.756 -3.35 0a1.724 1.724 0 0 0 -2.573 -1.066c-1.543 .94 -3.31 -.826 -2.37 -2.37a1.724 1.724 0 0 0 "
    "-1.065 -2.572c-1.756 -.426 -1.756 -2.924 0 -3.35a1.724 1.724 0 0 0 1.066 -2.573c-.94 -1.543 .826 -3.31 2.37 "
    "-2.37c1 .608 2.296 .07 2.572 -1.065z\"/><path d=\"M9 12a3 3 0 1 0 6 0a3 3 0 0 0 -6 0\"/></svg>";
const char* const REFRESH_SVG =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" stroke-width=\"2\" "
    "stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M20 11a8.1 8.1 0 0 0 -15.5 -2m-.5 -4v4h4\"/>"
    "<path d=\"M4 13a8.1 8.1 0 0 0 15.5 2m.5 4v-4h-4\"/></svg>";

QPixmap svgPixmap(const QString& svg, int size, double dpr) {
    QSvgRenderer r(svg.toUtf8());
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p);
    p.end();
    pm.setDevicePixelRatio(dpr);
    return pm;
}

static QColor alpha(const QString& c, double a) {
    QColor q = QColor::fromString(c);
    q.setAlphaF(static_cast<float>(a));
    return q;
}

// ── Text ─────────────────────────────────────────────────────────────────────

Text::Text(const QString& text, const Theme& theme, double size, bool bold, const QString& color, Qt::Alignment align,
           double tracking, QWidget* parent)
    : QWidget(parent), m_text(text), m_font(theme.font(size, bold)),
      m_color(QColor::fromString(color.isEmpty() ? theme.onSurface : color)), m_align(align) {
    if (tracking) m_font.setLetterSpacing(QFont::AbsoluteSpacing, tracking * theme.scale);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void Text::setText(const QString& text) {
    if (text == m_text) return;
    m_text = text;
    updateGeometry();
    update();
}

void Text::setColor(const QString& color) {
    m_color = QColor::fromString(color);
    update();
}

void Text::setBold(bool bold) {
    if (m_font.bold() == bold) return;
    m_font.setBold(bold);
    updateGeometry();
    update();
}

QSize Text::sizeHint() const {
    const QFontMetrics fm(m_font);
    return {fm.horizontalAdvance(m_text) + 2, fm.height()};
}

QSize Text::minimumSizeHint() const {
    const QFontMetrics fm(m_font);
    return {std::min(fm.horizontalAdvance(m_text) + 2, fm.horizontalAdvance(QStringLiteral("…")) * 3), fm.height()};
}

void Text::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(m_font);
    p.setPen(m_color);
    const QFontMetrics fm(m_font);
    const QString shown = fm.elidedText(m_text, Qt::ElideRight, width());
    p.drawText(rect(), int(m_align | Qt::AlignVCenter), shown);
    if (shown != m_text && toolTip().isEmpty()) setToolTip(m_text);
}

void ClickText::enableClicks() {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setCursor(Qt::PointingHandCursor);
}

void ClickText::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) Q_EMIT clicked();
}

Wrap::Wrap(const QString& text, const Theme& theme, double size, const QString& color, bool bold, QWidget* parent)
    : QLabel(text, parent) {
    setWordWrap(true);
    setFont(theme.font(size, bold));
    setStyleSheet(QStringLiteral("color:%1; background:transparent;").arg(color.isEmpty() ? theme.muted : color));
    setTextInteractionFlags(Qt::NoTextInteraction);
    setTextFormat(Qt::PlainText);
}

// ── small pieces ─────────────────────────────────────────────────────────────

Chip::Chip(const QString& color, const Theme& theme, QWidget* parent) : QWidget(parent), m_c(QColor::fromString(color)) {
    setFixedSize(theme.px(3), theme.px(16));
}

void Chip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(m_c);
    const double r = std::min(width(), height()) / 3.0;
    p.drawRoundedRect(QRectF(rect()), r, r);
}

Divider::Divider(const Theme& theme, QWidget* parent) : QWidget(parent), m_c(QColor::fromString(theme.outline)) {
    setFixedHeight(1);
}

void Divider::paintEvent(QPaintEvent*) { QPainter(this).fillRect(rect(), m_c); }

PillButton::PillButton(const QString& text, const Theme& theme, bool selected, QWidget* parent)
    : QWidget(parent), m_theme(theme), m_text(text), m_selected(selected), m_font(theme.font(12.5)) {
    const QFontMetrics fm(m_font);
    setFixedHeight(theme.px(30));
    setFixedWidth(fm.horizontalAdvance(text) + theme.px(20));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(T(QStringLiteral("{text} tab"), {{"text", text}}));
    setToolTip(text);
}

void PillButton::setSelected(bool on) {
    m_selected = on;
    update();
}

void PillButton::enterEvent(QEnterEvent*) {
    m_hover = true;
    update();
}
void PillButton::leaveEvent(QEvent*) {
    m_hover = false;
    update();
}
void PillButton::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) Q_EMIT clicked();
}

void PillButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const int rad = m_theme.px(6);
    if (!isEnabled()) p.setOpacity(0.38);   // greyed out: nothing to do yet (e.g. Save with no changes)
    if (m_selected) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor::fromString(m_theme.accent));
        p.drawRoundedRect(r, rad, rad);
        p.setPen(QColor::fromString(m_theme.onAccent()));
    } else {
        // A thin outline, so an unselected tab still reads as something to click.
        p.setPen(QPen(m_hover ? QColor::fromString(m_theme.muted) : QColor::fromString(m_theme.outline), 1));
        p.setBrush(m_hover ? alpha(m_theme.onSurface, 0.08) : QColor(Qt::transparent));
        p.drawRoundedRect(r, rad, rad);
        p.setPen(QColor::fromString(m_theme.onSurface));
    }
    p.setFont(m_font);
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

IconButton::IconButton(const char* svg, const Theme& theme, const QString& tip, int size, QWidget* parent)
    : QWidget(parent), m_theme(theme), m_svg(QString::fromLatin1(svg)), m_size(theme.px(size)) {
    setFixedSize(m_size + theme.px(8), m_size + theme.px(8));
    setCursor(Qt::PointingHandCursor);
    setToolTip(tip);
    setAccessibleName(tip);
}

void IconButton::setActive(bool on) {
    m_active = on;
    update();
}
void IconButton::enterEvent(QEnterEvent*) {
    m_hover = true;
    update();
}
void IconButton::leaveEvent(QEvent*) {
    m_hover = false;
    update();
}
void IconButton::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) Q_EMIT clicked();
}

void IconButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_hover) {
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(m_theme.onSurface, 0.08));
        p.drawRoundedRect(QRectF(rect()), m_theme.px(5), m_theme.px(5));
    }
    const QString color = (m_active || m_hover) ? m_theme.accent : m_theme.muted;
    const QPixmap pm = svgPixmap(m_svg.arg(color), m_size, devicePixelRatioF());
    const int off = (width() - m_size) / 2;
    p.drawPixmap(off, off, pm);
}

FlagGlyph::FlagGlyph(const Theme& theme, const QString& color, int size, QWidget* parent)
    : QWidget(parent), m_color(color.isEmpty() ? theme.accent : color), m_size(theme.px(size)) {
    setFixedSize(m_size, m_size);
}

void FlagGlyph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawPixmap(0, 0, svgPixmap(QString::fromLatin1(FLAG_SVG).arg(m_color), m_size, devicePixelRatioF()));
}

// ── gantry and track ─────────────────────────────────────────────────────────

LightsGantry::LightsGantry(const Theme& theme, QWidget* parent) : QWidget(parent), m_theme(theme) {
    const int d = theme.px(12), gx = theme.px(7), gy = theme.px(3);
    setFixedSize(COLUMNS * d + (COLUMNS - 1) * gx, 2 * d + gy);
    setAccessibleName(T(QStringLiteral("Start lights")));
}

void LightsGantry::setState(int lit, bool go) {
    if (lit == m_lit && go == m_go) return;
    m_lit = lit;
    m_go = go;
    update();
}

void LightsGantry::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const int d = m_theme.px(12), gx = m_theme.px(7), gy = m_theme.px(3);
    for (int col = 0; col < COLUMNS; ++col) {
        const bool on = m_lit >= col + 1;
        QColor c = QColor::fromString(QLatin1StringView(on ? (m_go ? LIGHT_GO : LIGHT_ON) : LIGHT_OFF));
        if (!on) c.setAlphaF(0.55f);
        p.setBrush(c);
        for (int row = 0; row < 2; ++row) p.drawEllipse(QRectF(col * (d + gx), row * (d + gy), d, d));
    }
}

TrackView::TrackView(const Outline& points, const Theme& theme, int w, int h, QWidget* parent)
    : QWidget(parent), m_points(points), m_theme(theme) {
    setFixedSize(theme.px(w), theme.px(h));
    setAccessibleName(T(QStringLiteral("Circuit outline")));
}

void TrackView::animateIn(int ms, int drawMs) {
    m_fade = 0.0;
    m_reveal = drawMs ? 0.0 : 1.0;
    const int total = std::max(ms, drawMs);
    if (m_anim) m_anim->stop();
    m_anim = new QVariantAnimation(this);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->setDuration(total);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this, ms, drawMs, total](const QVariant& v) {
        const double t = v.toDouble() * total;
        m_fade = ms ? std::min(1.0, t / ms) : 1.0;
        m_reveal = drawMs ? std::min(1.0, t / drawMs) : 1.0;
        update();
    });
    m_anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void TrackView::paintEvent(QPaintEvent*) {
    if (m_points.size() < 2) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const TrackGeometry g = trackGeometry(m_points);
    const double s = std::min(width() / g.box.width(), height() / g.box.height());
    const double ox = (width() - g.box.width() * s) / 2 - g.box.x() * s;
    const double oy = (height() - g.box.height() * s) / 2 - g.box.y() * s;
    auto pt = [&](const QPointF& q) { return QPointF(ox + q.x() * s, oy + q.y() * s); };
    QPainterPath path(pt(m_points.first()));
    for (qsizetype i = 1; i < m_points.size(); ++i) path.lineTo(pt(m_points[i]));
    const QColor color = QColor::fromString(m_theme.onSurface);
    p.setOpacity(0.85 * m_fade);
    QPen pen(color, g.stroke * s);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    if (m_reveal < 1.0) {
        double length = path.length();
        if (length <= 0) length = 1.0;
        const double unit = g.stroke * s;
        pen.setDashPattern({length / unit * m_reveal + 0.01, length / unit + 1});
    }
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    QPen tick(color, g.stroke * s * 1.8);
    tick.setCapStyle(Qt::FlatCap);
    p.setPen(tick);
    p.drawLine(pt(g.tickA), pt(g.tickB));
}

// ── rows ─────────────────────────────────────────────────────────────────────

Row::Row(const Theme& theme, const QString& url, std::function<void(const QString&)> openLink, QWidget* parent)
    : QWidget(parent), lay(new QHBoxLayout(this)), m_theme(theme), m_url(url), m_openLink(std::move(openLink)) {
    lay->setContentsMargins(theme.px(4), theme.px(4), theme.px(4), theme.px(4));
    lay->setSpacing(theme.px(8));
    if (!url.isEmpty()) {
        setCursor(Qt::PointingHandCursor);
        setToolTip(T(QStringLiteral("Open on Wikipedia")));
    }
}

QWidget* Row::cell(QWidget* w, int width, int stretch) {
    if (width >= 0) w->setFixedWidth(m_theme.px(width));
    lay->addWidget(w, stretch);
    return w;
}

void Row::enterEvent(QEnterEvent*) {
    if (m_url.isEmpty()) return;
    m_hover = true;
    update();
}
void Row::leaveEvent(QEvent*) {
    m_hover = false;
    update();
}
void Row::mouseReleaseEvent(QMouseEvent* e) {
    if (!m_url.isEmpty() && m_openLink && e->button() == Qt::LeftButton) m_openLink(m_url);
}

void Row::paintEvent(QPaintEvent*) {
    if (!m_hover) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(alpha(m_theme.onSurface, 0.06));
    p.drawRoundedRect(QRectF(rect()), m_theme.px(4), m_theme.px(4));
}

QWidget* withHelp(QWidget* label, const Theme& theme) {
    auto* box = new QWidget();
    auto* lay = new QHBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(theme.px(4));
    lay->addWidget(label);
    lay->addWidget(new Text(QStringLiteral("(?)"), theme, 9, false, theme.muted));
    lay->addStretch(1);
    box->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    return box;
}

namespace {
class TooltipWrapper : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() != QEvent::ToolTipChange || !obj->isWidgetType()) return false;
        auto* w = static_cast<QWidget*>(obj);
        const QString text = w->toolTip();
        if (text.isEmpty() || text.trimmed().startsWith(u'<')) return false;
        QStringList out;
        bool changed = false;
        for (const QString& line : text.split(u'\n')) {
            if (line.size() <= 70) {
                out << line;
                continue;
            }
            changed = true;
            QString cur;
            for (const QString& word : line.split(u' ', Qt::SkipEmptyParts)) {
                if (!cur.isEmpty() && cur.size() + 1 + word.size() > 70) {
                    out << cur;
                    cur.clear();
                }
                cur += (cur.isEmpty() ? QString() : QStringLiteral(" ")) + word;
            }
            if (!cur.isEmpty()) out << cur;
        }
        if (changed) w->setToolTip(out.join(u'\n'));
        return false;
    }
};
}  // namespace

void wrapTooltips(QObject* app) {
    static TooltipWrapper* wrapper = nullptr;
    if (!wrapper) {
        wrapper = new TooltipWrapper(app);
        app->installEventFilter(wrapper);
    }
}

}  // namespace f1::ui
