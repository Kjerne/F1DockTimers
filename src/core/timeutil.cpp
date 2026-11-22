#include "timeutil.h"

#include "i18n.h"
#include "pyjson.h"

#include <QRegularExpression>
#include <cmath>

namespace f1 {

namespace {

const QRegularExpression& reDate() {
    static const QRegularExpression re(QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})"));
    return re;
}
const QRegularExpression& reTime() {
    static const QRegularExpression re(QStringLiteral("^(\\d{2}):(\\d{2}):?(\\d*)"));
    return re;
}
const QRegularExpression& reIso() {
    static const QRegularExpression re(QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})T(\\d{2}):(\\d{2}):(\\d{2})"));
    return re;
}
const QRegularExpression& reOffsetTail() {
    static const QRegularExpression re(QStringLiteral("([+-])(\\d{2}):(\\d{2})$"));
    return re;
}
const QRegularExpression& reGmt() {
    static const QRegularExpression re(QStringLiteral("^([+-]?)(\\d{2}):(\\d{2})"));
    return re;
}

// datetime(y, mo, d, hh, mm, ss, tzinfo=utc).timestamp(); nullopt where Python raises ValueError.
std::optional<qint64> epoch(int y, int mo, int d, int hh = 0, int mm = 0, int ss = 0) {
    const QDate date(y, mo, d);
    if (y < 1 || !date.isValid() || !QTime::isValid(hh, mm, ss)) return std::nullopt;
    return QDateTime(date, QTime(hh, mm, ss), QTimeZone::UTC).toSecsSinceEpoch();
}

QString textOf(const QJsonValue& v) { return py::truthy(v) ? py::str(v) : QString(); }

int cap(const QRegularExpressionMatch& m, int i) { return m.captured(i).toInt(); }

}  // namespace

qint64 nowSec() { return QDateTime::currentSecsSinceEpoch(); }

QString weekdayName(int isoDay) {
    static const char* names[] = {F1_TR_NOOP("Mon"), F1_TR_NOOP("Tue"), F1_TR_NOOP("Wed"), F1_TR_NOOP("Thu"), F1_TR_NOOP("Fri"), F1_TR_NOOP("Sat"), F1_TR_NOOP("Sun")};
    return T(QString::fromLatin1(names[(isoDay - 1 + 7) % 7]));
}

QString monthName(int month) {
    static const char* names[] = {F1_TR_NOOP("Jan"), F1_TR_NOOP("Feb"), F1_TR_NOOP("Mar"), F1_TR_NOOP("Apr"), F1_TR_NOOP("May"), F1_TR_NOOP("Jun"), F1_TR_NOOP("Jul"), F1_TR_NOOP("Aug"), F1_TR_NOOP("Sep"), F1_TR_NOOP("Oct"), F1_TR_NOOP("Nov"), F1_TR_NOOP("Dec")};
    return T(QString::fromLatin1(names[(month - 1 + 12) % 12]));
}

std::optional<qint64> parseUtc(const QJsonValue& date, const QJsonValue& time) {
    const auto m = reDate().match(textOf(date));
    if (!m.hasMatch()) return std::nullopt;
    const QString ts = py::truthy(time) ? py::str(time) : QStringLiteral("00:00:00Z");
    const auto t = reTime().match(ts);
    int hh = 0, mm = 0, ss = 0;
    if (t.hasMatch()) {
        hh = cap(t, 1);
        mm = cap(t, 2);
        const QString s = t.captured(3);
        if (!s.isEmpty()) {
            bool ok = false;
            ss = s.toInt(&ok);
            if (!ok) return std::nullopt;
        }
    }
    return epoch(cap(m, 1), cap(m, 2), cap(m, 3), hh, mm, ss);
}

std::optional<qint64> parseIso(const QJsonValue& text) {
    const QString s = textOf(text);
    const auto m = reIso().match(s);
    if (!m.hasMatch()) return std::nullopt;
    auto t = epoch(cap(m, 1), cap(m, 2), cap(m, 3), cap(m, 4), cap(m, 5), cap(m, 6));
    if (!t) return std::nullopt;
    const auto off = reOffsetTail().match(s);
    if (off.hasMatch()) {
        const qint64 offset = cap(off, 2) * 3600LL + cap(off, 3) * 60LL;
        *t = off.captured(1) == u"+" ? *t - offset : *t + offset;
    }
    return t;
}

std::optional<qint64> parseLocal(const QJsonValue& dateTime, const QJsonValue& gmtOffset) {
    const auto m = reIso().match(textOf(dateTime));
    const auto o = reGmt().match(textOf(gmtOffset));
    if (!m.hasMatch() || !o.hasMatch()) return std::nullopt;
    qint64 offset = cap(o, 2) * 3600LL + cap(o, 3) * 60LL;
    if (o.captured(1) == u"-") offset = -offset;
    auto t = epoch(cap(m, 1), cap(m, 2), cap(m, 3), cap(m, 4), cap(m, 5), cap(m, 6));
    if (!t) return std::nullopt;
    return *t - offset;
}

namespace {
struct Dhm {
    qint64 d, h, m;
};
Dhm split(double seconds) {
    qint64 s = static_cast<qint64>(seconds);   // int() truncates
    const qint64 d = py::floordiv(s, 86400);
    s -= d * 86400;
    const qint64 h = py::floordiv(s, 3600);
    s -= h * 3600;
    return {d, h, py::floordiv(s, 60)};
}
}  // namespace

QString fmtShort(double seconds) {
    if (seconds <= 0) return T(QStringLiteral("live"));
    const Dhm x = split(seconds);
    if (x.d > 0) return T(QStringLiteral("{days}d"), {{"days", QString::number(x.d)}});
    if (x.h > 0) return T(QStringLiteral("{hours}h"), {{"hours", QString::number(x.h)}});
    if (x.m > 0) return T(QStringLiteral("{minutes}m"), {{"minutes", QString::number(x.m)}});
    return T(QStringLiteral("<1m"));
}

QString fmtLong(double seconds) {
    if (seconds <= 0) return T(QStringLiteral("live now"));
    const Dhm x = split(seconds);
    QStringList parts;
    if (x.d > 0) parts << T(QStringLiteral("{days}d"), {{"days", QString::number(x.d)}});
    if (x.h > 0) parts << T(QStringLiteral("{hours}h"), {{"hours", QString::number(x.h)}});
    parts << T(QStringLiteral("{minutes}m"), {{"minutes", QString::number(x.m)}});
    return T(QStringLiteral("in {time}"), {{"time", parts.join(u' ')}});
}

QString fmtClock(double seconds) {
    const qint64 s = std::max<qint64>(0, static_cast<qint64>(seconds));
    if (s >= 3600)
        return QString::asprintf("%lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60);
    return QString::asprintf("%02lld:%02lld", s / 60, s % 60);
}

QString systemZoneName() {
    const QByteArray id = QTimeZone::systemTimeZoneId();
    return id.isEmpty() ? QStringLiteral("UTC") : QString::fromUtf8(id);
}

Zone zoneFor(const QString& name) {
    if (name.isEmpty() || name == u"auto") return std::nullopt;
    QTimeZone z(name.toUtf8());
    if (!z.isValid()) return std::nullopt;
    return z;
}

QDateTime toLocal(qint64 ts, const Zone& zone) {
    if (!zone) return QDateTime::fromSecsSinceEpoch(ts, QTimeZone(QTimeZone::LocalTime));
    return QDateTime::fromSecsSinceEpoch(ts, *zone);
}

namespace {
QString clock(const QDateTime& dt, bool twelve, bool padHour) {
    const int h = dt.time().hour(), m = dt.time().minute();
    if (twelve) {
        const int h12 = h % 12 == 0 ? 12 : h % 12;
        return (padHour ? QString::asprintf("%02d:%02d", h12, m) : QString::asprintf("%d:%02d", h12, m)) +
               (h < 12 ? QStringLiteral(" AM") : QStringLiteral(" PM"));
    }
    return QString::asprintf("%02d:%02d", h, m);
}
}  // namespace

QString fmtDatetime(qint64 ts, const Zone& zone, bool twelve, bool longForm) {
    const QDateTime dt = toLocal(ts, zone);
    const QString wd = weekdayName(dt.date().dayOfWeek());
    const QString c = clock(dt, twelve, true);
    if (longForm)
        return QStringLiteral("%1 %2 %3, %4").arg(wd, QString::asprintf("%02d", dt.date().day()),
                                                  monthName(dt.date().month()), c);
    return wd + u' ' + c;
}

QString fmtClockTime(qint64 ts, const Zone& zone, bool twelve) { return clock(toLocal(ts, zone), twelve, false); }

QString fmtDate(qint64 ts, const Zone& zone) {
    const QDate d = toLocal(ts, zone).date();
    return QStringLiteral("%1 %2 %3 %4")
        .arg(weekdayName(d.dayOfWeek()))
        .arg(d.day())
        .arg(monthName(d.month()))
        .arg(d.year());
}

QString fmtStamp(double ts, const Zone& zone, bool twelve) {
    if (ts == 0) return T(QStringLiteral("never"));
    const QDateTime dt = toLocal(static_cast<qint64>(ts), zone);
    const QDateTime now = toLocal(nowSec(), zone);
    const QString c = clock(dt, twelve, false);
    if (dt.date() == now.date()) return c;
    return QStringLiteral("%1 %2 %3").arg(dt.date().day()).arg(monthName(dt.date().month()), c);
}

QString utcOffsetLabel(qint64 ts, const Zone& zone) {
    const QDateTime dt = toLocal(ts, zone);
    int mins = static_cast<int>(py::floordiv(dt.offsetFromUtc(), 60));
    const QChar sign = mins >= 0 ? u'+' : u'-';
    mins = std::abs(mins);
    return QStringLiteral("UTC") + sign + QString::asprintf("%02d:%02d", mins / 60, mins % 60);
}

}  // namespace f1
