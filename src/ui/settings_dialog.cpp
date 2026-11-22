#include "settings_dialog.h"

#include "../core/i18n.h"
#include "../core/pilltext.h"
#include "../core/sessions.h"
#include "../core/timeutil.h"
#include "../core/weather.h"
#include "../paths.h"
#include "../services/poller.h"
#include "../services/wintrust.h"
#include "version.h"

#include <QAbstractSpinBox>
#include <QColorDialog>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimeZone>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace f1::ui {

namespace {

QString tr(const char* s) { return T(QString::fromUtf8(s)); }

// A small chevron for combo and spin boxes, written once to the cache folder.
QString arrowFile(const QString& color, bool up) {
    const QString path = QDir(paths::cacheDir())
                             .filePath(QStringLiteral("chevron_%1%2.svg").arg(up ? QStringLiteral("up_") : QStringLiteral("down_"),
                                                                            QString(color).remove(u'#')));
    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" "
                                   "stroke-width=\"3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"%2\"/></svg>")
                        .arg(color, up ? QStringLiteral("M6 15l6 -6l6 6") : QStringLiteral("M6 9l6 6l6 -6"))
                        .toUtf8());
    }
    return QDir::fromNativeSeparators(path);
}

class NoWheelUnlessFocused : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::Wheel && obj->isWidgetType() && !static_cast<QWidget*>(obj)->hasFocus()) {
            ev->ignore();
            return true;
        }
        return false;
    }
};

}  // namespace

QString dialogStylesheet(const Theme& t) {
    const QString arrow = arrowFile(t.muted, false), up = arrowFile(t.muted, true);
    QString css = QStringLiteral(R"(
    QDialog, QWidget#page { background:%SURFACE%; color:%ON%; }
    QLabel { color:%ON%; background:transparent; }
    QLabel[hint="true"] { color:%MUTED%; }
    QTabWidget::pane { border:1px solid %OUTLINE%; border-radius:8px; top:-1px; }
    QTabBar::tab { background:transparent; color:%ON%; padding:6px 12px; border-radius:6px; margin:2px; }
    QTabBar::tab:selected { background:%ACCENT%; color:%ONACCENT%; }
    QTabBar::tab:hover:!selected { background:rgba(228,225,236,0.08); }
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { background:#24243A; color:%ON%;
        border:1px solid %OUTLINE%; border-radius:6px; padding:4px 6px; selection-background-color:%ACCENT%;
        selection-color:%ONACCENT%; }
    QComboBox { combobox-popup: 0; padding-right:24px; }
    QComboBox:hover, QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color:%MUTED%; }
    QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color:%ACCENT%; }
    QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:center right; width:22px; border:none; }
    QComboBox::down-arrow { image:url(%ARROW%); width:10px; height:10px; }
    QComboBox QAbstractItemView { border:1px solid %OUTLINE%; outline:0; padding:2px; }
    QComboBox QAbstractItemView::item { min-height:24px; padding:2px 8px; }
    QComboBox QAbstractItemView { background:#24243A; color:%ON%; selection-background-color:%ACCENT%;
        selection-color:%ONACCENT%; }
    QSpinBox, QDoubleSpinBox { padding-right:22px; }
    QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin:border; subcontrol-position:top right;
        width:20px; border:none; border-left:1px solid %OUTLINE%; border-top-right-radius:6px; background:#2A2A40; }
    QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin:border; subcontrol-position:bottom right;
        width:20px; border:none; border-left:1px solid %OUTLINE%; border-bottom-right-radius:6px; background:#2A2A40; }
    QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
    QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background:#383852; }
    QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image:url(%UP%); width:9px; height:9px; }
    QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image:url(%ARROW%); width:9px; height:9px; }
    QToolTip { background:#24243A; color:%ON%; border:1px solid %OUTLINE%; padding:4px 6px; }
    QCheckBox { color:%ON%; spacing:8px; }
    QPushButton { background:#2A2A40; color:%ON%; border:1px solid %OUTLINE%; border-radius:6px;
        padding:5px 12px; }
    QPushButton:hover { background:#33334C; }
    QPushButton:default { background:%ACCENT%; color:%ONACCENT%; border:none; }
    QScrollArea { background:transparent; border:none; }
    )");
    css.replace(QStringLiteral("%SURFACE%"), t.surface).replace(QStringLiteral("%ON%"), t.onSurface);
    css.replace(QStringLiteral("%MUTED%"), t.muted).replace(QStringLiteral("%OUTLINE%"), t.outline);
    css.replace(QStringLiteral("%ACCENT%"), t.accent).replace(QStringLiteral("%ONACCENT%"), t.onAccent());
    css.replace(QStringLiteral("%ARROW%"), arrow).replace(QStringLiteral("%UP%"), up);
    return css;
}

QStringList zoneNames() {
    QStringList out;
    for (const QByteArray& id : QTimeZone::availableTimeZoneIds()) {
        const QString z = QString::fromUtf8(id);
        if (z.contains(u'/') && !z.startsWith(QStringLiteral("Etc/")) && !z.startsWith(QStringLiteral("SystemV/"))) out << z;
    }
    out.sort();
    out.removeDuplicates();
    return out.isEmpty() ? QStringList{QStringLiteral("Europe/Copenhagen"), QStringLiteral("Europe/London")} : out;
}

const QMap<QString, QString>& settingsTips() {
    static const QMap<QString, QString> tips = {
        // General
        {QStringLiteral("start_win"), tr("Launch F1DockTimers automatically when you sign in to Windows.")},
        {QStringLiteral("auto_update"), tr("Ask GitHub once a day whether a newer F1DockTimers exists. Sends no personal data.")},
        {QStringLiteral("start_min"), tr("On: start quietly in the tray. Off: open the panel at launch.")},
        {QStringLiteral("widget_on"), tr("A movable F1 panel that stays on your desktop.")},
        {QStringLiteral("default_tab"), tr("Tab the panel opens on. Live and Grid take over automatically during a session / after qualifying.")},
        {QStringLiteral("time_fmt"), tr("How session times are written: 14:00 or 02:00 PM.")},
        {QStringLiteral("tz"), tr("Zone for all session times. Automatic follows Windows. Click, then type to jump (e.g. \"Europe/Co\").")},
        {QStringLiteral("sched_h"), tr("How often the race calendar is checked for changes.")},
        {QStringLiteral("stand_h"), tr("How often driver/team standings and the last result are refreshed.")},
        // Appearance
        {QStringLiteral("accent_btn"), tr("Highlight colour: selected tab, countdown, favourites.")},
        {QStringLiteral("font_fam"), tr("Noto Sans is the original look; Monospace uses Cascadia Mono / Consolas.")},
        {QStringLiteral("font_scale"), tr("Text size in the panel and widget.")},
        {QStringLiteral("ui_scale"), tr("Size of the whole panel and widget, on top of Windows display scaling.")},
        {QStringLiteral("pop_op"), tr("How see-through the tray panel background is.")},
        {QStringLiteral("wid_op"), tr("How see-through the desktop widget background is (text stays solid).")},
        {QStringLiteral("anim"), tr("Circuit fade-in, start lights sequence and popup slide.")},
        {QStringLiteral("on_top"), tr("Keep the desktop widget above other windows.")},
        {QStringLiteral("click_through"), tr("Mouse clicks go through the widget. Turn this off again from the tray icon menu.")},
        {QStringLiteral("locked"), tr("Stop the widget from being moved or resized by accident.")},
        {QStringLiteral("remember"), tr("Restore the widget where you left it after a restart.")},
        {QStringLiteral("dbl"), tr("What a double-click on the desktop widget does.")},
        // Taskbar
        {QStringLiteral("pill_on"), tr("Small countdown next to the tray icons. Hides itself over fullscreen games.")},
        {QStringLiteral("p_style"), tr("Noctalia: dark box with accent text. Match taskbar: transparent with Windows colours. Custom: your colours.")},
        {QStringLiteral("p_bg"), tr("Background colour (Custom style).")},
        {QStringLiteral("p_bg_op"), tr("Background see-through amount (Custom style). 0 % = only text.")},
        {QStringLiteral("p_text"), tr("Colour of the first line (Custom style).")},
        {QStringLiteral("p_text2"), tr("Colour of the second line (Custom style).")},
        {QStringLiteral("p_live"), tr("Colour used while a session is live (Custom style).")},
        {QStringLiteral("p_border"), tr("Thin outline around the countdown box.")},
        {QStringLiteral("p_border_c"), tr("Outline colour (Custom style).")},
        {QStringLiteral("p_radius"), tr("Rounded corners. 0 = square box.")},
        {QStringLiteral("p_width"), tr("Fixed width, or Automatic to fit the text. You can also drag the box's left edge on the taskbar.")},
        {QStringLiteral("p_hmode"), tr("Automatic leaves a small gap, Full fills the taskbar height, Custom uses the pixel value.")},
        {QStringLiteral("p_height"), tr("Box height in pixels (Custom height).")},
        {QStringLiteral("p_offset"), tr("Distance from the tray icons. You can also drag the box along the taskbar.")},
        {QStringLiteral("p_font"), tr("Largest text size. With auto-fit the text shrinks when it would not fit.")},
        {QStringLiteral("p_autofit"), tr("Shrink the text so it always fits inside the box.")},
        {QStringLiteral("p_bold"), tr("Write the first line in bold.")},
        {QStringLiteral("p_align"), tr("Text alignment inside the box.")},
        {QStringLiteral("p_lines"), tr("One compact line or two lines.")},
        {QStringLiteral("p_preset"), tr("Ready-made layouts. Fills the four line fields below.")},
        {QStringLiteral("p_top"), tr("What the first line shows. Use the {tokens} listed below.")},
        {QStringLiteral("p_bottom"), tr("What the second line shows.")},
        {QStringLiteral("p_ltop"), tr("First line while a session is live.")},
        {QStringLiteral("p_lbottom"), tr("Second line while a session is live.")},
        // Formula 1
        {QStringLiteral("cd_target"),
         tr("Next race: big countdown always to the Grand Prix (LIGHTS OUT in 4d 9h).\nNext session: counts to whatever is next "
            "(NEXT · PRACTICE 1 in 2d 6h), LIGHTS OUT for the race.\nAlso used by the tray tooltip. The taskbar countdown always "
            "shows the next session.")},
        {QStringLiteral("fav_driver"), tr("Shown in the accent colour in every table.")},
        {QStringLiteral("fav_team"), tr("Team shown in the accent colour in every table.")},
        {QStringLiteral("team_colors"), tr("Coloured bar beside each driver and team.")},
        {QStringLiteral("n_race"), tr("Windows notification before the race starts.")},
        {QStringLiteral("n_quali"), tr("Windows notification before qualifying starts.")},
        {QStringLiteral("n_sprint"), tr("Windows notification before the sprint starts.")},
        {QStringLiteral("lead"), tr("How many minutes before the session the notification appears.")},
        {QStringLiteral("n_results"), tr("After qualifying, the sprint and the race: a notification with the top 3.\nNot shown while "
                                         "spoiler-free mode hides the results.")},
        {QStringLiteral("spoiler"), tr("Hides results, grid, live timing and standings once a qualifying, sprint or race has started, "
                                       "until you press Show results. For when you watch later.")},
        // Live data
        {QStringLiteral("live_mode"),
         tr("OpenF1 Free: no account; full results ~20–30 min after each session.\nFree + F1 live stream: live order and gaps "
            "during sessions from F1's own (unofficial) stream, full results from OpenF1 afterwards. May stop working at any time.\n"
            "OpenF1 Sponsor: official live data during sessions; needs a paid OpenF1 account.")},
        {QStringLiteral("user"), tr("Your OpenF1 account e-mail (Sponsor plan only).")},
        {QStringLiteral("pw"), tr("Stored in Windows Credential Manager, never in files or logs.")},
        {QStringLiteral("debug"), tr("Show Grid and Live outside race weekends using the last session's data. For testing.")},
    };
    return tips;
}

SettingsDialog::SettingsDialog(Settings* settings, DataStore* store, const Theme& theme, std::function<void()> save,
                               CredentialStore creds, QWidget* parent)
    : QDialog(parent), m_s(settings), m_store(store), m_t(theme), m_save(std::move(save)), m_creds(std::move(creds)) {
    setWindowTitle(T(QStringLiteral("{app_title} · Settings"), {{"app_title", QStringLiteral(F1_APP_TITLE)}}));
    setStyleSheet(dialogStylesheet(theme));
    resize(560, 620);
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget();
    auto page = [](QWidget* form) {
        auto* sa = new QScrollArea();
        sa->setWidgetResizable(true);
        form->setObjectName(QStringLiteral("page"));
        sa->setWidget(form);
        return sa;
    };
    tabs->addTab(page(general()), T(QStringLiteral("General")));
    tabs->addTab(page(appearance()), T(QStringLiteral("Appearance")));
    tabs->addTab(page(taskbar()), T(QStringLiteral("Taskbar")));
    tabs->addTab(page(formula1()), T(QStringLiteral("Formula 1")));
    tabs->addTab(page(live()), T(QStringLiteral("Live data")));
    tabs->addTab(page(about()), T(QStringLiteral("About")));
    root->addWidget(tabs);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Save)->setDefault(true);
    connect(bb, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAndClose);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);
    m_built = true;
    updatePreview();
    applyTips();
}

void SettingsDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    if (m_wheelGuard) return;
    m_wheelGuard = true;   // a scroll over the page must not change values under the mouse
    auto* guard = new NoWheelUnlessFocused(this);
    for (QWidget* w : findChildren<QWidget*>())
        if (qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSpinBox*>(w)) {
            w->setFocusPolicy(Qt::StrongFocus);
            w->installEventFilter(guard);
        }
}

void SettingsDialog::applyTips() {
    // Hover help on the field and on its label.
    const QList<QFormLayout*> forms = findChildren<QFormLayout*>();
    for (auto it = settingsTips().begin(); it != settingsTips().end(); ++it) {
        QWidget* w = m_controls.value(it.key());
        if (!w) continue;
        w->setToolTip(it.value());
        for (QFormLayout* f : forms)
            if (QWidget* lab = f->labelForField(w)) lab->setToolTip(it.value());
    }
}

QLabel* SettingsDialog::hintLabel(const QString& text) {
    auto* lab = new QLabel(text);
    lab->setWordWrap(true);
    lab->setProperty("hint", true);
    return lab;
}

QCheckBox* SettingsDialog::check(const QString& name, const QString& label, bool value) {
    auto* c = new QCheckBox(label);
    c->setChecked(value);
    reg(name, c);
    return c;
}

QComboBox* SettingsDialog::combo(const QString& name, const QList<std::pair<QString, QString>>& options, const QString& value) {
    auto* c = new QComboBox();
    for (const auto& [val, label] : options) c->addItem(label, val);
    c->setCurrentIndex(std::max(0, c->findData(value)));
    reg(name, c);
    return c;
}

QPushButton* SettingsDialog::colorButton(const QString& name, const QString& value) {
    auto* b = new QPushButton();
    auto show = [this, b](const QString& c) {
        const QString hex = QColor::fromString(c).name().toUpper();
        b->setProperty("color", hex);
        const QString fg = QColor::fromString(c).lightnessF() > 0.55 ? QStringLiteral("#000000") : QStringLiteral("#FFFFFF");
        b->setText(hex);
        b->setStyleSheet(QStringLiteral("background:%1; color:%2; border:1px solid %3;").arg(hex, fg, m_t.outline));
    };
    show(value);
    connect(b, &QPushButton::clicked, this, [this, b, show] {
        const QColor c = QColorDialog::getColor(QColor::fromString(b->property("color").toString()), this, T(QStringLiteral("Colour")));
        if (c.isValid()) {
            show(c.name());
            updatePreview();
        }
    });
    reg(name, b);
    return b;
}

QString SettingsDialog::colorOf(const QString& name) const { return m_controls.value(name)->property("color").toString(); }

// â”€â”€ pages â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

QWidget* SettingsDialog::general() {
    const Settings& s = *m_s;
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);
    auto* startWin = check(QStringLiteral("start_win"), T(QStringLiteral("Start with Windows")), s.start_with_windows);
    f->addRow(startWin);
    f->addRow(check(QStringLiteral("start_min"), T(QStringLiteral("Start minimized to the tray (no popup at launch)")), s.start_minimized));
    f->addRow(check(QStringLiteral("widget_on"), T(QStringLiteral("Show the desktop widget")), s.widget_enabled));
    f->addRow(T(QStringLiteral("Default tab")),
              combo(QStringLiteral("default_tab"),
                    {{QStringLiteral("schedule"), T(QStringLiteral("Schedule"))}, {QStringLiteral("drivers"), T(QStringLiteral("Drivers"))},
                     {QStringLiteral("constructors"), T(QStringLiteral("Teams"))}},
                    s.default_tab));
    f->addRow(T(QStringLiteral("Time format")),
              combo(QStringLiteral("time_fmt"),
                    {{QStringLiteral("24h"), T(QStringLiteral("24-hour"))}, {QStringLiteral("12h"), T(QStringLiteral("12-hour"))}},
                    s.time_format));
    auto* units = combo(QStringLiteral("units"),
                        {{QStringLiteral("metric"), T(QStringLiteral("Metric (°C, km/h)"))},
                         {QStringLiteral("imperial"), T(QStringLiteral("Imperial (°F, mph)"))}},
                        s.units);
    units->setToolTip(T(QStringLiteral("Units for the weather forecast and track conditions.")));
    f->addRow(T(QStringLiteral("Units")), units);
    auto* tz = new QComboBox();
    tz->setMaxVisibleItems(16);
    tz->setToolTip(T(QStringLiteral("Click, then type to jump, e.g. \"Europe/Co\"")));
    tz->addItem(T(QStringLiteral("Automatic (Windows: {system_zone_name})"), {{"system_zone_name", systemZoneName()}}),
                QStringLiteral("auto"));
    for (const QString& z : zoneNames()) tz->addItem(z, z);
    tz->setCurrentIndex(std::max(0, tz->findData(s.timezone)));
    reg(QStringLiteral("tz"), tz);
    f->addRow(T(QStringLiteral("Time zone")), tz);
    auto* lang = new QComboBox();
    lang->addItem(T(QStringLiteral("Automatic (Windows language)")), QStringLiteral("auto"));
    for (const auto& l : i18n::available({paths::builtinLangDir(), paths::userLangDir()})) lang->addItem(l.name, l.code);
    lang->setCurrentIndex(std::max(0, lang->findData(s.language)));
    reg(QStringLiteral("lang"), lang);
    auto* langFolder = new QPushButton(T(QStringLiteral("Open language folder")));
    langFolder->setToolTip(T(QStringLiteral("Add your own translation here: copy _template.txt, translate it and restart.")));
    connect(langFolder, &QPushButton::clicked, this, [] {
        // Create the user language folder with a fresh copy of the template, then open it.
        const QString folder = paths::userLangDir();
        QDir().mkpath(folder);
        const QString tpl = QDir(paths::builtinLangDir()).filePath(QStringLiteral("_template.txt"));
        const QString dst = QDir(folder).filePath(QStringLiteral("_template.txt"));
        if (QFile::exists(tpl)) {
            QFile::remove(dst);
            QFile::copy(tpl, dst);
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    auto* row = new QHBoxLayout();
    row->addWidget(lang, 1);
    row->addWidget(langFolder);
    f->addRow(T(QStringLiteral("Language")), row);
    f->addRow(hintLabel(T(QStringLiteral("A new language takes effect after restarting F1DockTimers."))));
    for (const auto& [name, value] : {std::pair{QStringLiteral("sched_h"), s.schedule_refresh_h},
                                      std::pair{QStringLiteral("stand_h"), s.standings_refresh_h}}) {
        auto* sp = new QDoubleSpinBox();
        sp->setRange(0.5, 48);
        sp->setSuffix(QStringLiteral(" h"));
        sp->setValue(value);
        reg(name, sp);
        f->addRow(name == u"sched_h" ? T(QStringLiteral("Schedule refresh")) : T(QStringLiteral("Standings refresh")), sp);
    }
    row = new QHBoxLayout();
    auto* b1 = new QPushButton(T(QStringLiteral("Refresh data now")));
    connect(b1, &QPushButton::clicked, this, &SettingsDialog::refreshRequested);
    auto* b2 = new QPushButton(T(QStringLiteral("Clear cache")));
    connect(b2, &QPushButton::clicked, this, [this] {
        Q_EMIT clearCacheRequested();
        QMessageBox::information(this, T(QStringLiteral("Cache")), T(QStringLiteral("Cache cleared. Data will be fetched again.")));
    });
    row->addWidget(b1);
    row->addWidget(b2);
    row->addStretch(1);
    f->addRow(row);
    QCheckBox* autoUpdate = check(QStringLiteral("auto_update"), T(QStringLiteral("Check for updates automatically (once a day)")), s.check_updates);
    if (wintrust::isPackaged()) {
        // Microsoft Store copy: the Store installs updates; the GitHub check is switched off.
        autoUpdate->hide();
        f->addRow(hintLabel(T(QStringLiteral("Version {version}. Updates come automatically from the Microsoft Store."),
                              {{"version", QStringLiteral(F1_VERSION)}})));
    } else {
        f->addRow(autoUpdate);
        row = new QHBoxLayout();
        auto* b5 = new QPushButton(T(QStringLiteral("Check for updates (you have {version})"), {{"version", QStringLiteral(F1_VERSION)}}));
        connect(b5, &QPushButton::clicked, this, &SettingsDialog::checkUpdatesRequested);
        row->addWidget(b5);
        row->addStretch(1);
        f->addRow(row);
    }
    row = new QHBoxLayout();
    auto* b3 = new QPushButton(T(QStringLiteral("Export settings…")));
    b3->setToolTip(T(QStringLiteral("Save all settings to a .json file (no passwords)")));
    connect(b3, &QPushButton::clicked, this, &SettingsDialog::exportSettings);
    auto* b4 = new QPushButton(T(QStringLiteral("Import settings…")));
    b4->setToolTip(T(QStringLiteral("Load settings from an exported .json file")));
    connect(b4, &QPushButton::clicked, this, &SettingsDialog::importSettings);
    row->addWidget(b3);
    row->addWidget(b4);
    row->addStretch(1);
    f->addRow(row);
    return w;
}

QWidget* SettingsDialog::appearance() {
    const Settings& s = *m_s;
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);
    auto* accentBtn = new QPushButton(s.accent);
    reg(QStringLiteral("accent_btn"), accentBtn);
    connect(accentBtn, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor::fromString(m_accent), this, T(QStringLiteral("Accent colour")));
        if (c.isValid()) setAccent(c.name());
    });
    auto* reset = new QPushButton(T(QStringLiteral("Original")));
    connect(reset, &QPushButton::clicked, this, [this] { setAccent(QStringLiteral("#BCC2FF")); });
    auto* row = new QHBoxLayout();
    row->addWidget(accentBtn);
    row->addWidget(reset);
    row->addStretch(1);
    f->addRow(T(QStringLiteral("Accent colour")), row);
    setAccent(s.accent);
    f->addRow(T(QStringLiteral("Typeface")),
              combo(QStringLiteral("font_fam"),
                    {{QStringLiteral("Noto Sans"), T(QStringLiteral("Noto Sans (original)"))}, {QStringLiteral("monospace"), T(QStringLiteral("Monospace"))}},
                    s.font_family));
    const std::tuple<const char*, const char*, int, int, int> spins[] = {
        {"font_scale", F1_TR_NOOP("Font size"), 70, 160, s.font_scale},
        {"ui_scale", F1_TR_NOOP("Interface scale"), 70, 200, s.ui_scale},
        {"pop_op", F1_TR_NOOP("Popup opacity"), 40, 100, s.popup_opacity},
        {"wid_op", F1_TR_NOOP("Widget background opacity"), 0, 100, s.widget_opacity}};
    for (const auto& [name, label, lo, hi, value] : spins) {
        auto* sp = new QSpinBox();
        sp->setRange(lo, hi);
        sp->setSuffix(QStringLiteral(" %"));
        sp->setValue(value);
        reg(QString::fromLatin1(name), sp);
        f->addRow(T(QString::fromLatin1(label)), sp);
    }
    f->addRow(check(QStringLiteral("anim"), T(QStringLiteral("Animations (circuit fade-in, start lights, popup slide)")), s.animations));
    f->addRow(new QLabel(T(QStringLiteral("Desktop widget"))));
    f->addRow(check(QStringLiteral("on_top"), T(QStringLiteral("Always on top")), s.widget_always_on_top));
    f->addRow(check(QStringLiteral("click_through"), T(QStringLiteral("Click-through (mouse passes through; turn off from the tray menu)")),
                    s.widget_click_through));
    f->addRow(check(QStringLiteral("locked"), T(QStringLiteral("Lock position and size (pin)")), s.widget_locked));
    f->addRow(check(QStringLiteral("remember"), T(QStringLiteral("Remember position and size")), s.widget_remember_position));
    f->addRow(T(QStringLiteral("Double-click on widget")),
              combo(QStringLiteral("dbl"),
                    {{QStringLiteral("refresh"), T(QStringLiteral("Refresh data"))}, {QStringLiteral("panel"), T(QStringLiteral("Open the tray panel"))},
                     {QStringLiteral("settings"), T(QStringLiteral("Open settings"))}, {QStringLiteral("none"), T(QStringLiteral("Nothing"))}},
                    s.widget_double_click));
    return w;
}

QWidget* SettingsDialog::taskbar() {
    const Settings& s = *m_s;
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);
    f->addRow(check(QStringLiteral("pill_on"), T(QStringLiteral("Show the taskbar countdown beside the tray icons")), s.taskbar_pill));
    auto* embed = check(QStringLiteral("pill_embed"), T(QStringLiteral("Embed in the taskbar (experimental)")), s.pill_embed);
    embed->setToolTip(T(QStringLiteral("Puts the countdown inside the Windows taskbar instead of floating over it. It then hides with the "
                                       "taskbar (fullscreen games, auto-hide) and never covers menus. If it causes trouble, turn it off.")));
    f->addRow(embed);
    auto* dock = check(QStringLiteral("pill_dock"), T(QStringLiteral("Place next to other taskbar widgets")), s.pill_auto_dock);
    dock->setToolTip(T(QStringLiteral("Sits beside other apps' widgets in the taskbar (e.g. a battery monitor) instead of on top of "
                                      "them. Position then adds extra space from there.")));
    f->addRow(dock);
    m_preview = new PillPreview();
    f->addRow(m_preview);
    f->addRow(hintLabel(T(QStringLiteral("On the taskbar: drag to move · drag the left edge to resize · double-click it to reset."))));

    f->addRow(T(QStringLiteral("Style")),
              combo(QStringLiteral("p_style"),
                    {{QStringLiteral("noctalia"), T(QStringLiteral("Noctalia (dark panel, accent text)"))},
                     {QStringLiteral("taskbar"), T(QStringLiteral("Match my Windows taskbar (transparent, system colours)"))},
                     {QStringLiteral("custom"), T(QStringLiteral("Custom colours"))}},
                    s.pill_style));
    auto* bgOp = new QSpinBox();
    bgOp->setRange(0, 100);
    bgOp->setSuffix(QStringLiteral(" %"));
    bgOp->setValue(s.pill_bg_opacity);
    reg(QStringLiteral("p_bg_op"), bgOp);
    const std::pair<QString, QWidget*> customRows[] = {
        {T(QStringLiteral("Background")), colorButton(QStringLiteral("p_bg"), s.pill_bg)},
        {T(QStringLiteral("Background opacity")), bgOp},
        {T(QStringLiteral("First line colour")), colorButton(QStringLiteral("p_text"), s.pill_text)},
        {T(QStringLiteral("Second line colour")), colorButton(QStringLiteral("p_text2"), s.pill_text2)},
        {T(QStringLiteral("Live highlight")), colorButton(QStringLiteral("p_live"), s.pill_live_color)},
        {T(QStringLiteral("Border colour")), colorButton(QStringLiteral("p_border_c"), s.pill_border_color)}};
    for (const auto& [label, wdg] : customRows) {
        f->addRow(label, wdg);
        m_customRows << wdg;
    }
    f->addRow(check(QStringLiteral("p_border"), T(QStringLiteral("Border")), s.pill_border));

    auto spin = [&](const char* name, int lo, int hi, const QString& suffix, int value) {
        auto* sp = new QSpinBox();
        sp->setRange(lo, hi);
        sp->setSuffix(suffix);
        sp->setValue(value);
        reg(QString::fromLatin1(name), sp);
        return sp;
    };
    auto* radius = spin("p_radius", 0, 30, T(QStringLiteral(" px")), s.pill_radius);
    radius->setSpecialValueText(T(QStringLiteral("0 (square box)")));
    f->addRow(T(QStringLiteral("Corner radius")), radius);
    auto* width = spin("p_width", 0, 900, T(QStringLiteral(" px")), s.pill_width);
    width->setSpecialValueText(T(QStringLiteral("Automatic")));
    f->addRow(T(QStringLiteral("Width")), width);
    const QString hmode = s.pill_height == -1 ? QStringLiteral("full") : (s.pill_height > 0 ? QStringLiteral("custom") : QStringLiteral("auto"));
    auto* hm = combo(QStringLiteral("p_hmode"),
                     {{QStringLiteral("auto"), T(QStringLiteral("Automatic"))}, {QStringLiteral("full"), T(QStringLiteral("Full taskbar height"))},
                      {QStringLiteral("custom"), T(QStringLiteral("Custom"))}},
                     hmode);
    auto* height = spin("p_height", 16, 80, T(QStringLiteral(" px")), s.pill_height > 0 ? s.pill_height : 28);
    auto* row = new QHBoxLayout();
    row->addWidget(hm);
    row->addWidget(height);
    f->addRow(T(QStringLiteral("Height")), row);
    auto* offset = spin("p_offset", -400, 1600, T(QStringLiteral(" px left of the tray")), s.taskbar_pill_offset);
    offset->setToolTip(T(QStringLiteral("Extra distance from the tray (or from the widget it docks beside). Negative values move "
                                        "it closer, even over the tray icons (only while \"Place next to other taskbar widgets\" is off).")));
    f->addRow(T(QStringLiteral("Position")), offset);
    auto* font = new QDoubleSpinBox();
    font->setRange(7, 28);
    font->setSingleStep(0.5);
    font->setSuffix(T(QStringLiteral(" px")));
    font->setValue(s.pill_font_px);
    reg(QStringLiteral("p_font"), font);
    f->addRow(T(QStringLiteral("Text size (max)")), font);
    f->addRow(check(QStringLiteral("p_autofit"), T(QStringLiteral("Auto-fit text to the pill size")), s.pill_autofit));
    f->addRow(check(QStringLiteral("p_bold"), T(QStringLiteral("First line bold")), s.pill_bold_top));
    f->addRow(T(QStringLiteral("Alignment")),
              combo(QStringLiteral("p_align"), {{QStringLiteral("left"), T(QStringLiteral("Left"))}, {QStringLiteral("center"), T(QStringLiteral("Centre"))}},
                    s.pill_align));
    f->addRow(T(QStringLiteral("Lines")),
              combo(QStringLiteral("p_lines"), {{QStringLiteral("2"), T(QStringLiteral("Two lines"))}, {QStringLiteral("1"), T(QStringLiteral("One line"))}},
                    QString::number(s.pill_lines)));
    auto* preset = new QComboBox();
    preset->addItem(T(QStringLiteral("(custom)")), QVariant());
    const QList<pill::Preset> pre = pill::presets();
    for (int i = 0; i < pre.size(); ++i) preset->addItem(pre[i].label, i);
    reg(QStringLiteral("p_preset"), preset);
    f->addRow(T(QStringLiteral("Layout preset")), preset);
    const std::tuple<const char*, const char*, QString> lines[] = {{"p_top", F1_TR_NOOP("First line"), s.pill_top},
                                                                   {"p_bottom", F1_TR_NOOP("Second line"), s.pill_bottom},
                                                                   {"p_ltop", F1_TR_NOOP("First line (live)"), s.pill_live_top},
                                                                   {"p_lbottom", F1_TR_NOOP("Second line (live)"), s.pill_live_bottom}};
    for (const auto& [name, label, value] : lines) {
        auto* e = new QLineEdit(value);
        reg(QString::fromLatin1(name), e);
        f->addRow(T(QString::fromLatin1(label)), e);
        connect(e, &QLineEdit::textChanged, this, &SettingsDialog::updatePreview);
    }
    QStringList toks, help;
    for (const QString& k : pill::tokenNames()) {
        toks << QStringLiteral("{") + k + u'}';
        help << QStringLiteral("{") + k + QStringLiteral("}  ") + pill::tokenExample(k);
    }
    auto* tokens = hintLabel(T(QStringLiteral("Tokens: ")) + toks.join(u' ') + T(QStringLiteral("  (hover for examples)")));
    tokens->setToolTip(help.join(u'\n'));
    f->addRow(tokens);

    connect(preset, &QComboBox::currentIndexChanged, this, [this, preset, pre](int i) {
        const QVariant idx = preset->itemData(i);
        if (!idx.isValid()) return;
        const pill::Preset& p = pre[idx.toInt()];
        qobject_cast<QLineEdit*>(control(QStringLiteral("p_top")))->setText(p.top);
        qobject_cast<QLineEdit*>(control(QStringLiteral("p_bottom")))->setText(p.bottom);
        qobject_cast<QLineEdit*>(control(QStringLiteral("p_ltop")))->setText(p.liveTop);
        qobject_cast<QLineEdit*>(control(QStringLiteral("p_lbottom")))->setText(p.liveBottom);
        auto* l = qobject_cast<QComboBox*>(control(QStringLiteral("p_lines")));
        l->setCurrentIndex(l->findData(p.bottom.isEmpty() ? QStringLiteral("1") : QStringLiteral("2")));
    });
    for (const char* n : {"p_style", "p_hmode", "p_align", "p_lines"})
        connect(qobject_cast<QComboBox*>(control(QString::fromLatin1(n))), &QComboBox::currentIndexChanged, this, &SettingsDialog::updatePreview);
    for (const char* n : {"p_bg_op", "p_radius", "p_width", "p_height"})
        connect(qobject_cast<QSpinBox*>(control(QString::fromLatin1(n))), &QSpinBox::valueChanged, this, &SettingsDialog::updatePreview);
    connect(font, &QDoubleSpinBox::valueChanged, this, &SettingsDialog::updatePreview);
    for (const char* n : {"p_autofit", "p_bold", "p_border"})
        connect(qobject_cast<QCheckBox*>(control(QString::fromLatin1(n))), &QCheckBox::toggled, this, &SettingsDialog::updatePreview);
    return w;
}

void SettingsDialog::pillValues(Settings& s) const {
    auto cb = [this](const char* n) { return qobject_cast<QCheckBox*>(control(QString::fromLatin1(n)))->isChecked(); };
    auto co = [this](const char* n) { return qobject_cast<QComboBox*>(control(QString::fromLatin1(n)))->currentData().toString(); };
    auto sp = [this](const char* n) { return qobject_cast<QSpinBox*>(control(QString::fromLatin1(n)))->value(); };
    auto le = [this](const char* n) { return qobject_cast<QLineEdit*>(control(QString::fromLatin1(n)))->text(); };
    const QString hmode = co("p_hmode");
    s.taskbar_pill = cb("pill_on");
    s.pill_embed = cb("pill_embed");
    s.pill_auto_dock = cb("pill_dock");
    s.pill_style = co("p_style");
    s.pill_bg = colorOf(QStringLiteral("p_bg"));
    s.pill_bg_opacity = sp("p_bg_op");
    s.pill_text = colorOf(QStringLiteral("p_text"));
    s.pill_text2 = colorOf(QStringLiteral("p_text2"));
    s.pill_live_color = colorOf(QStringLiteral("p_live"));
    s.pill_border = cb("p_border");
    s.pill_border_color = colorOf(QStringLiteral("p_border_c"));
    s.pill_radius = sp("p_radius");
    s.pill_width = sp("p_width");
    s.pill_height = hmode == u"full" ? -1 : (hmode == u"custom" ? sp("p_height") : 0);
    s.taskbar_pill_offset = sp("p_offset");
    s.pill_font_px = qobject_cast<QDoubleSpinBox*>(control(QStringLiteral("p_font")))->value();
    s.pill_autofit = cb("p_autofit");
    s.pill_bold_top = cb("p_bold");
    s.pill_align = co("p_align");
    s.pill_lines = co("p_lines").toInt();
    s.pill_top = le("p_top");
    s.pill_bottom = le("p_bottom");
    s.pill_live_top = le("p_ltop");
    s.pill_live_bottom = le("p_lbottom");
}

void SettingsDialog::updatePreview() {
    if (!m_built || !m_preview) return;
    const bool custom = qobject_cast<QComboBox*>(control(QStringLiteral("p_style")))->currentData().toString() == u"custom";
    for (QWidget* w : m_customRows) w->setEnabled(custom);
    control(QStringLiteral("p_height"))->setEnabled(qobject_cast<QComboBox*>(control(QStringLiteral("p_hmode")))->currentData().toString() == u"custom");
    Settings tmp = *m_s;
    pillValues(tmp);
    const pill::State lines = pill::state(m_store->race(), m_store->get(QStringLiteral("live")), tmp, nowSec(), zoneFor(tmp.timezone),
                                          m_store->forecast());
    const PillStyle style = PillStyle::resolve(tmp, m_t);
    const int h = tmp.pill_height > 0 ? tmp.pill_height : (tmp.pill_height == -1 ? 30 : 24);
    const int w = tmp.pill_width ? tmp.pill_width : 190;
    m_preview->setPreview(style, lines, QSize(w, h), tmp.pill_font_px, tmp.pill_autofit, QColor(0x1C, 0x1C, 0x1C));
}

QWidget* SettingsDialog::formula1() {
    const Settings& s = *m_s;
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);
    f->addRow(T(QStringLiteral("Countdown target")),
              combo(QStringLiteral("cd_target"),
                    {{QStringLiteral("race"), T(QStringLiteral("Next race"))}, {QStringLiteral("session"), T(QStringLiteral("Next session"))}},
                    s.countdown_target));
    f->addRow(hintLabel(T(QStringLiteral("Race = always to the Grand Prix. Session = to FP1, FP2, Quali … in turn. Hover for more."))));
    auto* favDriver = new QComboBox();
    favDriver->setMaxVisibleItems(16);
    favDriver->addItem(T(QStringLiteral("(none)")), QString());
    const QJsonArray drivers = m_store->get(QStringLiteral("drivers")).value(QStringLiteral("rows")).toArray();
    for (const QJsonValue& rv : drivers) {
        const QJsonObject r = rv.toObject();
        const QString code = r.value(QStringLiteral("code")).toString(), team = r.value(QStringLiteral("team")).toString();
        favDriver->addItem(code + QStringLiteral("  ·  ") + r.value(QStringLiteral("name")).toString() +
                               (team.isEmpty() ? QString() : QStringLiteral("  (") + team + u')'),
                           code);
    }
    int i = favDriver->findData(s.favorite_driver);
    if (i < 0 && !s.favorite_driver.isEmpty()) {
        favDriver->addItem(s.favorite_driver, s.favorite_driver);
        i = favDriver->count() - 1;
    }
    favDriver->setCurrentIndex(std::max(0, i));
    reg(QStringLiteral("fav_driver"), favDriver);
    f->addRow(T(QStringLiteral("Favourite driver")), favDriver);
    if (drivers.isEmpty()) f->addRow(hintLabel(T(QStringLiteral("The driver list fills in once the standings have loaded."))));
    auto* favTeam = new QComboBox();
    favTeam->setMaxVisibleItems(16);
    favTeam->addItem(T(QStringLiteral("(none)")), QString());
    for (const QJsonValue& rv : m_store->get(QStringLiteral("constructors")).value(QStringLiteral("rows")).toArray())
        favTeam->addItem(rv[QStringLiteral("name")].toString(), rv[QStringLiteral("team_id")].toString());
    i = favTeam->findData(s.favorite_team);
    if (i < 0 && !s.favorite_team.isEmpty()) {
        favTeam->addItem(s.favorite_team, s.favorite_team);
        i = favTeam->count() - 1;
    }
    favTeam->setCurrentIndex(std::max(0, i));
    reg(QStringLiteral("fav_team"), favTeam);
    f->addRow(T(QStringLiteral("Favourite team")), favTeam);
    auto* teamColors = check(QStringLiteral("team_colors"), T(QStringLiteral("Team colour chips")), s.team_colors);
    // The Python dialog creates this box but never adds it to the page (so it cannot be changed
    // there). Kept the same for parity; the setting still saves its current value.
    teamColors->setParent(w);
    teamColors->hide();
    auto preset = [](const QString& key, const QString& name) {
        const auto [near, weekend, other] = weather::timings(key);
        return std::pair{key, T(QStringLiteral("{name} ({near} · {weekend} · {other})"),
                                {{"name", name}, {"near", weather::fmtMinutes(near)}, {"weekend", weather::fmtMinutes(weekend)},
                                 {"other", weather::fmtMinutes(other)}})};
    };
    auto* wx = combo(QStringLiteral("wx_refresh"),
                     {preset(QStringLiteral("fast"), T(QStringLiteral("Fast"))), preset(QStringLiteral("normal"), T(QStringLiteral("Normal"))),
                      preset(QStringLiteral("slow"), T(QStringLiteral("Slow"))), {QStringLiteral("custom"), T(QStringLiteral("Custom…"))},
                      {QStringLiteral("off"), T(QStringLiteral("Off (no forecast)"))}},
                     s.weather_refresh);
    wx->setToolTip(T(QStringLiteral("How often the weather forecast is downloaded from Open-Meteo: around a session (3 h before until it "
                                    "ends) · during the race weekend · the rest of the time.\nLive track weather during sessions is "
                                    "not affected.")));
    f->addRow(T(QStringLiteral("Weather updates")), wx);
    f->addRow(hintLabel(T(QStringLiteral("The three times: close to a session · rest of the race weekend · other days."))));
    auto* row = new QHBoxLayout();
    const QString labels[] = {T(QStringLiteral("around sessions")), T(QStringLiteral("weekend")), T(QStringLiteral("otherwise"))};
    for (int k = 0; k < 3; ++k) {
        auto* sp = new QSpinBox();
        sp->setRange(5, 1440);
        sp->setSingleStep(5);
        sp->setSuffix(QStringLiteral(" ") + T(QStringLiteral("min")));
        sp->setValue(s.weather_custom.at(k).toInt());
        sp->setToolTip(T(QStringLiteral("Minutes between forecast updates: {when}."), {{"when", labels[k]}}));
        m_wxCustom << sp;
        row->addWidget(new QLabel(labels[k]));
        row->addWidget(sp);
    }
    row->addStretch(1);
    m_wxCustomRow = new QWidget();
    m_wxCustomRow->setLayout(row);
    f->addRow(QString(), m_wxCustomRow);
    connect(wx, &QComboBox::currentIndexChanged, this, [this, wx] { m_wxCustomRow->setVisible(wx->currentData().toString() == u"custom"); });
    m_wxCustomRow->setVisible(s.weather_refresh == u"custom");
    f->addRow(check(QStringLiteral("spoiler"), T(QStringLiteral("Spoiler-free mode (hide results until I've watched)")), s.spoiler_free));

    f->addRow(new QLabel(T(QStringLiteral("Notifications"))));
    f->addRow(check(QStringLiteral("n_race"), T(QStringLiteral("Before the race")), s.notify_race));
    f->addRow(check(QStringLiteral("n_quali"), T(QStringLiteral("Before qualifying")), s.notify_quali));
    f->addRow(check(QStringLiteral("n_sprint"), T(QStringLiteral("Before the sprint")), s.notify_sprint));
    f->addRow(check(QStringLiteral("n_results"), T(QStringLiteral("Results (top 3) after qualifying, sprint and race")), s.notify_results));
    auto* lead = new QSpinBox();
    lead->setRange(0, 120);
    lead->setSuffix(T(QStringLiteral(" min")));
    lead->setValue(s.notify_lead_min);
    reg(QStringLiteral("lead"), lead);
    f->addRow(T(QStringLiteral("Lead time")), lead);

    f->addRow(new QLabel(T(QStringLiteral("Session-time corrections"))));
    f->addRow(hintLabel(T(QStringLiteral("Only needed if a delay isn't picked up automatically. Resets next round."))));
    const Race* race = m_store->race();
    const QJsonObject offs = (race && s.session_offsets.value(QStringLiteral("round_key")).toString() == roundKey(race))
                                 ? s.session_offsets
                                 : QJsonObject();
    if (!race) {
        f->addRow(hintLabel(T(QStringLiteral("No upcoming race loaded."))));
    } else {
        for (const Session& sess : race->rawSessions) {
            auto* sp = new QSpinBox();
            sp->setRange(-720, 720);
            sp->setSuffix(T(QStringLiteral(" min")));
            sp->setValue(offs.value(sess.id).toInt());
            m_offsets.insert(sess.id, sp);
            f->addRow(sessionName(sess.id), sp);
        }
    }
    return w;
}

QWidget* SettingsDialog::live() {
    const Settings& s = *m_s;
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);
    const QString mode = (s.openf1_plan == u"sponsor" && s.live_source == u"openf1") ? QStringLiteral("sponsor")
                         : s.live_source == u"f1_feed"                               ? QStringLiteral("combo")
                                                                                     : QStringLiteral("free");
    liveMode = combo(QStringLiteral("live_mode"),
                     {{QStringLiteral("free"), T(QStringLiteral("OpenF1 Free · results after each session"))},
                      {QStringLiteral("combo"), T(QStringLiteral("OpenF1 Free + F1 live stream (experimental)"))},
                      {QStringLiteral("sponsor"), T(QStringLiteral("OpenF1 Sponsor · live during sessions (account)"))}},
                     mode);
    f->addRow(T(QStringLiteral("Live data")), liveMode);
    auto* liveHint = hintLabel(QString());
    f->addRow(liveHint);
    user = new QLineEdit(s.openf1_username);
    user->setPlaceholderText(T(QStringLiteral("you@example.com")));
    reg(QStringLiteral("user"), user);
    f->addRow(T(QStringLiteral("OpenF1 username")), user);
    pw = new QLineEdit();
    pw->setEchoMode(QLineEdit::Password);
    const bool has = !s.openf1_username.isEmpty() && m_creds.get(s.openf1_username).has_value();
    pw->setPlaceholderText(has ? T(QStringLiteral("•••••••• (stored)")) : T(QStringLiteral("password")));
    reg(QStringLiteral("pw"), pw);
    f->addRow(T(QStringLiteral("OpenF1 password")), pw);
    auto* forget = new QPushButton(T(QStringLiteral("Forget stored password")));
    forget->setToolTip(T(QStringLiteral("Remove the password from Windows Credential Manager.")));
    connect(forget, &QPushButton::clicked, this, [this] {
        const QString u = user->text().trimmed();
        m_creds.remove(u.isEmpty() ? m_s->openf1_username : u);
        pw->clear();
        pw->setPlaceholderText(T(QStringLiteral("password")));
    });
    f->addRow(forget);
    auto modeChanged = [this, liveHint, forget] {
        const QString m = liveMode->currentData().toString();
        for (QWidget* x : {static_cast<QWidget*>(user), static_cast<QWidget*>(pw), static_cast<QWidget*>(forget)}) x->setEnabled(m == u"sponsor");
        if (m == u"free") liveHint->setText(T(QStringLiteral("No account needed. Live tab shows final results ~20–30 min after each session.")));
        else if (m == u"combo")
            liveHint->setText(T(QStringLiteral("Live during sessions via F1's unofficial stream, OpenF1 results after. May break anytime.")));
        else liveHint->setText(T(QStringLiteral("Official live timing during sessions. Needs a paid OpenF1 account below.")));
    };
    connect(liveMode, &QComboBox::currentIndexChanged, this, modeChanged);
    modeChanged();
    f->addRow(check(QStringLiteral("debug"), T(QStringLiteral("Debug: force Grid and Live tabs")), s.debug_force_tabs));
    return w;
}

QWidget* SettingsDialog::about() {
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    auto* lab = new QLabel(T(QStringLiteral(
                               "<b>{app_title}</b> {version}<br>F1 schedule, countdowns, live timing and standings for Windows.<br><br>"
                               "Inspired by the <a href='https://noctalia.dev/plugins/community/f1'>Noctalia F1 plugin</a> (gcap0n1, MIT)."
                               "<br>Data: Jolpica-F1 · OpenF1. Weather data by <a href='https://open-meteo.com/'>Open-Meteo.com</a> "
                               "(CC BY 4.0). Circuits: bacinger/f1-circuits.<br><br><span style='color:#8F8F9E'>Unofficial · not "
                               "affiliated with Formula 1. Full credits in THIRD_PARTY_NOTICES.md.</span>"),
                             {{"app_title", QStringLiteral(F1_APP_TITLE)}, {"version", QStringLiteral(F1_VERSION)}}));
    lab->setWordWrap(true);
    lab->setTextFormat(Qt::RichText);
    lab->setOpenExternalLinks(true);
    v->addWidget(lab);
    v->addStretch(1);
    return w;
}

// â”€â”€ actions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void SettingsDialog::setAccent(const QString& c) {
    m_accent = QColor::fromString(c).name().toUpper();
    auto* b = qobject_cast<QPushButton*>(control(QStringLiteral("accent_btn")));
    b->setText(m_accent);
    const QString fg = QColor::fromString(c).lightnessF() > 0.55 ? QStringLiteral("#262C67") : QStringLiteral("#FFFFFF");
    b->setStyleSheet(QStringLiteral("background:%1; color:%2; border:none;").arg(m_accent, fg));
}

void SettingsDialog::exportSettings() {
    const QString start = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                              .filePath(QStringLiteral("F1DockTimers-settings.json"));
    const QString path = QFileDialog::getSaveFileName(this, T(QStringLiteral("Export settings")), start, QStringLiteral("Settings (*.json)"));
    if (path.isEmpty()) return;
    QString err;
    if (!m_s->exportTo(path, &err)) {
        QMessageBox::warning(this, T(QStringLiteral("Export settings")), T(QStringLiteral("Could not write the file:\n{e}"), {{"e", err}}));
        return;
    }
    QMessageBox::information(this, T(QStringLiteral("Export settings")),
                             T(QStringLiteral("Settings exported. Passwords are not included; window positions stay on this PC.")));
}

void SettingsDialog::importSettings() {
    const QString path = QFileDialog::getOpenFileName(this, T(QStringLiteral("Import settings")),
                                                      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                      QStringLiteral("Settings (*.json)"));
    if (path.isEmpty()) return;
    QString err;
    const auto changed = m_s->importFrom(path, &err);
    if (!changed) {
        QMessageBox::warning(this, T(QStringLiteral("Import settings")), err);
        return;
    }
    m_save();
    Q_EMIT applied();
    QMessageBox::information(this, T(QStringLiteral("Import settings")),
                             changed->size() == 1
                                 ? T(QStringLiteral("Imported and applied (1 setting changed). Settings will reopen with the new values."))
                                 : T(QStringLiteral("Imported and applied ({count} settings changed). Settings will reopen with the new values."),
                                     {{"count", QString::number(changed->size())}}));
    accept();
    Q_EMIT reopenRequested();
}

void SettingsDialog::saveAndClose() {
    Settings& s = *m_s;
    auto cb = [this](const char* n) { return qobject_cast<QCheckBox*>(control(QString::fromLatin1(n)))->isChecked(); };
    auto co = [this](const char* n) { return qobject_cast<QComboBox*>(control(QString::fromLatin1(n)))->currentData().toString(); };
    auto sp = [this](const char* n) { return qobject_cast<QSpinBox*>(control(QString::fromLatin1(n)))->value(); };
    auto ds = [this](const char* n) { return qobject_cast<QDoubleSpinBox*>(control(QString::fromLatin1(n)))->value(); };
    s.start_with_windows = cb("start_win");
    s.start_minimized = cb("start_min");
    s.check_updates = cb("auto_update");
    s.widget_enabled = cb("widget_on");
    pillValues(s);
    s.default_tab = co("default_tab");
    s.time_format = co("time_fmt");
    s.timezone = co("tz").isEmpty() ? QStringLiteral("auto") : co("tz");
    s.units = co("units").isEmpty() ? QStringLiteral("metric") : co("units");
    s.language = co("lang").isEmpty() ? QStringLiteral("auto") : co("lang");
    s.schedule_refresh_h = ds("sched_h");
    s.standings_refresh_h = ds("stand_h");
    s.accent = m_accent;
    s.font_family = co("font_fam");
    s.font_scale = sp("font_scale");
    s.ui_scale = sp("ui_scale");
    s.popup_opacity = sp("pop_op");
    s.widget_opacity = sp("wid_op");
    s.animations = cb("anim");
    s.widget_always_on_top = cb("on_top");
    s.widget_click_through = cb("click_through");
    s.widget_locked = cb("locked");
    s.widget_remember_position = cb("remember");
    s.widget_double_click = co("dbl");
    s.countdown_target = co("cd_target");
    s.favorite_driver = co("fav_driver");
    s.favorite_team = co("fav_team");
    s.team_colors = cb("team_colors");
    s.weather_refresh = co("wx_refresh").isEmpty() ? QStringLiteral("normal") : co("wx_refresh");
    s.weather_custom = QJsonArray{m_wxCustom[0]->value(), m_wxCustom[1]->value(), m_wxCustom[2]->value()};
    s.notify_race = cb("n_race");
    s.notify_quali = cb("n_quali");
    s.notify_sprint = cb("n_sprint");
    s.notify_lead_min = sp("lead");
    s.notify_results = cb("n_results");
    if (cb("spoiler") && !s.spoiler_free)   // turning it on hides only what comes next
        s.spoiler_revealed_at = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    s.spoiler_free = cb("spoiler");
    if (const Race* race = m_store->race(); race && !m_offsets.isEmpty()) {
        QJsonObject offs;
        for (auto it = m_offsets.begin(); it != m_offsets.end(); ++it)
            if (it.value()->value()) offs.insert(it.key(), it.value()->value());
        if (!offs.isEmpty()) offs.insert(QStringLiteral("round_key"), roundKey(race));
        s.session_offsets = offs;
    }
    const QString m = liveMode->currentData().toString();
    s.live_source = m == u"combo" ? QStringLiteral("f1_feed") : QStringLiteral("openf1");
    s.openf1_plan = m == u"sponsor" ? QStringLiteral("sponsor") : QStringLiteral("free");
    const QString u = user->text().trimmed();
    if (u != s.openf1_username && !s.openf1_username.isEmpty() && pw->text().isEmpty()) m_creds.remove(s.openf1_username);
    s.openf1_username = u;
    if (!u.isEmpty() && !pw->text().isEmpty() && !m_creds.set(u, pw->text()))
        QMessageBox::warning(this, T(QStringLiteral("OpenF1")), T(QStringLiteral("Could not store the password in Windows Credential Manager.")));
    pw->clear();
    s.debug_force_tabs = cb("debug");
    s.clamp();
    m_save();
    Q_EMIT applied();
    accept();
}

}  // namespace f1::ui
