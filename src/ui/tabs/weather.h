#pragma once
// Weather tab: live track conditions, the outlook hour by hour, and every session's forecast
// (port of ui/tabs/weather.py). Every table row uses the same five columns:
// [name or hour] [start] [sky ........] [temp] [rain].

#include "../../core/weather.h"
#include "common.h"

namespace f1::ui {

inline constexpr int TEMP_W = 60, RAIN_W = 44;

// {session id: measured summary} for this round (OpenF1 /weather after each session).
QJsonObject measuredFor(const DataStore* store, const Race* race);
// The forecast if it belongs to this race weekend, else null.
const weather::Forecast* forecastFor(const DataStore* store, const Race* race);
// Add the SKY / TEMP / RAIN cells for session `s` to row `r`; returns the hover text. Finished
// sessions show what F1 measured at the track; others show the forecast.
QString sessionCells(Tab* tab, Row* r, const Session& s, const weather::Forecast* fc, const QJsonObject& measured,
                     qint64 now, int size = 13);

class WeatherTab : public Tab {
    Q_OBJECT
public:
    explicit WeatherTab(Ctx* ctx, QWidget* parent = nullptr);

private:
    Row* row(QWidget* first, QWidget* second = nullptr);
    void values(Row* r, const QString& sky, const QString& temp, qint64 chance, const QString& color, int size,
                bool boldRain = false);
    void columns(const QString& first, const QString& second, const QString& tip);
    void live(const Race* race, qint64 now);
    void outlook(const weather::Forecast* fc, const Session& s, qint64 now);
    int m_nameW, m_startW = 92;
};

}  // namespace f1::ui
