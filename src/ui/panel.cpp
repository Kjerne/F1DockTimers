#include "panel.h"

#include "../core/i18n.h"
#include "../core/spoilers.h"
#include "settings_dialog.h"
#include "tabs/live.h"
#include "tabs/schedule.h"
#include "tabs/tables.h"
#include "tabs/weather.h"

#include <QDateTime>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QVBoxLayout>

namespace f1::ui {

QList<TabEntry> tabList(const DataStore* store, const Settings& s, qint64 now) {
    const Race* race = store->race();
    const Sessions sessions = race ? race->sessions : Sessions();
    const bool force = s.debug_force_tabs;
    QList<TabEntry> tabs{{QStringLiteral("schedule"), T(QStringLiteral("Schedule"))},
                         {QStringLiteral("weather"), T(QStringLiteral("Weather"))}};
    if ((race && gridApplicable(sessions, now)) || force) tabs.append({QStringLiteral("grid"), T(QStringLiteral("Grid"))});
    const bool isLive = liveActive(store->get(QStringLiteral("live")), sessions, now, force);
    tabs.append({QStringLiteral("live"), isLive ? T(QStringLiteral("● Live")) : T(QStringLiteral("Live"))});
    tabs.append({QStringLiteral("results"), T(QStringLiteral("Last"))});
    tabs.append({QStringLiteral("drivers"), T(QStringLiteral("Drivers"))});
    tabs.append({QStringLiteral("constructors"), T(QStringLiteral("Teams"))});
    return tabs;
}

QString pickView(const DataStore* store, const Settings& s, qint64 now) {
    const Race* race = store->race();
    const Sessions sessions = race ? race->sessions : Sessions();
    if (liveActive(store->get(QStringLiteral("live")), sessions, now, s.debug_force_tabs)) return QStringLiteral("live");
    if (race && gridApplicable(sessions, now)) return QStringLiteral("grid");
    return (s.default_tab == u"drivers" || s.default_tab == u"constructors") ? s.default_tab : QStringLiteral("schedule");
}

namespace {
// Which store keys each view shows (a change to any of them rebuilds the body).
bool relevant(const QString& view, const QString& key) {
    if (view == u"schedule") return key == u"race" || key == u"status" || key == u"weather" || key == u"measured_weather";
    if (view == u"weather") return key == u"weather" || key == u"race" || key == u"live" || key == u"measured_weather";
    if (view == u"grid") return key == u"grid" || key == u"grid_official" || key == u"race";
    if (view == u"live") return key == u"live" || key == u"race";
    if (view == u"results") return key == u"results";
    return key == view;
}
}  // namespace

F1Panel::F1Panel(Ctx* ctx, const QString& mode, QWidget* parent) : QWidget(parent), m_ctx(ctx), m_mode(mode) {
    setFocusPolicy(Qt::StrongFocus);
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(60);
    connect(&m_rebuildTimer, &QTimer::timeout, this, [this] { rebuildBody(); });
    connect(ctx->store, &DataStore::changed, this, &F1Panel::dataChanged);
    buildChrome();
}

void F1Panel::buildChrome() {
    const Theme& t = *m_ctx->theme;
    auto* root = new QVBoxLayout(this);
    const int pad = t.px(14);
    root->setContentsMargins(pad, t.px(12), pad, t.px(8));
    root->setSpacing(t.px(10));

    auto* header = new QHBoxLayout();
    header->setSpacing(t.px(8));
    header->addWidget(new FlagGlyph(t, t.accent, 16));
    header->addWidget(new Text(T(QStringLiteral("FORMULA 1")), t, 11, true, t.muted, Qt::AlignLeft, 0.6));
    header->addStretch(1);
    m_roundLabel = new Text(QString(), t, 11, false, t.muted, Qt::AlignRight);
    header->addWidget(m_roundLabel);
    if (m_mode == u"popup") {
        m_pinBtn = new IconButton(PIN_SVG, t, QStringLiteral("Pin (keep open when clicking elsewhere)"), 14);
        m_pinBtn->setActive(pinned);
        connect(m_pinBtn, &IconButton::clicked, this, [this] {
            pinned = !pinned;
            m_pinBtn->setActive(pinned);
            Q_EMIT pinToggled(pinned);
        });
        header->addWidget(m_pinBtn);
        m_gearBtn = new IconButton(GEAR_SVG, t, T(QStringLiteral("Settings (Ctrl+,)")), 14);
        connect(m_gearBtn, &IconButton::clicked, this, &F1Panel::toggleSettings);
        header->addWidget(m_gearBtn);
    }
    root->addLayout(header);
    root->addWidget(new Divider(t));

    m_title = new Text(QString(), t, 20, true, t.onSurface);
    m_title->setAccessibleName(T(QStringLiteral("Grand Prix")));
    root->addWidget(m_title);

    m_tabbarHost = new QWidget();
    m_tabbar = new QHBoxLayout(m_tabbarHost);
    m_tabbar->setContentsMargins(0, 0, 0, 0);
    m_tabbar->setSpacing(t.px(2));
    root->addWidget(m_tabbarHost);

    scroll = new QScrollArea();
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->setStyleSheet(
        QStringLiteral("QScrollArea{background:transparent;} QScrollArea>QWidget>QWidget{background:transparent;}"
                       "QScrollBar:vertical{background:transparent;width:%1px;margin:0;}"
                       "QScrollBar::handle:vertical{background:rgba(198,197,208,0.45);border-radius:%2px;min-height:%3px;}"
                       "QScrollBar::handle:vertical:hover{background:rgba(198,197,208,0.7);}"
                       "QScrollBar::add-line,QScrollBar::sub-line{height:0;}"
                       "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}")
            .arg(t.px(6))
            .arg(t.px(3))
            .arg(t.px(24)));
    scroll->viewport()->setAutoFillBackground(false);
    root->addWidget(scroll, 1);

    // Settings tab: Revert / Save, always in view below the scrolling page, greyed out until an edit.
    m_settingsBar = new QWidget();
    auto* bar = new QHBoxLayout(m_settingsBar);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(t.px(6));
    m_settingsState = new Text(QString(), t, 11, false, t.muted);
    bar->addWidget(m_settingsState, 1);
    m_revertBtn = new PillButton(T(QStringLiteral("Revert")), t);
    m_revertBtn->setToolTip(T(QStringLiteral("Undo the changes since the last save")));
    connect(m_revertBtn, &PillButton::clicked, this, &F1Panel::revertSettings);
    m_saveBtn = new PillButton(T(QStringLiteral("Save")), t, true);
    m_saveBtn->setToolTip(T(QStringLiteral("Save and apply (Ctrl+S)")));
    connect(m_saveBtn, &PillButton::clicked, this, &F1Panel::saveSettings);
    bar->addWidget(m_revertBtn);
    bar->addWidget(m_saveBtn);
    m_settingsBar->setFixedHeight(t.px(30));
    m_settingsBar->hide();
    root->addWidget(m_settingsBar);

    auto* foot = new QHBoxLayout();
    foot->setSpacing(t.px(6));
    m_footer = new Text(QString(), t, 10, false, t.muted);
    m_footer->setColor(QColor::fromString(t.muted).darker(125).name());
    foot->addWidget(m_footer, 1);
    auto* refresh = new IconButton(REFRESH_SVG, t, QStringLiteral("Refresh (R)"), 12);
    connect(refresh, &IconButton::clicked, this, &F1Panel::refreshRequested);
    m_updateLink = new ClickText(QString(), t, 10, true, t.accent, Qt::AlignRight);
    m_updateLink->enableClicks();
    m_updateLink->setToolTip(T(QStringLiteral("A newer version is available. Click to update.")));
    connect(m_updateLink, &ClickText::clicked, this, &F1Panel::updateClicked);
    foot->addWidget(m_updateLink);
    setUpdate(m_updateVersion);
    foot->addWidget(refresh);
    root->addLayout(foot);
    m_tabsSig.clear();
    rebuildBody();
}

void F1Panel::setUpdate(const QString& version) {
    m_updateVersion = version;
    m_updateLink->setText(version.isEmpty() ? QString() : T(QStringLiteral("Update {version} available"), {{"version", version}}));
    m_updateLink->setVisible(!version.isEmpty());
}

void F1Panel::setPinned(bool on) {
    pinned = on;
    if (m_pinBtn) m_pinBtn->setActive(on);
}

void F1Panel::dataChanged(const QString& key) {
    if (view == SETTINGS_VIEW) {}   // never rebuild under the user's edits
    else if (relevant(view, key) || key == u"race") m_rebuildTimer.start();
    else if (key == u"live") refreshTabbar(nowSec());
    updateHeader();
}

void F1Panel::updateHeader() {
    const DataStore* store = m_ctx->store;
    const Race* race = store->race();
    m_roundLabel->setText(race ? T(QStringLiteral("Round {round} · {season}"), {{"round", race->round}, {"season", race->season}})
                               : QString());
    if (view == SETTINGS_VIEW) m_title->setText(T(QStringLiteral("Settings")));
    else if (race) m_title->setText(race->name);
    else if (store->status.value(QStringLiteral("loading")).toBool()) m_title->setText(T(QStringLiteral("Loading…")));
    else m_title->setText(T(QStringLiteral("No upcoming race")));
    QStringList bits{QStringLiteral("Updated ") + fmtStamp(store->lastUpdated(), m_ctx->zone(), m_ctx->twelve())};
    const QString error = store->status.value(QStringLiteral("error")).toString();
    if (store->status.value(QStringLiteral("offline")).toBool()) bits << QStringLiteral("offline · showing cached data");
    else if (!error.isEmpty() && error != u"no_race") bits << QStringLiteral("couldn't load (%1), retrying").arg(error);
    m_footer->setText(bits.join(QStringLiteral("  ·  ")));
}

void F1Panel::refreshTabbar(qint64 now) {
    const bool inSettings = view == SETTINGS_VIEW;
    QList<TabEntry> tabs;
    if (inSettings) {   // the Settings tab's own sub-tabs replace the F1 tabs
        const QList<SettingsPage> pages = settingsPages();
        for (int i = 0; i < pages.size(); ++i) tabs.append({QString::number(i), pages[i].label});
    } else {
        tabs = tabList(m_ctx->store, *m_ctx->settings, now);
    }
    const QString current = inSettings ? QString::number(settingsPage) : view;
    QList<std::pair<TabEntry, bool>> sig;
    if (inSettings) sig.append({{SETTINGS_VIEW, QString()}, false});
    for (const auto& e : tabs) sig.append({e, e.id == current});
    if (sig == m_tabsSig) return;
    m_tabsSig = sig;
    while (m_tabbar->count()) {
        QLayoutItem* item = m_tabbar->takeAt(0);
        if (QWidget* w = item->widget()) {
            w->hide();   // never painted again, even before deletion
            w->deleteLater();
        }
        delete item;
    }
    for (const auto& e : tabs) {
        auto* b = new PillButton(e.label, *m_ctx->theme, e.id == current);
        const QString id = e.id;
        if (inSettings) connect(b, &PillButton::clicked, this, [this, id] { setSettingsPage(id.toInt()); });
        else connect(b, &PillButton::clicked, this, [this, id] { setView(id); });
        m_tabbar->addWidget(b);
    }
    m_tabbar->addStretch(1);
}

QStringList F1Panel::validViews(qint64 now) const {
    QStringList out;
    for (const auto& e : tabList(m_ctx->store, *m_ctx->settings, now)) out << e.id;
    return out;
}

void F1Panel::setView(const QString& v, bool animate) {
    if (v == SETTINGS_VIEW && m_mode != u"popup") {   // the desktop widget has no Settings tab
        Q_EMIT settingsRequested();
        return;
    }
    const QString target = (v == SETTINGS_VIEW || validViews(nowSec()).contains(v)) ? v : QStringLiteral("schedule");
    const bool changed = target != view;
    if (changed && target == SETTINGS_VIEW) m_prevView = view;
    view = target;
    if (m_gearBtn) m_gearBtn->setActive(view == SETTINGS_VIEW);
    rebuildBody(changed);
    if (animate && tabWidget) tabWidget->onShown();
}

void F1Panel::openFresh() {
    // Settings with unsaved edits stay open when the popup opens again.
    if (view == SETTINGS_VIEW && m_settingsForm && m_settingsForm->dirty()) return;
    setView(pickView(m_ctx->store, *m_ctx->settings, nowSec()));
}

// ── settings tab ─────────────────────────────────────────────────────────────

void F1Panel::showSettings(int page) {
    if (page >= 0) settingsPage = std::clamp(page, 0, static_cast<int>(settingsPages().size()) - 1);
    if (view == SETTINGS_VIEW && m_settingsForm) setSettingsPage(settingsPage);
    else setView(SETTINGS_VIEW, false);
}

void F1Panel::toggleSettings() { setView(view == SETTINGS_VIEW ? m_prevView : SETTINGS_VIEW); }

void F1Panel::setSettingsPage(int page) {
    settingsPage = std::clamp(page, 0, static_cast<int>(settingsPages().size()) - 1);
    if (m_settingsForm) m_settingsForm->setPage(settingsPage);
    refreshTabbar(nowSec());
    scroll->verticalScrollBar()->setValue(0);
}

SettingsForm* F1Panel::makeSettingsForm() {
    // The form with unsaved edits, else a fresh one from the current settings (the tray menu may
    // have changed some meanwhile).
    if (m_settingsForm && m_settingsForm->dirty()) return m_settingsForm;
    auto* f = new SettingsForm(m_ctx->settings, m_ctx->store, *m_ctx->theme, m_ctx->saveSettings, {}, settingsPage);
    connect(f, &SettingsForm::changed, this, &F1Panel::updateSettingsBar);
    connect(f, &SettingsForm::pageChanged, this, [this](int i) { settingsPage = i; });
    // Applying settings rebuilds the panel: never inside the form's own click.
    connect(f, &SettingsForm::applied, this, [this] { QTimer::singleShot(0, this, &F1Panel::settingsApplied); });
    connect(f, &SettingsForm::refreshRequested, this, [this] { Q_EMIT settingsAction(QStringLiteral("refresh")); });
    connect(f, &SettingsForm::clearCacheRequested, this, [this] { Q_EMIT settingsAction(QStringLiteral("clear_cache")); });
    connect(f, &SettingsForm::checkUpdatesRequested, this, [this] { Q_EMIT settingsAction(QStringLiteral("check_updates")); });
    if (m_settingsForm && m_settingsForm != scroll->widget()) m_settingsForm->deleteLater();
    m_settingsForm = f;
    return f;
}

void F1Panel::swapBody(QWidget* w) {
    QWidget* old = scroll->takeWidget();
    scroll->setWidget(w);
    w->show();
    if (old && old != w) {
        if (old == m_settingsForm) {   // unsaved settings edits wait for the next visit
            old->setParent(this);
            old->hide();
        } else {
            old->deleteLater();
        }
    }
    tabWidget = qobject_cast<Tab*>(w);
}

void F1Panel::updateSettingsBar() {
    const bool on = view == SETTINGS_VIEW;
    m_settingsBar->setVisible(on);
    const bool dirty = on && m_settingsForm && m_settingsForm->dirty();
    m_settingsState->setText(dirty ? T(QStringLiteral("Unsaved changes")) : T(QStringLiteral("No unsaved changes")));
    m_settingsState->setColor(dirty ? m_ctx->theme->accent : m_ctx->theme->muted);
    for (PillButton* b : {m_revertBtn, m_saveBtn}) {
        b->setEnabled(dirty);
        b->setCursor(dirty ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
}

void F1Panel::settingsApplied() {
    // The app applies the settings (it builds a new popup); with no app attached we rebuild here,
    // so the form shows the saved values.
    const QPointer<SettingsForm> before = m_settingsForm;
    Q_EMIT settingsSaved();
    if (view == SETTINGS_VIEW && before && m_settingsForm == before) rebuildBody();
    updateSettingsBar();
}

void F1Panel::saveSettings() {
    if (m_settingsForm && view == SETTINGS_VIEW) m_settingsForm->save();
}

void F1Panel::revertSettings() {
    if (m_settingsForm) m_settingsForm->setClean();
    if (view == SETTINGS_VIEW) rebuildBody();
}

void F1Panel::cycle(int step) {
    if (view == SETTINGS_VIEW) {
        const int n = static_cast<int>(settingsPages().size());
        setSettingsPage((settingsPage + step + n) % n);
        return;
    }
    const QStringList views = validViews(nowSec());
    const qsizetype i = std::max<qsizetype>(0, views.indexOf(view));
    setView(views[(i + step + views.size()) % views.size()]);
}

bool spoilerView(const QString& v) {
    return v == u"grid" || v == u"live" || v == u"results" || v == u"drivers" || v == u"constructors";
}

bool F1Panel::spoilersHiddenNow(qint64 now) const {
    const DataStore* st = m_ctx->store;
    return spoilers::spoilersHidden(m_ctx->settings->spoiler_free, m_ctx->settings->spoiler_revealed_at, st->race(),
                                    st->get(QStringLiteral("results")), st->get(QStringLiteral("last_session")), now);
}

Tab* F1Panel::makeTab(const QString& v) {
    if (spoilerView(v) && spoilersHiddenNow(nowSec())) return new SpoilerTab(m_ctx, [this] { reveal(); });
    if (v == u"schedule") return new ScheduleTab(m_ctx);
    if (v == u"weather") return new WeatherTab(m_ctx);
    if (v == u"grid") return new GridTab(m_ctx);
    if (v == u"live") return new LiveTab(m_ctx);
    if (v == u"results") return new ResultsTab(m_ctx);
    if (v == u"drivers") return new DriversTab(m_ctx);
    if (v == u"constructors") return new ConstructorsTab(m_ctx);
    return new PendingTab(m_ctx, v);
}

void F1Panel::reveal() {
    // "Show results": everything up to now is no longer a spoiler.
    m_ctx->settings->spoiler_revealed_at = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    m_ctx->saveSettings();
    Q_EMIT m_ctx->store->changed(QStringLiteral("results"));   // every panel and the pill refresh
    rebuildBody();
}

void F1Panel::rebuildBody(bool resetScroll) {
    const qint64 now = nowSec();
    if (view == SETTINGS_VIEW && m_mode != u"popup") view = QStringLiteral("schedule");
    if (view != SETTINGS_VIEW && !validViews(now).contains(view)) view = QStringLiteral("schedule");
    updateHeader();
    m_tabsSig.clear();
    refreshTabbar(now);
    QScrollBar* bar = scroll->verticalScrollBar();
    const int keep = resetScroll ? 0 : bar->value();
    if (view == SETTINGS_VIEW) {
        swapBody(makeSettingsForm());
    } else {
        m_ctx->compact = width() > 0 && width() < m_ctx->theme->px(400);
        Tab* w = makeTab(view);
        connect(w, &Tab::rebuildRequested, this, [this] { QTimer::singleShot(0, this, [this] { rebuildBody(false); }); });
        swapBody(w);
    }
    updateSettingsBar();
    QTimer::singleShot(0, bar, [bar, keep] { bar->setValue(keep); });
}

void F1Panel::tick(qint64 now) {
    if (view == SETTINGS_VIEW) {
        if (now % 30 == 0) updateHeader();
        return;
    }
    // A competitive session just started (or results were revealed): swap the cover in or out.
    if (spoilerView(view) && now % 15 == 0 && spoilersHiddenNow(now) != (qobject_cast<SpoilerTab*>(tabWidget) != nullptr))
        rebuildBody();
    if (tabWidget) tabWidget->tick(now);
    if (now % 30 == 0) {
        refreshTabbar(now);
        if (!validViews(now).contains(view)) setView(QStringLiteral("schedule"), false);
        updateHeader();
    }
}

void F1Panel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    const bool compact = width() < m_ctx->theme->px(400);
    if (compact != m_ctx->compact && view == u"schedule") m_rebuildTimer.start();
}

void F1Panel::keyPressEvent(QKeyEvent* e) {
    const int k = e->key();
    QScrollBar* bar = scroll->verticalScrollBar();
    if (k == Qt::Key_Left) cycle(-1);
    else if (k == Qt::Key_Right) cycle(1);
    else if (k == Qt::Key_R && !e->modifiers()) Q_EMIT refreshRequested();
    else if (k == Qt::Key_Escape) {
        if (view == SETTINGS_VIEW) setView(m_prevView);   // back to the F1 tab it came from
        else Q_EMIT closeRequested();
    } else if (e->matches(QKeySequence::Preferences) || (k == Qt::Key_Comma && (e->modifiers() & Qt::ControlModifier))) {
        if (m_mode == u"popup") toggleSettings();
        else Q_EMIT settingsRequested();
    } else if (e->matches(QKeySequence::Save) && view == SETTINGS_VIEW)
        saveSettings();
    else if (k == Qt::Key_PageDown || k == Qt::Key_Down)
        bar->setValue(bar->value() + (k == Qt::Key_PageDown ? bar->pageStep() : bar->singleStep() * 3));
    else if (k == Qt::Key_PageUp || k == Qt::Key_Up)
        bar->setValue(bar->value() - (k == Qt::Key_PageUp ? bar->pageStep() : bar->singleStep() * 3));
    else QWidget::keyPressEvent(e);
}

void F1Panel::paintEvent(QPaintEvent*) {
    const Theme& t = *m_ctx->theme;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const int rad = t.px(12);
    QPainterPath path;
    path.addRoundedRect(r, rad, rad);
    QColor bg = QColor::fromString(t.surface);
    bg.setAlphaF(static_cast<float>(bgOpacity));
    p.fillPath(path, bg);
    QColor border = QColor::fromString(t.outline);
    border.setAlphaF(bgOpacity > 0.05 ? 0.55f : 0.0f);
    p.setPen(QPen(border, 1));
    p.drawPath(path);
}

}  // namespace f1::ui
