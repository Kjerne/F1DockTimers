#pragma once
// Schedule tab: circuit, start lights, countdown, session times (port of ui/tabs/schedule.py).

#include "common.h"

#include <QElapsedTimer>
#include <QTimer>

namespace f1::ui {

inline constexpr int LIGHT_STEP_MS = 620;
inline constexpr int LIGHT_HOLD_MS = 1100;
inline constexpr int INTRO_FADE_MS = 280;

class ScheduleTab : public Tab {
    Q_OBJECT
public:
    explicit ScheduleTab(Ctx* ctx, QWidget* parent = nullptr);
    void tick(qint64 now) override;
    void onShown() override;

private:
    bool raceUnderway(qint64 now) const;
    void animStep();

    struct SessionRow {
        Session s;
        Text* name;
        Text* when;
    };
    QList<SessionRow> m_rows;
    LightsGantry* m_gantry = nullptr;
    TrackView* m_track = nullptr;
    Text* m_cdLabel = nullptr;
    Text* m_countdown = nullptr;
    QTimer m_animTimer;
    QElapsedTimer m_anim;
    bool m_animating = false;
};

}  // namespace f1::ui
