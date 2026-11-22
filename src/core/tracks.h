#pragma once
// Circuit outlines from tracks.txt (bacinger/f1-circuits, MIT) (port of core/tracks.py).
//
// One circuit per line, "<circuitId> x,y x,y ..." in a unit box, north up. Drawing parameters
// (8 % padding, 2.2 % stroke, start/finish tick) match the original plugin.

#include <QHash>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

namespace f1 {

using Outline = QList<QPointF>;

class TrackLibrary {
public:
    int load(const QString& file);
    QStringList ids() const;
    QString resolve(const QString& circuitId) const;   // "" when unknown
    const Outline* points(const QString& circuitId) const;

private:
    QHash<QString, Outline> m_tracks;
};

struct TrackGeometry {
    QRectF box;            // bounding box with padding
    double stroke = 0;
    QPointF tickA, tickB;  // start/finish tick
};
TrackGeometry trackGeometry(const Outline& pts);

}  // namespace f1
