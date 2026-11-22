#include "common.h"

#include "../../core/i18n.h"
#include "../../core/pyjson.h"
#include "../../core/teams.h"

namespace f1::ui {

namespace {
QString s(const QJsonObject& o, const char* k) {
    const QJsonValue v = o.value(QLatin1StringView(k));
    return (v.isUndefined() || v.isNull()) ? QString() : py::str(v);
}
}  // namespace

bool Ctx::isFavorite(const QJsonObject& row) const {
    const QString fd = settings->favorite_driver.toUpper().remove(u' ');
    const QString ft = settings->favorite_team.toLower().remove(u' ');
    return (!fd.isEmpty() && s(row, "code") == fd) || (!ft.isEmpty() && s(row, "team_id") == ft);
}

Chip* Ctx::chip(const QJsonObject& row, const QString& explicitColor) const {
    if (!settings->team_colors) return nullptr;
    // Jolpica rows name the team (team_id); OpenF1 rows bring their own colour.
    QString color = explicitColor;
    if (color.isEmpty()) color = s(row, "team_id").isEmpty() ? s(row, "color") : teamColor(s(row, "team_id"));
    if (color.isEmpty()) color = teamColor(QString());
    return new Chip(color, *theme);
}

QString emptyMessage(const QJsonObject& d, const QString& noneText) {
    if (d.value(QStringLiteral("failed")).toBool()) return T(QStringLiteral("Couldn't reach F1 data right now. Retrying automatically."));
    if (d.value(QStringLiteral("loaded")).toBool()) return noneText;
    return QStringLiteral("Loading…");
}

Tab::Tab(Ctx* c, QWidget* parent) : QWidget(parent), ctx(c), t(*c->theme), lay(new QVBoxLayout(this)) {
    lay->setContentsMargins(0, t.px(8), t.px(4), 0);
    lay->setSpacing(t.px(4));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
}

Text* Tab::header(const QString& text) {
    auto* w = new Text(text, t, 11, true, t.muted, Qt::AlignLeft, 0.3);
    lay->addWidget(w);
    return w;
}

Wrap* Tab::hint(const QString& text, const QString& color) {
    auto* w = new Wrap(text, t, 12, color);
    lay->addWidget(w);
    return w;
}

Text* Tab::posLabel(const QJsonValue& pos) {
    const auto n = py::toInt(pos);
    const QString text = (pos.isUndefined() || pos.isNull()) ? QString() : py::str(pos);
    return new Text(text, t, 13, false, (n && *n <= 3) ? t.onSurface : t.muted, Qt::AlignRight);
}

Row* Tab::makeRow(const QJsonObject& row) { return new Row(t, s(row, "url"), ctx->openLink); }

Row* Tab::resultRow(const QJsonObject& row, int detailWidth, bool points) {
    Row* r = makeRow(row);
    if (Chip* c = ctx->chip(row)) r->cell(c);
    r->cell(posLabel(row.value(QStringLiteral("pos"))), 28);
    r->cell(new Text(s(row, "code"), t, 13, true, ctx->isFavorite(row) ? t.accent : t.onSurface), 44);
    auto* team = new Text(s(row, "team"), t, 13, false, t.muted);
    team->setToolTip(s(row, "name"));
    r->cell(team, -1, 1);
    r->cell(new Text(s(row, "detail"), t, 13, false, t.onSurface, Qt::AlignRight), detailWidth);
    if (points) {
        const QString pts = s(row, "pts");
        r->cell(new Text(pts != u"0" ? pts : QString(), t, 13, false, t.muted, Qt::AlignRight), 32);
    }
    lay->addWidget(r);
    return r;
}

void Tab::sessionPills(const QList<std::pair<QString, QString>>& items, const QString& current, const QString& pickKey) {
    auto* bar = new QWidget();
    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(0, t.px(2), 0, t.px(4));
    hl->setSpacing(t.px(4));
    for (const auto& [id, label] : items) {
        auto* b = new PillButton(label, t, id == current);
        connect(b, &PillButton::clicked, this, [this, id, pickKey] {
            ctx->picks.insert(pickKey, id);
            Q_EMIT rebuildRequested();
        });
        hl->addWidget(b);
    }
    hl->addStretch(1);
    lay->addWidget(bar);
}

void Tab::classificationRows(const QJsonArray& rows) {
    const QString fd = ctx->settings->favorite_driver.toUpper();
    for (const QJsonValue& rv : rows) {
        const QJsonObject row = rv.toObject();
        auto* r = new Row(t);
        if (ctx->settings->team_colors && !s(row, "color").isEmpty())
            if (Chip* ch = ctx->chip(row, s(row, "color"))) r->cell(ch);
        r->cell(posLabel(row.value(QStringLiteral("pos"))), 28);
        const QString code = s(row, "code");
        r->cell(new Text(code, t, 13, true, (!fd.isEmpty() && code == fd) ? t.accent : t.onSurface), 44);
        r->cell(new Text(s(row, "team"), t, 13, false, t.muted), -1, 1);
        r->cell(new Text(s(row, "detail"), t, 13, false, s(row, "status").isEmpty() ? t.onSurface : t.muted, Qt::AlignRight), 92);
        // Qualifying fills the last column with the gap to pole; a race or sprint with its points.
        QString last = s(row, "gap");
        const QString pts = s(row, "pts");
        if (last.isEmpty() && !pts.isEmpty() && pts != u"0") last = T(QStringLiteral("{pts} pts"), {{"pts", pts}});
        r->cell(new Text(last, t, 12, false, t.muted, Qt::AlignRight), 78);
        const QJsonValue laps = row.value(QStringLiteral("laps"));
        if (!laps.isNull() && !laps.isUndefined())
            r->setToolTip(T(QStringLiteral("{laps} laps"), {{"laps", py::str(laps)}}) +
                          (pts.isEmpty() ? QString() : T(QStringLiteral(" · {pts} pts"), {{"pts", pts}})));
        lay->addWidget(r);
    }
}

void Tab::penaltyNote(Row* r, const QJsonObject& row) {
    const qint64 m = py::toInt(row.value(QStringLiteral("moved"))).value_or(0);
    if (m <= 0) return;
    int teamI = -1;
    for (int i = 0; i < r->lay->count(); ++i)
        if (r->lay->stretch(i) == 1) teamI = i;
    if (teamI >= 0) {
        r->lay->setStretch(teamI, 0);
        r->lay->insertWidget(teamI + 1, new Text(T(QStringLiteral("+{places} places"), {{"places", QString::number(m)}}), t, 11, true, t.error), 1);
    }
    const qint64 pos = py::toInt(row.value(QStringLiteral("pos"))).value_or(0);
    r->setToolTip(T(QStringLiteral("Qualified P{qpos}, starts P{pos} ({change} places)"),
                    {{"qpos", QString::number(pos - m)}, {"pos", s(row, "pos")}, {"change", QStringLiteral("+%1").arg(m)}}));
}

void Tab::gridView(QJsonArray official, const QJsonArray& provisional, const QString& officialNote) {
    // Places moved: official position minus qualifying position, when the source did not say.
    QHash<QString, qint64> qualified;
    for (const QJsonValue& v : provisional)
        if (const auto p = py::toInt(v.toObject().value(QStringLiteral("pos")))) qualified.insert(s(v.toObject(), "code"), *p);
    for (qsizetype i = 0; i < official.size(); ++i) {
        QJsonObject row = official[i].toObject();
        if (row.contains(QStringLiteral("moved"))) continue;
        const auto pos = py::toInt(row.value(QStringLiteral("pos")));
        if (pos && qualified.contains(s(row, "code"))) row.insert(QStringLiteral("moved"), *pos - qualified.value(s(row, "code")));
        official[i] = row;
    }
    const bool hasOfficial = !official.isEmpty(), hasProvisional = !provisional.isEmpty();
    const bool showOfficial = hasOfficial && (ctx->settings->grid_official || !hasProvisional);
    if (hasOfficial && hasProvisional) {
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
                Q_EMIT rebuildRequested();
            });
            sw->addWidget(b);
        }
        sw->addStretch(1);
        lay->addLayout(sw);
    }
    if (showOfficial) {
        qint64 penalised = 0;
        for (const QJsonValue& r : official) penalised += py::toInt(r[QStringLiteral("moved")]).value_or(0) > 0 ? 1 : 0;
        const QString note = !hasProvisional ? QString()
                             : penalised == 1 ? T(QStringLiteral("1 driver starts further back than qualified."))
                             : penalised      ? T(QStringLiteral("{count} drivers start further back than qualified."),
                                                  {{"count", QString::number(penalised)}})
                                              : T(QStringLiteral("Same order as qualifying."));
        hint(note.isEmpty() ? officialNote : officialNote + u' ' + note);
        for (const QJsonValue& rv : official) penaltyNote(resultRow(rv.toObject(), 80), rv.toObject());
    } else {
        // Never present the qualifying order as the final grid.
        hint(T(QStringLiteral("Provisional: the qualifying order, before penalties.")));
        for (const QJsonValue& rv : provisional) resultRow(rv.toObject(), 80);
    }
}

Row* Tab::standingRow(const QJsonObject& row, const QString& primary, const QString& secondary) {
    Row* r = makeRow(row);
    if (Chip* c = ctx->chip(row)) r->cell(c);
    r->cell(posLabel(row.value(QStringLiteral("pos"))), 28);
    const QString color = ctx->isFavorite(row) ? t.accent : t.onSurface;
    if (!secondary.isEmpty()) {
        auto* prim = new Text(primary, t, 13, true, color);
        prim->setToolTip(s(row, "name"));
        r->cell(prim, 48);
        r->cell(new Text(secondary, t, 13, false, t.muted), -1, 1);
    } else {
        r->cell(new Text(primary, t, 13, true, color), -1, 1);
    }
    const qint64 wins = py::toInt(row.value(QStringLiteral("wins"))).value_or(0);
    auto* w = new Text(wins ? T(QStringLiteral("{wins}W"), {{"wins", QString::number(wins)}}) : QString(), t, 12, false, t.muted,
                       Qt::AlignRight);
    w->setToolTip(wins == 1 ? T(QStringLiteral("1 win")) : T(QStringLiteral("{wins} wins"), {{"wins", QString::number(wins)}}));
    r->cell(w, 30);
    const qint64 gap = py::toInt(row.value(QStringLiteral("gap"))).value_or(0);
    r->cell(new Text(gap > 0 ? T(QStringLiteral("-{gap}"), {{"gap", QString::number(gap)}}) : QString(), t, 12, false, t.muted,
                     Qt::AlignRight),
            40);
    r->cell(new Text(s(row, "pts"), t, 13, true, t.onSurface, Qt::AlignRight), 44);
    lay->addWidget(r);
    return r;
}

PendingTab::PendingTab(Ctx* c, const QString& name, QWidget* parent) : Tab(c, parent) {
    hint(QStringLiteral("The %1 tab is not in the C++ build yet. The Python app shows it today.").arg(name));
    finish();
}

}  // namespace f1::ui
