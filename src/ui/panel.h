#pragma once
// The F1 panel: header, title, pill tabs, tab body (port of ui/panel.py). One class serves the
// tray popup (and later the desktop widget); both read the same DataStore.

#include "tabs/common.h"

#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>

namespace f1::ui {

struct TabEntry {
    QString id, label;
    bool operator==(const TabEntry&) const = default;
};
QList<TabEntry> tabList(const DataStore* store, const Settings& s, qint64 now);
QString pickView(const DataStore* store, const Settings& s, qint64 now);
bool spoilerView(const QString& view);   // grid, live, results, drivers, constructors

class F1Panel : public QWidget {
    Q_OBJECT
public:
    F1Panel(Ctx* ctx, const QString& mode = QStringLiteral("popup"), QWidget* parent = nullptr);
    void setView(const QString& view, bool animate = true);
    void openFresh();
    void cycle(int step);
    void rebuildBody(bool resetScroll = false);
    void tick(qint64 now);
    void setPinned(bool on);
    void setUpdate(const QString& version);   // "" hides the footer link
    QStringList validViews(qint64 now) const;

    QString view = QStringLiteral("schedule");
    double bgOpacity = 1.0;
    bool pinned = false;
    QScrollArea* scroll = nullptr;
    Tab* tabWidget = nullptr;

Q_SIGNALS:
    void closeRequested();
    void settingsRequested();
    void refreshRequested();
    void pinToggled(bool on);
    void updateClicked();

protected:
    friend class DesktopWidget;   // forwards its key presses here
    void keyPressEvent(QKeyEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void buildChrome();
    void dataChanged(const QString& key);
    void updateHeader();
    void refreshTabbar(qint64 now);
    Tab* makeTab(const QString& view);
    bool spoilersHiddenNow(qint64 now) const;
    void reveal();

    Ctx* m_ctx;
    QString m_mode;
    QList<std::pair<TabEntry, bool>> m_tabsSig;
    Text* m_roundLabel = nullptr;
    Text* m_title = nullptr;
    Text* m_footer = nullptr;
    ClickText* m_updateLink = nullptr;
    QString m_updateVersion;
    IconButton* m_pinBtn = nullptr;
    QWidget* m_tabbarHost = nullptr;
    QHBoxLayout* m_tabbar = nullptr;
    QTimer m_rebuildTimer;
};

}  // namespace f1::ui
