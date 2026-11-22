#pragma once
// Remote-data hygiene (port of f1win/core/sanitize.py).

#include <QJsonValue>
#include <QString>
#include <optional>

namespace f1 {

constexpr int MAX_FIELD_CHARS = 72;
constexpr qint64 MAX_BODY_BYTES = 1'048'576;

// Clamp a remote value to printable text of at most `limit` characters (code points).
QString safe(const QJsonValue& value, int limit = MAX_FIELD_CHARS);
QString safe(const QString& value, int limit = MAX_FIELD_CHARS);

// Wikipedia links only, https (http upgraded), no userinfo or port.
std::optional<QString> safeLink(const QJsonValue& url);

// OpenF1 / F1 feed bare hex "27F4D2" -> "#27F4D2".
std::optional<QString> hexColor(const QJsonValue& raw);

}  // namespace f1
