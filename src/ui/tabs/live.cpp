#include "live.h"

#include "../../core/i18n.h"
#include "../../core/pyjson.h"
#include "../../core/teams.h"
#include "../../core/parsers.h"
#include "../../core/weekend.h"

#include <QHBoxLayout>

namespace f1::ui {

namespace {
QString s(const QJsonObject& o, const char* k) {
    const QJsonValue v = o.value(QLatin1StringView(k));
    return (v.isUndefined() || v.isNull()) ? QString() : py::str(v);
}
}  // namespace

QString liveMessage(const QString& code) {
    if (code == u"auth")
        return T(QStringLiteral("Live data during a session needs the OpenF1 Sponsor plan. Add your OpenF1 username and password in "
                                "Settings › Live data (stored in Windows Credential Manager). On the Free plan the session's "
                                "results appear here after it ends, and every other tab keeps working."));
    if (code == u"login")
        return T(QStringLiteral("OpenF1 rejected the stored username and password. Check them in Settings › Formula 1."));
    if (code == u"token") return T(QStringLiteral("OpenF1 refused the access token. A new one is requested automatically."));
    if (code == u"rate") return T(QStringLiteral("The live feed is rate limited; it will retry shortly."));
    if (code == u"net") return T(QStringLiteral("The live feed could not be reached; retrying."));
    if (code == u"free")
        return T(QStringLiteral("Live timing during a session needs the OpenF1 Sponsor plan (Settings › Live data). On the free "
                                "plan the full classification appears below about 20–30 minutes after the session ends."));
    return {};
}

LiveTab::LiveTab(Ctx* c, QWidget* parent) : Tab(c, parent) {
    const qint64 now = nowSec();
    const Race* race = ctx->store->race();
    const Sessions sessions = race ? race->sessions : Sessions();
    const QJsonObject live = ctx->store->get(QStringLiteral("live"));
    if (!liveActive(live, sessions, now, ctx->settings->debug_force_tabs)) {
        header(T(QStringLiteral("LIVE TIMING")));
        const QString problem = s(live, "error");
        const bool near = race && nearSession(sessions, now);
        if (near && !problem.isEmpty()) {
            lay->addWidget(new Wrap(T(QStringLiteral("A session is running, but live timing is not available.")), t, 13, t.onSurface));
        } else {
            lay->addWidget(new Wrap(T(QStringLiteral("No session running right now.")), t, 13, t.onSurface));
            hint(T(QStringLiteral("Live positions and track flags appear here automatically during practice, qualifying and the race.")));
        }
        if (!liveMessage(problem).isEmpty()) hint(liveMessage(problem));
        else if (ctx->settings->live_source == u"openf1" && near)
            hint(liveMessage(ctx->settings->openf1_plan == u"free" ? QStringLiteral("free") : QStringLiteral("auth")));
        if (const auto nxt = race ? nextSession(sessions, now) : std::nullopt)
            lay->addWidget(new Text(T(QStringLiteral("Next: {name} · {fmt_datetime}"),
                                      {{"name", nxt->name()}, {"fmt_datetime", fmtDatetime(nxt->ts, ctx->zone(), ctx->twelve())}}),
                                    t, 12, true, t.accent));
        lastSession();
        finish();
        return;
    }

    const QJsonObject session = live.value(QStringLiteral("session")).toObject();
    QString title = QStringLiteral("● LIVE");
    if (!s(session, "name").isEmpty()) {
        const qint64 part = py::toInt(session.value(QStringLiteral("part"))).value_or(0);
        title = QStringLiteral("● LIVE · ") + s(session, "name") + (part > 0 ? QStringLiteral(" Q%1").arg(part) : QString());
    }
    auto* head = new QWidget();
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(t.px(10));
    hl->addWidget(new Text(title, t, 12, true, t.accent));
    m_clock = live.value(QStringLiteral("clock")).toObject();
    if (!m_clock.isEmpty()) {
        m_clockLabel = new Text(QString(), t, 12, false, t.onSurface);
        hl->addWidget(m_clockLabel);
    }
    const QString flag = friendlyFlag(s(live, "flag"));
    if (!flag.isEmpty()) hl->addWidget(new Text(flag, t, 12, false, t.muted));
    hl->addStretch(1);
    lay->addWidget(head);
    const QJsonArray msgs = live.value(QStringLiteral("messages")).toArray();
    if (!msgs.isEmpty()) {
        auto* m = new Wrap(msgs.first().toString(), t, 11, t.muted);
        QStringList all;
        for (const QJsonValue& x : msgs) all << x.toString();
        m->setToolTip(all.join(u'\n'));
        lay->addWidget(m);
    }

    const QString fd = ctx->settings->favorite_driver.toUpper();
    const bool qualifying = s(session, "kind") == u"Qualifying";
    const QJsonArray rows = live.value(QStringLiteral("rows")).toArray();
    for (qsizetype i = 0; i < rows.size() && i < 24; ++i) {
        const QJsonObject row = rows[i].toObject();
        const QString state = s(row, "state");
        const bool dimmed = state == u"out" || state == u"prev";
        auto* r = new Row(t);
        if (ctx->settings->team_colors && !s(row, "color").isEmpty())
            if (Chip* ch = ctx->chip(row, s(row, "color"))) r->cell(ch);
        r->cell(posLabel(row.value(QStringLiteral("pos"))), 28);
        const QString code = s(row, "code");
        r->cell(new Text(code, t, 13, true, (!fd.isEmpty() && code == fd) ? t.accent : (state == u"out" ? t.muted : t.onSurface)), 46);
        QString team = s(row, "team");
        const qint64 outPart = py::toInt(row.value(QStringLiteral("out_part"))).value_or(0);
        if (state == u"out" && outPart > 0) team = QStringLiteral("out Q%1").arg(outPart);
        else if (row.value(QStringLiteral("in_pit")).toBool()) team += QStringLiteral("  ·  PIT");
        r->cell(new Text(team, t, 13, false, t.muted), -1, 1);
        const QJsonValue pits = row.value(QStringLiteral("pits"));
        if (!pits.isNull() && !pits.isUndefined() && !session.isEmpty() && !qualifying)
            r->cell(new Text(T(QStringLiteral("{pits} pit"), {{"pits", py::str(pits)}}), t, 12, false, t.muted, Qt::AlignRight), 44);
        if (!s(row, "best").isEmpty() || !s(row, "gap").isEmpty()) {
            r->cell(new Text(s(row, "best"), t, 13, false, dimmed ? t.muted : t.onSurface, Qt::AlignRight), 80);
            r->cell(new Text(s(row, "gap"), t, 12, false, state == u"danger" ? t.error : t.muted, Qt::AlignRight), 60);
        }
        lay->addWidget(r);
    }
    finish();
    tick(now);
}

void LiveTab::lastSession() {
    // Every session of the latest weekend (OpenF1), as pills; the picked one's final classification.
    const qint64 now = nowSec();
    const QJsonObject ls = ctx->store->get(QStringLiteral("last_session"));
    const QJsonObject lsSession = ls.value(QStringLiteral("session")).toObject();
    const QString meetingKey = py::str(lsSession.value(QStringLiteral("meeting")));
    const QJsonObject meeting = ctx->store->get(QStringLiteral("weekends"))
                                    .value(QStringLiteral("meetings")).toObject().value(meetingKey).toObject();
    const QJsonArray started = weekend::startedSessions(meeting, now);
    if (started.isEmpty()) {   // the weekend list is not loaded yet: the latest session alone
        const QJsonArray rows = ls.value(QStringLiteral("rows")).toArray();
        if (rows.isEmpty()) return;
        lay->addSpacing(t.px(10));
        QString where = s(lsSession, "location");
        if (where.isEmpty()) where = s(lsSession, "circuit");
        header((QStringLiteral("LAST SESSION · ") + where.toUpper() + QStringLiteral(" · ") + s(lsSession, "name").toUpper())
                   .replace(QStringLiteral(" ·  · "), QStringLiteral(" · ")));
        const qint64 start = py::toInt(lsSession.value(QStringLiteral("start"))).value_or(0);
        const QString when = start ? fmtDatetime(start, ctx->zone(), ctx->twelve(), true) : QString();
        hint(when.isEmpty() ? T(QStringLiteral("Final classification · OpenF1"))
                            : T(QStringLiteral("Final classification · {when} · OpenF1"), {{"when", when}}));
        classificationRows(rows);
        return;
    }
    const QJsonObject results = meeting.value(QStringLiteral("results")).toObject();
    auto rowsFor = [&](const QJsonObject& sess) {
        const QString key = py::str(sess.value(QStringLiteral("key")));
        QJsonArray rows = results.value(key).toObject().value(QStringLiteral("rows")).toArray();
        if (rows.isEmpty() && key == py::str(lsSession.value(QStringLiteral("key"))))
            rows = ls.value(QStringLiteral("rows")).toArray();   // fetched first by the latest-session loader
        return rows;
    };
    // The starting grid, once qualifying has a classification: official from OpenF1's starting grid
    // (or, once raced, the race result), provisional = the qualifying order.
    const QJsonObject res = ctx->store->get(QStringLiteral("results"));
    const QString resRound = s(res, "season") + u'-' + s(res, "round");
    const QJsonObject weekends = ctx->store->get(QStringLiteral("weekends"));
    const Race* race = ctx->store->race();
    const QString round = py::str(weekends.value(QStringLiteral("rounds")).toObject().value(resRound)) == meetingKey
                              ? resRound
                              : (race ? race->season + u'-' + race->round : QString());
    QJsonArray gridOfficial, gridProvisional;
    const QJsonObject go = ctx->store->get(QStringLiteral("grid_official"));
    if (s(go, "round") == round) gridOfficial = go.value(QStringLiteral("rows")).toArray();
    bool gridFromRace = false;
    if (gridOfficial.isEmpty() && round == resRound) {
        gridOfficial = parsers::gridFromResults(res);
        gridFromRace = !gridOfficial.isEmpty();
    }
    QList<std::pair<QString, QString>> items;
    QString current, fallback;
    for (const QJsonValue& v : started) {
        const QJsonObject sess = v.toObject();
        const QString key = py::str(sess.value(QStringLiteral("key")));
        if (weekend::sessionKind(sess) == u"race" && (!gridOfficial.isEmpty() || !gridProvisional.isEmpty()))
            items.append({QStringLiteral("grid"), QStringLiteral("Grid")});
        items.append({key, weekend::pillLabel(sess)});
        if (key == ctx->picks.value(QStringLiteral("live"))) current = key;
        if (!rowsFor(sess).isEmpty()) fallback = key;   // the latest one with a classification
        if (weekend::sessionKind(sess) == u"quali") gridProvisional = rowsFor(sess);
    }
    // Qualifying done but the race not started yet: the Grid pill goes last.
    const bool raceStarted = std::any_of(items.begin(), items.end(), [](const auto& i) { return i.first == u"grid"; });
    if (!raceStarted && (!gridOfficial.isEmpty() || !gridProvisional.isEmpty())) items.append({QStringLiteral("grid"), QStringLiteral("Grid")});
    if (ctx->picks.value(QStringLiteral("live")) == u"grid" && (!gridOfficial.isEmpty() || !gridProvisional.isEmpty()))
        current = QStringLiteral("grid");
    if (current.isEmpty()) current = fallback.isEmpty() ? items.last().first : fallback;
    lay->addSpacing(t.px(10));
    header(T(QStringLiteral("SESSIONS · {where}"), {{"where", meeting.value(QStringLiteral("location")).toString().toUpper()}}));
    sessionPills(items, current, QStringLiteral("live"));
    if (current == u"grid") {
        gridView(gridOfficial, gridProvisional,
                 gridFromRace ? T(QStringLiteral("The grid the race started from, with penalties (Jolpica)."))
                              : T(QStringLiteral("Official grid with penalties, checked again just before the race.")));
        return;
    }
    for (const QJsonValue& v : started) {
        const QJsonObject sess = v.toObject();
        if (py::str(sess.value(QStringLiteral("key"))) != current) continue;
        const QJsonArray rows = rowsFor(sess);
        const qint64 start = py::toInt(sess.value(QStringLiteral("start"))).value_or(0);
        const qint64 end = py::toInt(sess.value(QStringLiteral("end"))).value_or(0);
        if (rows.isEmpty()) {
            if (now < end) hint(T(QStringLiteral("{name} is running; live timing shows above while it is on."), {{"name", s(sess, "name")}}));
            else if (now > end + weekend::FINAL_AFTER_S) hint(T(QStringLiteral("OpenF1 has no classification for {name}."), {{"name", s(sess, "name")}}));
            else hint(T(QStringLiteral("The classification of {name} appears here about 20 to 30 minutes after it ends."), {{"name", s(sess, "name")}}));
            return;
        }
        hint(T(QStringLiteral("{name} · final classification · {when} · OpenF1"),
               {{"name", s(sess, "name")}, {"when", fmtDatetime(start, ctx->zone(), ctx->twelve(), true)}}));
        classificationRows(rows);
    }
}

void LiveTab::tick(qint64 now) {
    if (!m_clockLabel || m_clock.isEmpty()) return;
    const double remaining = m_clock.value(QStringLiteral("remaining")).toDouble();
    const double at = m_clock.value(QStringLiteral("at")).toDouble();
    const double left = remaining - (m_clock.value(QStringLiteral("running")).toBool() ? (now - at) : 0);
    m_clockLabel->setText(T(QStringLiteral("{fmt_clock} left"), {{"fmt_clock", fmtClock(left)}}));
}

}  // namespace f1::ui
