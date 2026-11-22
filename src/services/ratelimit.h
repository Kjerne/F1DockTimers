#pragma once
// Per-host pacing (port of RateLimiter in services/http.py): a minimum gap between request
// starts, a rolling one-minute cap and Retry-After blocks. Pure.
//
// Jolpica: 4/s burst, 500/h. OpenF1: Free 3/s and 30/min, Sponsor 6/s and 60/min.

#include <QHash>
#include <algorithm>
#include <QString>
#include <deque>
#include <utility>

namespace f1 {

struct Limit {
    double gap;    // minimum seconds between request starts
    int perMin;    // max requests per rolling minute
};

inline Limit openf1Plan(const QString& plan) { return plan == u"sponsor" ? Limit{0.18, 57} : Limit{0.36, 28}; }

inline QHash<QString, Limit> defaultLimits() {
    return {{QStringLiteral("api.jolpi.ca"), {0.35, 100}},
            {QStringLiteral("api.openf1.org"), {0.36, 28}},
            {QStringLiteral("api.open-meteo.com"), {1.0, 10}}};
}

class RateLimiter {
public:
    explicit RateLimiter(QHash<QString, Limit> limits = defaultLimits()) : limits(std::move(limits)) {}

    double wait(const QString& host, double now) {
        const Limit l = limits.value(host, Limit{0.2, 120});
        auto& q = starts[host];
        while (!q.empty() && now - q.front() >= 60.0) q.pop_front();
        double w = std::max(0.0, blockedUntil.value(host, 0.0) - now);
        if (!q.empty()) w = std::max(w, q.back() + l.gap - now);
        if (static_cast<int>(q.size()) >= l.perMin) w = std::max(w, q.front() + 60.0 - now + 0.05);
        return w;
    }
    void note(const QString& host, double now) { starts[host].push_back(now); }
    void block(const QString& host, double until) { blockedUntil[host] = std::max(blockedUntil.value(host, 0.0), until); }

    QHash<QString, Limit> limits;

private:
    QHash<QString, std::deque<double>> starts;
    QHash<QString, double> blockedUntil;
};

}  // namespace f1
