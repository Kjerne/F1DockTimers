#pragma once
// Live tab: running order, flag and session clock (port of ui/tabs/live.py). Outside a session it
// explains why nothing is live and shows the last session's final classification (OpenF1).

#include "common.h"

namespace f1::ui {

// The message for a live error code ("auth", "login", "token", "rate", "net", "free", "stream").
QString liveMessage(const QString& code);

class LiveTab : public Tab {
    Q_OBJECT
public:
    explicit LiveTab(Ctx* ctx, QWidget* parent = nullptr);
    void tick(qint64 now) override;

private:
    void lastSession();
    Text* m_clockLabel = nullptr;
    QJsonObject m_clock;
};

}  // namespace f1::ui
