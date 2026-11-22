#include "pyjson.h"

#include <QJsonDocument>
#include <cmath>
#include <limits>

namespace f1::py {

bool truthy(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Bool: return v.toBool();
    case QJsonValue::Double: return v.toDouble() != 0.0;
    case QJsonValue::String: return !v.toString().isEmpty();
    case QJsonValue::Array: return !v.toArray().isEmpty();
    case QJsonValue::Object: return !v.toObject().isEmpty();
    default: return false;
    }
}

static QString numberText(double d) {
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e16)
        return QString::number(static_cast<qint64>(d));
    if (std::isnan(d)) return QStringLiteral("nan");
    if (std::isinf(d)) return d > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
    return QString::number(d, 'g', QLocale::FloatingPointShortest);
}

QString str(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::String: return v.toString();
    case QJsonValue::Double: return numberText(v.toDouble());
    case QJsonValue::Bool: return v.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    default: return QStringLiteral("None");
    }
}

std::optional<double> toFloat(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Double: return v.toDouble();
    case QJsonValue::Bool: return v.toBool() ? 1.0 : 0.0;
    case QJsonValue::String: {
        const QString s = v.toString().trimmed();
        if (s.isEmpty()) return std::nullopt;
        bool ok = false;
        double d = s.toDouble(&ok);
        if (ok) return d;
        const QString l = s.toLower();
        if (l == u"nan" || l == u"+nan" || l == u"-nan") return std::numeric_limits<double>::quiet_NaN();
        if (l == u"inf" || l == u"+inf" || l == u"infinity" || l == u"+infinity")
            return std::numeric_limits<double>::infinity();
        if (l == u"-inf" || l == u"-infinity") return -std::numeric_limits<double>::infinity();
        return std::nullopt;
    }
    default: return std::nullopt;
    }
}

std::optional<qint64> toInt(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (!std::isfinite(d)) return std::nullopt;
        return static_cast<qint64>(std::trunc(d));
    }
    case QJsonValue::Bool: return v.toBool() ? 1 : 0;
    case QJsonValue::String: {
        const QString s = v.toString().trimmed();
        if (s.isEmpty()) return std::nullopt;
        for (qsizetype i = 0; i < s.size(); ++i) {
            const QChar c = s.at(i);
            if (!(c.isDigit() || (i == 0 && (c == u'+' || c == u'-')))) return std::nullopt;
        }
        bool ok = false;
        qint64 n = s.toLongLong(&ok);
        return ok ? std::optional<qint64>(n) : std::nullopt;
    }
    default: return std::nullopt;
    }
}

QJsonValue path(const QJsonValue& v, std::initializer_list<QString> keys) {
    QJsonValue cur = v;
    for (const QString& k : keys) {
        if (!cur.isObject()) return QJsonValue(QJsonValue::Undefined);
        cur = cur.toObject().value(k);
    }
    return cur;
}

QJsonArray bounded(const QJsonValue& v, int cap) {
    if (!v.isArray()) return {};
    QJsonArray a = v.toArray();
    while (a.size() > cap) a.removeLast();
    return a;
}

QString fmtG(double n) { return QString::asprintf("%g", n); }

bool isDigits(const QString& s) {
    if (s.isEmpty()) return false;
    for (QChar c : s)
        if (!c.isDigit()) return false;
    return true;
}

}  // namespace f1::py
