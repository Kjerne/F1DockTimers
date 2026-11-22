// Port of reference-python/tests/test_taskbar_embed.py: embedded taskbar mode (a child window of
// Shell_TrayWnd), the fallback to floating, docking beside other widgets, the Widgets-button
// lookup and the memory trim. Uses the real Windows desktop, so it runs on the "windows" platform.
#include "common.h"
#include "services/taskbar_embed.h"
#include "services/widgets_button.h"
#include "services/winshell.h"
#include "ui/taskbar_pill.h"

#include <windows.h>

#include <QApplication>
#include <QTemporaryDir>
#include <thread>

using namespace f1;
using namespace f1::ui;

namespace {
struct Rig {
    QTemporaryDir dir;
    DataStore store{dir.filePath(QStringLiteral("c.json")), QString()};
    Settings settings;
    Theme theme;
    Zone zone;
    std::unique_ptr<TaskbarPill> pill;
    explicit Rig(bool embed, int offset = 0) {
        settings.taskbar_pill = true;
        settings.pill_embed = embed;
        settings.taskbar_pill_offset = offset;
        pill = std::make_unique<TaskbarPill>(&store, &settings, &theme, &zone, [] { return QRect(); });
    }
};
QRect r(int l, int t, int rr, int b) { return QRect(QPoint(l, t), QPoint(rr - 1, b - 1)); }   // Python (l, t, r, b)
}  // namespace

class TestEmbed : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void embeddedModeLivesInTheTaskbarAndFallsBack() {
        if (!findTaskbar()) QSKIP("no taskbar in this session");
        Rig rig(true);
        TaskbarPill& pill = *rig.pill;
        pill.start();
        if (pill.embedFailed()) {   // e.g. a CI desktop whose taskbar refuses child windows
            QVERIFY(pill.isVisible());   // ...then the floating window takes over
            pill.stop();
            QSKIP("taskbar refused the child window");
        }
        QVERIFY(pill.embedded() && !pill.isVisible() && pill.isShown());
        const quintptr hwnd = pill.embedHost()->hwnd;
        QCOMPARE(reinterpret_cast<quintptr>(GetParent(reinterpret_cast<HWND>(hwnd))), pill.embedHost()->bar);
        QVERIFY(IsWindowVisible(reinterpret_cast<HWND>(hwnd)));
        // Explorer restart: our window disappears; the guard must create a new one.
        DestroyWindow(reinterpret_cast<HWND>(hwnd));
        QVERIFY(!pill.embedHost()->alive());
        pill.guard();
        QVERIFY(pill.embedded() && pill.embedHost()->alive() && pill.embedHost()->hwnd != hwnd);
        // Turning the option off goes back to the floating window.
        rig.settings.pill_embed = false;
        pill.restyle();
        QVERIFY(!pill.embedded() && pill.isVisible());
        rig.settings.pill_embed = true;
        pill.restyle();
        QVERIFY(pill.embedded() && !pill.isVisible());
        pill.stop();
        QVERIFY(!pill.embedded());
    }

    void floatingModeUnchanged() {
        Rig rig(false);
        rig.pill->start();
        QVERIFY(!rig.pill->embedded() && rig.pill->isVisible());
        rig.pill->stop();
    }

    void docksBesideOtherWidgets() {
        const QRect bar = r(0, 1050, 1920, 1080), tray = r(1624, 1050, 1920, 1080);
        const QRect battery = r(1503, 1050, 1622, 1080);   // packed against the tray
        const QRect other = r(1380, 1050, 1500, 1080);     // packed against the battery widget
        const QRect farAway = r(600, 1050, 700, 1080);         // somewhere in the middle: not ours to dodge
        QCOMPARE(winshell::withNeighbours(bar, tray, {}).left(), 1624);
        QCOMPARE(winshell::withNeighbours(bar, tray, {battery}).left(), 1503);
        QCOMPARE(winshell::withNeighbours(bar, tray, {farAway, other, battery}).left(), 1380);
        QCOMPARE(winshell::withNeighbours(bar, tray, {farAway}).left(), 1624);
        // vertical taskbar on the right: widgets stack above the tray
        QCOMPARE(winshell::withNeighbours(r(1860, 0, 1920, 1080), r(1860, 900, 1920, 1080), {r(1862, 840, 1918, 898)}).top(), 840);
    }

    // Two docking apps that both yield to each other swap places forever; we keep our spot.
    void neighbourOnFarSideDoesNotPushUs() {
        const QRect bar = r(0, 1050, 1920, 1080), tray = r(1624, 1050, 1920, 1080);
        const QRect me = r(1460, 1050, 1618, 1080);        // we are next to the tray
        const QRect battery = r(1340, 1050, 1458, 1080);   // BatteryHub docked outside us
        QCOMPARE(winshell::withNeighbours(bar, tray, {battery}, me).left(), 1624);
        // The same widget on the tray side still pushes us out, also when it overlaps us.
        QCOMPARE(winshell::withNeighbours(bar, tray, {r(1503, 1050, 1622, 1080)}, me).left(), 1503);
        // wider than us, overlapping: the tray-facing edge decides, not the centre
        QCOMPARE(winshell::withNeighbours(bar, tray, {r(1400, 1050, 1622, 1080)}, r(1500, 1050, 1618, 1080)).left(), 1400);
        // vertical bar: the far side is above us
        QCOMPARE(winshell::withNeighbours(r(1860, 0, 1920, 1080), r(1860, 900, 1920, 1080), {r(1862, 790, 1918, 840)},
                                          r(1862, 850, 1918, 896))
                     .top(),
                 900);
    }

    void dockingWaitsBeforeMovingTowardTheTray() {
        Rig rig(false);
        const TaskbarPill& pill = *rig.pill;
        const QRect bar = r(0, 1050, 1920, 1080);
        double t = 100;
        auto at = [&](int left) { return pill.steadyAnchor(bar, r(left, 1050, 1920, 1080), t).left(); };
        QCOMPARE(at(1340), 1340);   // first: taken as is
        QCOMPARE(at(1200), 1200);   // pushed out: at once
        QCOMPARE(at(1624), 1200);   // a gap opened: hold
        t += TaskbarPill::DOCK_SETTLE_S - 1;
        QCOMPARE(at(1624), 1200);
        t += 2;
        QCOMPARE(at(1624), 1624);   // still open: move in
        at(1500);                   // a neighbour came back within the wait: that resets it
        QCOMPARE(at(1624), 1500);
        QCOMPARE(at(1500), 1500);
        t += TaskbarPill::DOCK_SETTLE_S + 1;
        QCOMPARE(at(1624), 1500);   // new gap, new wait
        // taskbar moved or resized: no waiting
        QCOMPARE(pill.steadyAnchor(r(0, 0, 1920, 30), r(1624, 0, 1920, 30), t).left(), 1624);
    }

    void neighbourScanIsSafeHere() {
        const auto rects = winshell::taskbarRects(r(0, 0, 10, 10));
        QVERIFY(!rects || rects->bar.isValid());
    }

    void widgetsButtonLookupIsSafeHere() {
        // The UI Automation lookup runs off the UI thread and never throws; nullopt when there is no button.
        WidgetsButtonWatch w;
        w.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        const auto rect = w.rect();
        w.stop();
        QVERIFY(!rect || rect->width() > 0);
    }

    void memoryTrimFreesWorkingSet() {
        QByteArray junk(30 * 1024 * 1024, '\0');   // touch 30 MB so there is something to give back
        for (qsizetype i = 0; i < junk.size(); i += 4096) junk[i] = 'x';
        const double before = winshell::workingSetMb();
        QVERIFY(winshell::trimWorkingSet());
        QVERIFY(winshell::workingSetMb() < before);
    }

    void negativePositionOnlyWithoutDocking() {
        // Docking plus a negative position would slide onto the neighbour, which then moves away...
        Rig rig(false, -130);
        rig.settings.pill_auto_dock = true;
        QCOMPARE(rig.pill->effectiveOffset(), 0);
        rig.settings.pill_auto_dock = false;
        QCOMPARE(rig.pill->effectiveOffset(), -130);
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "windows");   // the real desktop, not the offscreen test platform
    QApplication app(argc, argv);
    TestEmbed t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_embed.moc"
