#include "sanitize.h"

#include "pyjson.h"

#include <QList>
#include <QRegularExpression>

namespace f1 {

namespace {

bool stripped(char32_t c) {
    // C0/C1 controls, DEL, Arabic letter mark, zero-width and bidi controls, BOM.
    return c <= 0x1F || (c >= 0x7F && c <= 0x9F) || c == 0x061C || (c >= 0x200B && c <= 0x200F) ||
           (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2064) || (c >= 0x2066 && c <= 0x206F) ||
           c == 0xFEFF;
}

}  // namespace

QString safe(const QString& value, int limit) {
    QList<uint> cps = value.toUcs4();
    for (uint& c : cps)
        if (stripped(c)) c = 0x20;
    QString text = QString::fromUcs4(reinterpret_cast<const char32_t*>(cps.constData()), cps.size()).trimmed();
    cps = text.toUcs4();
    if (cps.size() > limit) {
        text = QString::fromUcs4(reinterpret_cast<const char32_t*>(cps.constData()), limit) + QChar(0x2026);
    }
    return text;
}

QString safe(const QJsonValue& value, int limit) {
    if (value.isNull() || value.isUndefined()) return safe(QString(), limit);
    return safe(py::str(value), limit);
}

std::optional<QString> safeLink(const QJsonValue& url) {
    QString raw = py::truthy(url) ? py::str(url) : QString();
    if (raw.size() > 400) return std::nullopt;
    for (QChar c : raw)
        if (c.unicode() <= 0x20) return std::nullopt;
    if (raw.startsWith(u"http://")) raw = QStringLiteral("https://") + raw.mid(7);
    if (!raw.startsWith(u"https://")) return std::nullopt;
    // urlsplit netloc: from after "//" to the first of / ? #
    const QString rest = raw.mid(8);
    qsizetype end = rest.size();
    for (QChar stop : {QChar(u'/'), QChar(u'?'), QChar(u'#')}) {
        const qsizetype i = rest.indexOf(stop);
        if (i >= 0 && i < end) end = i;
    }
    const QString host = rest.left(end);
    if (host.isEmpty() || host.contains(u'@') || host.contains(u':')) return std::nullopt;
    const QString h = host.toLower();
    if (h != u"en.wikipedia.org" && h != u"www.wikipedia.org" && h != u"wikipedia.org") return std::nullopt;
    return raw;
}

std::optional<QString> hexColor(const QJsonValue& raw) {
    QString h = py::truthy(raw) ? py::str(raw) : QString();
    h = h.trimmed();
    while (h.startsWith(u'#')) h.remove(0, 1);
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{6}$"));
    if (re.match(h).hasMatch()) return QStringLiteral("#") + h.toUpper();
    return std::nullopt;
}

}  // namespace f1
