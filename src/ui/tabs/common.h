#pragma once
// Shared context and the tab base class (port of ui/tabs/common.py).

#include "../../core/timeutil.h"
#include "../../core/tracks.h"
#include "../../services/store.h"
#include "../../settings.h"
#include "../theme.h"
#include "../widgets.h"

#include <QVBoxLayout>
#include <QWidget>
#include <functional>

namespace f1::ui {

struct Ctx {
    DataStore* store;
    Settings* settings;
    const Theme* theme;
    const TrackLibrary* tracks;
    std::function<void(const QString&)> openLink;
    bool compact = false;
    std::function<void()> saveSettings = [] {};   // writes settings.json (the app wires it)
    QHash<QString, QString> picks;                // session picked in a tab's pills ("live", "last"); not saved

    Zone zone() const { return zoneFor(settings->timezone); }
    bool twelve() const { return settings->time_format == u"12h"; }
    bool isFavorite(const QJsonObject& row) const;
    // The team colour bar, or null when team colours are off.
    Chip* chip(const QJsonObject& row, const QString& explicitColor = {}) const;
};

// "Couldn't reach F1 data" / `noneText` / "Loading…" for an empty dataset.
QString emptyMessage(const QJsonObject& dataset, const QString& noneText);

// A tab body. The constructor builds the widgets; tick() moves countdowns cheaply.
class Tab : public QWidget {
    Q_OBJECT
public:
    explicit Tab(Ctx* ctx, QWidget* parent = nullptr);
    virtual void tick(qint64) {}
    virtual void onShown() {}

Q_SIGNALS:
    void rebuildRequested();   // e.g. another session was picked: the panel builds the tab again

public:

    Ctx* ctx;
    const Theme& t;
    QVBoxLayout* lay;

protected:
    Text* header(const QString& text);
    Wrap* hint(const QString& text, const QString& color = {});
    void finish() { lay->addStretch(1); }
    Text* posLabel(const QJsonValue& pos);
    Row* makeRow(const QJsonObject& row);
    // [chip] pos  code  team  detail  [points]
    Row* resultRow(const QJsonObject& row, int detailWidth, bool points = false);
    // [chip] pos  primary  secondary  wins  gap  points
    Row* standingRow(const QJsonObject& row, const QString& primary, const QString& secondary);
    // A row of small pills, one per session ({id, label}); picking one stores ctx->picks[pickKey]
    // and rebuilds the tab.
    void sessionPills(const QList<std::pair<QString, QString>>& items, const QString& current, const QString& pickKey);
    // An OpenF1 classification: [chip] pos  code  team  time or gap  gap to pole (qualifying) or points.
    void classificationRows(const QJsonArray& rows);
    // A starting grid with the Grid tab's Official / Provisional switch (the grid_official setting).
    // `official` rows may carry "moved" (places lost to penalties); without it, it is worked out from
    // `provisional`, the qualifying order. Either list may be empty.
    void gridView(QJsonArray official, const QJsonArray& provisional, const QString& officialNote);
    // "+5 places" in red after the team, and the qualified/starts tooltip, for a penalised driver.
    void penaltyNote(Row* r, const QJsonObject& row);
};

// A tab that is not ported yet (phase 3): says so instead of showing nothing.
class PendingTab : public Tab {
    Q_OBJECT
public:
    PendingTab(Ctx* ctx, const QString& name, QWidget* parent = nullptr);
};

}  // namespace f1::ui
