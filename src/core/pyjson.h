#pragma once
// Small helpers that give QJsonValue the same loose semantics the Python code relies on
// (truthiness, str(), float(), int(), dict.get on anything). Pure, Qt Core only.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <cmath>
#include <initializer_list>
#include <optional>

namespace f1::py {

// Python truthiness: None/False/0/""/[]/{} are false.
bool truthy(const QJsonValue& v);

// `a or b`
inline QJsonValue orv(const QJsonValue& a, const QJsonValue& b) { return truthy(a) ? a : b; }

// str(v): strings as-is, numbers without a trailing ".0" when integral, "True"/"False", "None".
QString str(const QJsonValue& v);

// float(v): numbers, numeric strings, bools. nullopt where Python raises.
std::optional<double> toFloat(const QJsonValue& v);

// int(v): ints, integral strings, floats (truncated), bools. nullopt where Python raises.
std::optional<qint64> toInt(const QJsonValue& v);

// `x if isinstance(x, dict) else {}` / `x if isinstance(x, list) else []`
inline QJsonObject obj(const QJsonValue& v) { return v.isObject() ? v.toObject() : QJsonObject(); }
inline QJsonArray arr(const QJsonValue& v) { return v.isArray() ? v.toArray() : QJsonArray(); }

// sanitize.path(): nested dict read, nullptr-ish (Undefined) when anything is missing.
QJsonValue path(const QJsonValue& v, std::initializer_list<QString> keys);

// sanitize.bounded(): the first `cap` items of a list, [] for anything else.
QJsonArray bounded(const QJsonValue& v, int cap);

// Python round(): half to even.
inline double round(double v) { return std::nearbyint(v); }

// Python `a // b` for integers (floors towards -infinity).
inline qint64 floordiv(qint64 a, qint64 b) {
    qint64 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// f"{n:g}"
QString fmtG(double n);

// Python str.isdigit() for the ASCII digits the APIs use.
bool isDigits(const QString& s);

}  // namespace f1::py
