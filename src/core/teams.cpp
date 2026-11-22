#include "teams.h"

#include "i18n.h"
#include "sanitize.h"

#include <QHash>

namespace f1 {

QString teamColor(const QString& constructorId, const QString& fallback) {
    // Approximate 2026 constructor colours keyed by Jolpica constructorId.
    static const QHash<QString, QString> colors = {
        {QStringLiteral("mercedes"), QStringLiteral("#27F4D2")},
        {QStringLiteral("ferrari"), QStringLiteral("#E8002D")},
        {QStringLiteral("mclaren"), QStringLiteral("#FF8000")},
        {QStringLiteral("red_bull"), QStringLiteral("#3671C6")},
        {QStringLiteral("rb"), QStringLiteral("#6692FF")},
        {QStringLiteral("alpine"), QStringLiteral("#00A1E8")},
        {QStringLiteral("haas"), QStringLiteral("#B6BABD")},
        {QStringLiteral("audi"), QStringLiteral("#BB0A30")},
        {QStringLiteral("williams"), QStringLiteral("#64C4FF")},
        {QStringLiteral("aston_martin"), QStringLiteral("#229971")},
        {QStringLiteral("cadillac"), QStringLiteral("#C4A24A")},
        // historical / fallbacks
        {QStringLiteral("sauber"), QStringLiteral("#52E252")},
        {QStringLiteral("alphatauri"), QStringLiteral("#5E8FAA")},
        {QStringLiteral("alfa"), QStringLiteral("#C92D4B")},
        {QStringLiteral("renault"), QStringLiteral("#FFF500")},
    };
    return colors.value(constructorId, fallback);
}

QString friendlyFlag(const QString& flag) {
    if (flag.isEmpty()) return {};
    if (flag == u"GREEN") return T(QStringLiteral("Green flag"));
    if (flag == u"CLEAR") return T(QStringLiteral("Track clear"));
    if (flag == u"YELLOW") return T(QStringLiteral("Yellow flag"));
    if (flag == u"DOUBLE YELLOW") return T(QStringLiteral("Double yellow"));
    if (flag == u"RED") return T(QStringLiteral("Red flag"));
    if (flag == u"CHEQUERED") return T(QStringLiteral("Chequered flag"));
    if (flag == u"BLUE") return T(QStringLiteral("Blue flag"));
    if (flag == u"SAFETY CAR") return T(QStringLiteral("Safety car"));
    if (flag == u"VSC") return T(QStringLiteral("Virtual safety car"));
    if (flag == u"VSC ENDING") return QStringLiteral("VSC ending");
    return safe(flag, 24);
}

}  // namespace f1
