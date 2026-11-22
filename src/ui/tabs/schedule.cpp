#include "schedule.h"

#include "../../core/i18n.h"
#include "../../core/pilltext.h"
#include "weather.h"

#include <QHBoxLayout>

namespace f1::ui {

ScheduleTab::ScheduleTab(Ctx* c, QWidget* parent) : Tab(c, parent) {
    m_animTimer.setInterval(40);
    connect(&m_animTimer, &QTimer::timeout, this, &ScheduleTab::animStep);
    const Race* race = ctx->store->race();
    if (!race) {
        const bool loading = ctx->store->status.value(QStringLiteral("loading")).toBool();
        hint(loading ? T(QStringLiteral("Loading…")) : T(QStringLiteral("No upcoming race")));
        finish();
        return;
    }

    auto* top = new QWidget();
    auto* topL = new QHBoxLayout(top);
    topL->setContentsMargins(t.px(4), t.px(15), 0, 0);
    topL->setSpacing(t.px(8));
    auto* left = new QWidget();
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(t.px(3));
    ll->addWidget(new Wrap(race->circuitName, t, 14, t.onSurface));
    ll->addWidget(new Text(race->location(), t, 12, false, t.muted));
    ll->addSpacing(t.px(6));
    m_gantry = new LightsGantry(t);
    ll->addWidget(m_gantry);
    ll->addSpacing(t.px(4));
    m_cdLabel = new Text(T(QStringLiteral("LIGHTS OUT")), t, 11, true, t.muted, Qt::AlignLeft, 0.6);
    ll->addWidget(m_cdLabel);
    m_countdown = new Text(QString(), t, 22, true, t.accent);
    m_countdown->setAccessibleName(T(QStringLiteral("Countdown")));
    ll->addWidget(m_countdown);
    ll->addStretch(1);
    topL->addWidget(left, 1);
    if (const Outline* pts = ctx->tracks->points(race->circuitId)) {
        m_track = ctx->compact ? new TrackView(*pts, t, 120, 108) : new TrackView(*pts, t, 170, 150);
        m_track->setToolTip(race->circuitName);
        topL->addWidget(m_track);
    }
    lay->addWidget(top);
    lay->addSpacing(t.px(10));

    const qint64 now = nowSec();
    const weather::Forecast* fc = forecastFor(ctx->store, race);
    // Table header, as on the Weather tab: SESSIONS (title) · START · SKY · TEMP · RAIN
    const QJsonObject measured = measuredFor(ctx->store, race);
    const bool hasWx = fc || !measured.isEmpty();
    const int nameW = ctx->compact ? 116 : 128;
    auto* head = new Row(t);
    head->lay->setContentsMargins(t.px(4), 0, t.px(4), t.px(2));
    head->cell(withHelp(new Text(T(QStringLiteral("SESSIONS")), t, 12, true, t.onSurface, Qt::AlignLeft, 0.4), t), nameW);
    head->cell(new Text(T(QStringLiteral("START")), t, 10, true, t.muted, Qt::AlignLeft, 0.3), 92);
    QStringList tip{T(QStringLiteral("Start: the start time in your time zone."))};
    if (hasWx) {
        head->cell(new Text(T(QStringLiteral("SKY")), t, 10, true, t.muted, Qt::AlignLeft, 0.3), -1, 1);
        head->cell(new Text(T(QStringLiteral("TEMP")), t, 10, true, t.muted, Qt::AlignRight, 0.3), TEMP_W);
        head->cell(new Text(T(QStringLiteral("RAIN")), t, 10, true, t.muted, Qt::AlignRight, 0.3), RAIN_W);
        tip << T(QStringLiteral("Sky, temperature and the highest chance of rain during each session. Finished sessions "
                                "show what F1 measured at the track (Rained / Dry, Wet / Dry). More on the Weather tab."));
    } else {
        head->cell(new QWidget(), -1, 1);
    }
    head->setToolTip(tip.join(u'\n'));
    lay->addWidget(head);

    for (const Session& s : race->sessions) {
        auto* r = new Row(t);
        auto* nameCell = new Text(s.name(), t, 13);
        r->cell(nameCell, nameW);
        const bool delayed = s.delay != 0;
        auto* when = new Text(fmtDatetime(s.ts, ctx->zone(), ctx->twelve()) + (delayed ? QStringLiteral("*") : QString()), t, 13,
                              false, delayed ? t.secondary : QString());
        r->cell(when, 92);
        QStringList tips{fmtDatetime(s.ts, ctx->zone(), ctx->twelve(), true) + QStringLiteral("  ") + utcOffsetLabel(s.ts, ctx->zone())};
        if (s.source == u"feed") tips << T(QStringLiteral("Start time from F1's live timing (revised)"));
        else if (s.source == u"manual") tips << T(QStringLiteral("Start time corrected by hand in Settings"));
        else tips << T(QStringLiteral("Published schedule time"));
        if (delayed)
            tips << T(QStringLiteral("* {delay} min vs. published time"),
                      {{"delay", (s.delay > 0 ? QStringLiteral("+") : QString()) + QString::number(s.delay)}});
        if (hasWx) {
            const QString wxTip = sessionCells(this, r, s, fc, measured, now, 13);
            if (!wxTip.isEmpty()) tips << wxTip;
        } else {
            r->cell(new QWidget(), -1, 1);
        }
        r->setToolTip(tips.join(u'\n'));
        lay->addWidget(r);
        m_rows.append({s, nameCell, when});
    }
    finish();
    tick(now);
}

void ScheduleTab::onShown() {
    if (!m_gantry || !ctx->settings->animations) return;
    m_anim.start();
    m_animating = true;
    m_animTimer.start();
    if (m_track) m_track->animateIn(INTRO_FADE_MS, 900);
    animStep();
}

bool ScheduleTab::raceUnderway(qint64 now) const {
    const Race* race = ctx->store->race();
    const auto ts = race ? sessionTs(race->sessions, QStringLiteral("race")) : std::nullopt;
    return ts && now >= *ts;
}

void ScheduleTab::animStep() {
    if (!m_animating || !m_gantry) {
        m_animTimer.stop();
        return;
    }
    const qint64 ms = m_anim.elapsed();
    const int cols = LightsGantry::COLUMNS;
    const int lit = static_cast<int>(std::min<qint64>(cols, ms / LIGHT_STEP_MS));
    const qint64 doneAt = cols * LIGHT_STEP_MS + LIGHT_HOLD_MS;
    const bool go = ms >= doneAt && raceUnderway(nowSec());
    m_gantry->setState(go ? cols : lit, go);
    if (ms >= doneAt + 400) {
        m_animTimer.stop();
        m_animating = false;
    }
}

void ScheduleTab::tick(qint64 now) {
    const Race* race = ctx->store->race();
    if (!race || !m_gantry) return;
    if (!m_animating) m_gantry->setState(LightsGantry::COLUMNS, raceUnderway(now));
    const auto [label, text, live] = pill::countdownState(race, *ctx->settings, now);
    m_cdLabel->setText(label);
    m_cdLabel->setColor(live ? t.accent : t.muted);
    m_countdown->setText(text);
    const auto nxt = nextSession(race->sessions, now);
    for (const SessionRow& r : m_rows) {
        const bool isNext = nxt && r.s.ts == nxt->ts;
        const bool past = r.s.ts <= now;
        r.name->setBold(isNext);
        r.name->setColor(isNext ? t.accent : (past ? t.muted : t.onSurface));
        r.when->setColor(isNext ? t.onSurface : t.muted);
    }
}

}  // namespace f1::ui
