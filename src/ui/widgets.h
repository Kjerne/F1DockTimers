#pragma once
// Small painted building blocks recreating Noctalia's ui.* primitives (port of ui/widgets.py).

#include "../core/tracks.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>
#include <functional>

namespace f1::ui {

// Tabler icons (MIT); "%1" is the stroke colour.
extern const char* const FLAG_SVG;
extern const char* const PIN_SVG;
extern const char* const GEAR_SVG;
extern const char* const REFRESH_SVG;

QPixmap svgPixmap(const QString& svg, int size, double dpr = 1.0);

// Single-line label that elides instead of clipping (ui.label maxLines=1).
class Text : public QWidget {
    Q_OBJECT
public:
    Text(const QString& text, const Theme& theme, double size = 13, bool bold = false, const QString& color = {},
         Qt::Alignment align = Qt::AlignLeft, double tracking = 0.0, QWidget* parent = nullptr);
    void setText(const QString& text);
    QString text() const { return m_text; }
    void setColor(const QString& color);
    void setBold(bool bold);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    QString m_text;
    QFont m_font;
    QColor m_color;
    Qt::Alignment m_align;
};

// Text that acts as a link (e.g. "Update available").
class ClickText : public Text {
    Q_OBJECT
public:
    using Text::Text;
    void enableClicks();
Q_SIGNALS:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* e) override;
};

// Multi-line hint text (ui.label maxLines=2..3).
class Wrap : public QLabel {
    Q_OBJECT
public:
    Wrap(const QString& text, const Theme& theme, double size = 12, const QString& color = {}, bool bold = false,
         QWidget* parent = nullptr);
};

// 3x16 team colour bar.
class Chip : public QWidget {
    Q_OBJECT
public:
    Chip(const QString& color, const Theme& theme, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QColor m_c;
};

class Divider : public QWidget {
    Q_OBJECT
public:
    explicit Divider(const Theme& theme, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QColor m_c;
};

// Tab button: ghost, or filled with the accent when selected (controlSize sm).
class PillButton : public QWidget {
    Q_OBJECT
public:
    PillButton(const QString& text, const Theme& theme, bool selected = false, QWidget* parent = nullptr);
Q_SIGNALS:
    void clicked();

protected:
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent*) override;

private:
    const Theme& m_theme;
    QString m_text;
    bool m_selected, m_hover = false;
    QFont m_font;
};

// Tiny glyph button for the header (pin, settings, refresh).
class IconButton : public QWidget {
    Q_OBJECT
public:
    IconButton(const char* svg, const Theme& theme, const QString& tip, int size = 16, QWidget* parent = nullptr);
    void setActive(bool on);
Q_SIGNALS:
    void clicked();

protected:
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent*) override;

private:
    const Theme& m_theme;
    QString m_svg;
    int m_size;
    bool m_hover = false, m_active = false;
};

class FlagGlyph : public QWidget {
    Q_OBJECT
public:
    FlagGlyph(const Theme& theme, const QString& color, int size = 16, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString m_color;
    int m_size;
};

// Five columns of two lamps: red until lights out, green once the race runs.
class LightsGantry : public QWidget {
    Q_OBJECT
public:
    static constexpr int COLUMNS = 5;
    explicit LightsGantry(const Theme& theme, QWidget* parent = nullptr);
    void setState(int lit, bool go);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    const Theme& m_theme;
    int m_lit = COLUMNS;
    bool m_go = false;
};

// Circuit outline painted from tracks.txt geometry, fading (and drawing) in like the original.
class TrackView : public QWidget {
    Q_OBJECT
public:
    TrackView(const Outline& points, const Theme& theme, int w = 170, int h = 150, QWidget* parent = nullptr);
    void animateIn(int ms = 280, int drawMs = 0);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Outline m_points;
    const Theme& m_theme;
    double m_fade = 1.0, m_reveal = 1.0;
    QPointer<QVariantAnimation> m_anim;
};

// A table row: hover highlight; opens its Wikipedia page when it has one.
class Row : public QWidget {
    Q_OBJECT
public:
    Row(const Theme& theme, const QString& url = {}, std::function<void(const QString&)> openLink = {},
        QWidget* parent = nullptr);
    QWidget* cell(QWidget* w, int width = -1, int stretch = 0);
    QHBoxLayout* lay;
    const QString& url() const { return m_url; }

protected:
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent*) override;

private:
    const Theme& m_theme;
    QString m_url;
    std::function<void(const QString&)> m_openLink;
    bool m_hover = false;
};

// `label` followed by a small muted "(?)": hover the row for an explanation.
QWidget* withHelp(QWidget* label, const Theme& theme);

// Plain-text tooltips don't wrap in Qt; re-flow long ones to about 70 characters per line.
void wrapTooltips(QObject* app);

}  // namespace f1::ui
