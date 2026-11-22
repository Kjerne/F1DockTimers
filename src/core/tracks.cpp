#include "tracks.h"

#include <QFile>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace f1 {

namespace {
const QRegularExpression& idRe() {
    static const QRegularExpression re(QStringLiteral("^[a-z0-9_]+$"));
    return re;
}
// Jolpica occasionally renames a circuit id; map known aliases to geometry ids.
QString alias(const QString& id) {
    static const QHash<QString, QString> a = {{QStringLiteral("las_vegas"), QStringLiteral("vegas")},
                                              {QStringLiteral("madrid"), QStringLiteral("madring")},
                                              {QStringLiteral("yas_island"), QStringLiteral("yas_marina")},
                                              {QStringLiteral("lusail"), QStringLiteral("losail")},
                                              {QStringLiteral("cota"), QStringLiteral("americas")}};
    return a.value(id);
}
}  // namespace

int TrackLibrary::load(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    int n = 0;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(u'\n');
    for (QString line : lines) {
        while (line.endsWith(u'\r')) line.chop(1);
        if (line.isEmpty() || line.startsWith(u'#')) continue;
        const qsizetype sp = line.indexOf(u' ');
        const QString cid = sp < 0 ? line : line.left(sp);
        if (!idRe().match(cid).hasMatch()) continue;
        Outline pts;
        const QStringList pairs = sp < 0 ? QStringList() : line.mid(sp + 1).split(u' ', Qt::SkipEmptyParts);
        for (const QString& pair : pairs) {
            const QStringList xy = pair.split(u',');
            bool okx = false, oky = false;
            if (xy.size() != 2) continue;
            const double x = xy[0].toDouble(&okx), y = xy[1].toDouble(&oky);
            if (okx && oky) pts.append(QPointF(x, y));
        }
        if (pts.size() >= 2) {
            m_tracks.insert(cid, pts);
            ++n;
        }
    }
    return n;
}

QStringList TrackLibrary::ids() const {
    QStringList k = m_tracks.keys();
    k.sort();
    return k;
}

QString TrackLibrary::resolve(const QString& circuitId) const {
    const QString cid = circuitId.toLower();
    if (!idRe().match(cid).hasMatch()) return {};
    if (m_tracks.contains(cid)) return cid;
    const QString a = alias(cid);
    return m_tracks.contains(a) ? a : QString();
}

const Outline* TrackLibrary::points(const QString& circuitId) const {
    const QString cid = resolve(circuitId);
    auto it = m_tracks.constFind(cid);
    return (cid.isEmpty() || it == m_tracks.constEnd()) ? nullptr : &*it;
}

TrackGeometry trackGeometry(const Outline& pts) {
    double minx = pts[0].x(), maxx = minx, miny = pts[0].y(), maxy = miny;
    for (const QPointF& p : pts) {
        minx = std::min(minx, p.x());
        maxx = std::max(maxx, p.x());
        miny = std::min(miny, p.y());
        maxy = std::max(maxy, p.y());
    }
    const double w = std::max(maxx - minx, 0.001), h = std::max(maxy - miny, 0.001);
    const double span = std::max(w, h), pad = span * 0.08;
    TrackGeometry g;
    g.stroke = span * 0.022;
    const QPointF a = pts[0], b = pts[1];
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    double ln = std::hypot(dx, dy);
    if (ln == 0) ln = 1.0;
    const double nx = -dy / ln, ny = dx / ln, half = span * 0.035;
    g.tickA = QPointF(a.x() - nx * half, a.y() - ny * half);
    g.tickB = QPointF(a.x() + nx * half, a.y() + ny * half);
    g.box = QRectF(minx - pad, miny - pad, w + 2 * pad, h + 2 * pad);
    return g;
}

}  // namespace f1
