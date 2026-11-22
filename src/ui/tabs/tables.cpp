#include "tables.h"

#include "../../core/weekend.h"

#include "../../core/i18n.h"
#include "../../core/parsers.h"
#include "../../core/pyjson.h"
#include "../../core/spoilers.h"

#include <QHBoxLayout>

namespace f1::ui {

namespace {
QString s(const QJsonObject& o, const char* k) {
    const QJsonValue v = o.value(QLatin1StringView(k));
    return (v.isUndefined() || v.isNull()) ? QString() : py::str(v);
}
}  // namespace

GridTab::GridTab(Ctx* c, QWidget* parent) : Tab(c, parent) {
    const QJsonObject grid = ctx->store->get(QStringLiteral("grid"));
    const QJsonArray rows = grid.value(QStringLiteral("rows")).toArray();
    if (rows.isEmpty()) {
        hint(emptyMessage(grid, T(QStringLiteral("No grid available. It appears here after qualifying."))));
        finish();
        return;
    }
    const Race* race = ctx->store->race();
    const QJsonObject official = ctx->store->get(QStringLiteral("grid_official"));
    const QJsonArray orows = official.value(QStringLiteral("rows")).toArray();
    const bool hasOfficial = !orows.isEmpty() && race &&
                             official.value(QStringLiteral("round")).toString() == race->season + u'-' + race->round;
    const bool showOfficial = hasOfficial && ctx->settings->grid_official;
    header(T(QStringLiteral("STARTING GRID")));
    if (hasOfficial) {
        auto* sw = new QHBoxLayout();
        sw->setSpacing(t.px(4));
        const std::tuple<bool, QString, QString> opts[] = {
            {true, T(QStringLiteral("Official")), T(QStringLiteral("The FIA starting grid with all penalties applied."))},
            {false, T(QStringLiteral("Provisional")), T(QStringLiteral("The order from qualifying, before penalties."))}};
        for (const auto& [flag, label, tip] : opts) {
            auto* b = new PillButton(label, t, flag == showOfficial);
            b->setToolTip(tip);
            const bool f = flag;
            connect(b, &PillButton::clicked, this, [this, f] {
                ctx->settings->grid_official = f;
                ctx->saveSettings();
                Q_EMIT ctx->store->changed(QStringLiteral("grid"));   // the panel rebuilds the Grid tab
            });
            sw->addWidget(b);
        }
        sw->addStretch(1);
        lay->addLayout(sw);
    }
    if (showOfficial) {
        qint64 penalised = 0;
        for (const QJsonValue& r : orows) penalised += py::toInt(r[QStringLiteral("moved")]).value_or(0) > 0 ? 1 : 0;
        const QString note = penalised == 1 ? T(QStringLiteral("1 driver starts further back than qualified."))
                             : penalised ? T(QStringLiteral("{count} drivers start further back than qualified."),
                                             {{"count", QString::number(penalised)}})
                                         : T(QStringLiteral("Same order as qualifying."));
        hint(T(QStringLiteral("Official grid with penalties (OpenF1), checked again just before the race.")) + u' ' + note);
        for (const QJsonValue& rv : orows) {
            const QJsonObject row = rv.toObject();
            penaltyNote(resultRow(row, 80), row);   // "+5 places" after the team
        }
    } else {
        // Never present the qualifying order as the final grid.
        const QJsonValue prov = grid.value(QStringLiteral("provisional"));
        hint((prov.isUndefined() || prov.toBool()) ? T(QStringLiteral("Provisional: the qualifying order, before penalties."))
                                                   : T(QStringLiteral("Official starting grid")));
        for (const QJsonValue& rv : rows) resultRow(rv.toObject(), 80);
    }
    finish();
}

ResultsTab::ResultsTab(Ctx* c, QWidget* parent) : Tab(c, parent) {
    const QJsonObject res = ctx->store->get(QStringLiteral("results"));
    const QJsonArray rows = res.value(QStringLiteral("rows")).toArray();
    if (rows.isEmpty()) {
        hint(emptyMessage(res, T(QStringLiteral("No results yet"))));
        finish();
        return;
    }
    header(T(QStringLiteral("LAST RACE · {upper}"), {{"upper", s(res, "race_name").toUpper()}}));
    QStringList bits;
    if (const qint64 d = py::toInt(res.value(QStringLiteral("date_ts"))).value_or(0)) bits << fmtDate(d, ctx->zone());
    if (!s(res, "circuit").isEmpty()) bits << s(res, "circuit");
    if (!bits.isEmpty()) hint(bits.join(QStringLiteral(" · ")));

    // The weekend's sessions: OpenF1's list when it is loaded, else what Jolpica has.
    const QString round = s(res, "season") + u'-' + s(res, "round");
    auto sameRound = [&](const QJsonObject& o) { return s(o, "season") + u'-' + s(o, "round") == round; };
    const QJsonObject quali = ctx->store->get(QStringLiteral("last_qualifying"));
    const QJsonObject sprint = ctx->store->get(QStringLiteral("last_sprint"));
    const bool haveQuali = sameRound(quali) && !quali.value(QStringLiteral("rows")).toArray().isEmpty();
    const bool haveSprint = sameRound(sprint) && !sprint.value(QStringLiteral("rows")).toArray().isEmpty();
    const QJsonObject weekends = ctx->store->get(QStringLiteral("weekends"));
    const QString meetingKey = py::str(weekends.value(QStringLiteral("rounds")).toObject().value(round));
    const QJsonObject meeting = weekends.value(QStringLiteral("meetings")).toObject().value(meetingKey).toObject();
    const QJsonObject ofResults = meeting.value(QStringLiteral("results")).toObject();

    struct Item {
        QString id, label, kind, sessionKey, name;
        qint64 start = 0;
    };
    QList<Item> items;
    for (const QJsonValue& v : weekend::startedSessions(meeting, nowSec())) {
        const QJsonObject sess = v.toObject();
        const QString kind = weekend::sessionKind(sess);
        items.append({kind == u"race" ? QStringLiteral("race") : py::str(sess.value(QStringLiteral("key"))), weekend::pillLabel(sess), kind,
                      py::str(sess.value(QStringLiteral("key"))), s(sess, "name"), py::toInt(sess.value(QStringLiteral("start"))).value_or(0)});
    }
    bool hasRace = false;
    for (const Item& i : items) hasRace |= i.kind == u"race";
    if (!hasRace) {   // no OpenF1 list for this weekend: Jolpica's sessions only
        items.clear();
        if (haveSprint) items.append({QStringLiteral("sprint"), QStringLiteral("Sprint"), QStringLiteral("sprint"), {}, QStringLiteral("Sprint")});
        if (haveQuali) items.append({QStringLiteral("quali"), QStringLiteral("Quali"), QStringLiteral("quali"), {}, QStringLiteral("Qualifying")});
        items.append({QStringLiteral("race"), QStringLiteral("Race"), QStringLiteral("race"), {}, QStringLiteral("Race")});
    }
    // The grid the race started from (after penalties, pit-lane starts), from Jolpica's race result.
    const QJsonArray startGrid = parsers::gridFromResults(res);
    if (!startGrid.isEmpty())
        for (qsizetype i = 0; i < items.size(); ++i)
            if (items[i].kind == u"race") {
                items.insert(i, {QStringLiteral("grid"), QStringLiteral("Grid"), QStringLiteral("grid"), {}, QStringLiteral("Starting grid")});
                break;
            }
    QString current = QStringLiteral("race");
    for (const Item& i : items)
        if (i.id == ctx->picks.value(QStringLiteral("last"))) current = i.id;
    if (items.size() > 1) {
        QList<std::pair<QString, QString>> pills;
        for (const Item& i : items) pills.append({i.id, i.label});
        sessionPills(pills, current, QStringLiteral("last"));
    }
    const Item* picked = nullptr;
    for (const Item& i : items)
        if (i.id == current) picked = &i;

    if (!picked || picked->kind == u"race") {
        const QJsonObject fl = res.value(QStringLiteral("fastest")).toObject();
        if (!fl.isEmpty())
            hint(T(QStringLiteral("Fastest lap  {driver}  {time}  (lap {lap})"),
                   {{"driver", s(fl, "code")}, {"time", s(fl, "time")}, {"lap", s(fl, "lap")}}));
        for (const QJsonValue& rv : rows) {
            const QJsonObject row = rv.toObject();
            Row* r = resultRow(row, 92, true);
            if (!s(row, "status").isEmpty())
                r->setToolTip(T(QStringLiteral("{name} · {status}"), {{"name", s(row, "name")}, {"status", s(row, "status")}}) +
                              (s(row, "url").isEmpty() ? QString() : T(QStringLiteral(" · Open on Wikipedia"))));
        }
        finish();
        return;
    }
    if (picked->kind == u"grid") {
        // Official: the grid the race started from (Jolpica). Provisional: the qualifying order.
        QJsonArray provisional = haveQuali ? quali.value(QStringLiteral("rows")).toArray() : QJsonArray();
        if (provisional.isEmpty())
            for (const Item& i : items)
                if (i.kind == u"quali") provisional = ofResults.value(i.sessionKey).toObject().value(QStringLiteral("rows")).toArray();
        gridView(startGrid, provisional, T(QStringLiteral("The grid the race started from, with penalties (Jolpica).")));
        finish();
        return;
    }
    if (picked->kind == u"quali" && haveQuali) {   // Jolpica: best time of each driver's last part
        for (const QJsonValue& rv : quali.value(QStringLiteral("rows")).toArray()) resultRow(rv.toObject(), 92, false);
        finish();
        return;
    }
    if (picked->kind == u"sprint" && haveSprint) {
        for (const QJsonValue& rv : sprint.value(QStringLiteral("rows")).toArray()) {
            const QJsonObject row = rv.toObject();
            Row* r = resultRow(row, 92, true);
            if (!s(row, "status").isEmpty())
                r->setToolTip(T(QStringLiteral("{name} · {status}"), {{"name", s(row, "name")}, {"status", s(row, "status")}}));
        }
        finish();
        return;
    }
    // Practice and sprint qualifying (and anything Jolpica lacks): OpenF1's classification.
    const QJsonArray ofRows = ofResults.value(picked->sessionKey).toObject().value(QStringLiteral("rows")).toArray();
    if (ofRows.isEmpty()) {
        hint(T(QStringLiteral("OpenF1 has no classification for {name}."), {{"name", picked->name}}));
    } else {
        hint(T(QStringLiteral("{name} · final classification · {when} · OpenF1"),
               {{"name", picked->name}, {"when", fmtDatetime(picked->start, ctx->zone(), ctx->twelve(), true)}}));
        classificationRows(ofRows);
    }
    finish();
}

DriversTab::DriversTab(Ctx* c, QWidget* parent) : Tab(c, parent) {
    const QJsonObject d = ctx->store->get(QStringLiteral("drivers"));
    const QJsonArray rows = d.value(QStringLiteral("rows")).toArray();
    if (rows.isEmpty()) {
        hint(emptyMessage(d, T(QStringLiteral("No standings available"))));
        finish();
        return;
    }
    header(T(QStringLiteral("DRIVER STANDINGS")));
    for (const QJsonValue& rv : rows) standingRow(rv.toObject(), s(rv.toObject(), "code"), s(rv.toObject(), "team"));
    finish();
}

ConstructorsTab::ConstructorsTab(Ctx* c, QWidget* parent) : Tab(c, parent) {
    const QJsonObject d = ctx->store->get(QStringLiteral("constructors"));
    const QJsonArray rows = d.value(QStringLiteral("rows")).toArray();
    if (rows.isEmpty()) {
        hint(emptyMessage(d, T(QStringLiteral("No standings available"))));
        finish();
        return;
    }
    header(T(QStringLiteral("CONSTRUCTOR STANDINGS")));
    for (const QJsonValue& rv : rows) standingRow(rv.toObject(), s(rv.toObject(), "name"), QString());
    finish();
}

SpoilerTab::SpoilerTab(Ctx* c, std::function<void()> reveal, QWidget* parent) : Tab(c, parent) {
    header(T(QStringLiteral("SPOILER-FREE MODE")));
    const Race* race = ctx->store->race();
    const qint64 now = nowSec();
    bool started = false;
    if (race)
        for (const auto& se : race->sessions) started |= spoilers::isCompetitive(se.id) && se.ts <= now;
    QString what = started ? race->name : s(ctx->store->get(QStringLiteral("results")), "race_name");
    if (what.isEmpty()) what = QStringLiteral("the last race");
    if (spoilers::sessionOver(race, now))
        hint(T(QStringLiteral("Results from {what} are hidden until you've watched it."), {{"what", what}}));
    else
        hint(T(QStringLiteral("A session at {what} is running. Live timing and results are hidden."), {{"what", what}}));
    auto* link = new ClickText(T(QStringLiteral("Show results")), t, 13, true, t.accent);
    link->enableClicks();
    link->setToolTip(T(QStringLiteral("Reveals everything up to now. Hidden again when the next qualifying, sprint or race starts.")));
    connect(link, &ClickText::clicked, this, [reveal] { reveal(); });
    lay->addWidget(link);
    hint(T(QStringLiteral("Turn spoiler-free mode off in Settings › Formula 1.")), t.muted);
    finish();
}

}  // namespace f1::ui
