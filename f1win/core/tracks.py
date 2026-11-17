"""Circuit outlines from tracks.txt (bacinger/f1-circuits, MIT).

Same format and geometry as the original plugin: one circuit per line,
"<circuitId> x,y x,y ..." in a unit box, north up. Drawing parameters (8 %
padding, 2.2 % stroke, start/finish tick) match panel.luau.
"""
from __future__ import annotations

import math
import re
from pathlib import Path

_ID = re.compile(r"^[a-z0-9_]+$")

# Jolpica occasionally renames a circuit id; map known aliases to geometry ids.
ALIASES = {
    "las_vegas": "vegas",
    "madrid": "madring",
    "yas_island": "yas_marina",
    "lusail": "losail",
    "cota": "americas",
}


class TrackLibrary:
    def __init__(self, *files: Path) -> None:
        self._tracks: dict[str, list[tuple[float, float]]] = {}
        for f in files:
            self.load(f)

    def load(self, file: Path) -> int:
        try:
            text = Path(file).read_text(encoding="utf-8")
        except OSError:
            return 0
        n = 0
        for line in text.splitlines():
            if not line or line.startswith("#"):
                continue
            cid, _, rest = line.partition(" ")
            if not _ID.match(cid):
                continue
            pts = []
            for pair in rest.split():
                try:
                    x, y = pair.split(",")
                    pts.append((float(x), float(y)))
                except ValueError:
                    continue
            if len(pts) >= 2:
                self._tracks[cid] = pts
                n += 1
        return n

    def ids(self) -> list[str]:
        return sorted(self._tracks)

    def resolve(self, circuit_id: str) -> str | None:
        cid = (circuit_id or "").lower()
        if not _ID.match(cid):
            return None
        if cid in self._tracks:
            return cid
        alias = ALIASES.get(cid)
        return alias if alias in self._tracks else None

    def points(self, circuit_id: str) -> list[tuple[float, float]] | None:
        cid = self.resolve(circuit_id)
        return self._tracks.get(cid) if cid else None


def geometry(pts: list[tuple[float, float]]):
    """Bounding box with the original padding, stroke width and start tick."""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    w, h = max(maxx - minx, 0.001), max(maxy - miny, 0.001)
    span = max(w, h)
    pad = span * 0.08
    stroke = span * 0.022
    (ax, ay), (bx, by) = pts[0], pts[1]
    dx, dy = bx - ax, by - ay
    ln = math.hypot(dx, dy) or 1.0
    nx, ny = -dy / ln, dx / ln
    half = span * 0.035
    tick = ((ax - nx * half, ay - ny * half), (ax + nx * half, ay + ny * half))
    box = (minx - pad, miny - pad, w + 2 * pad, h + 2 * pad)
    return box, stroke, tick


def to_svg(pts: list[tuple[float, float]], color: str) -> str:
    (bx, by, bw, bh), stroke, ((x1, y1), (x2, y2)) = geometry(pts)
    coords = " ".join(f"{x:.3f},{y:.3f}" for x, y in pts)
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{bx:.3f} {by:.3f} {bw:.3f} {bh:.3f}" '
        f'width="200" height="200"><polyline points="{coords}" fill="none" stroke="{color}" '
        f'stroke-width="{stroke:.4f}" stroke-linejoin="round" stroke-linecap="round"/>'
        f'<line x1="{x1:.3f}" y1="{y1:.3f}" x2="{x2:.3f}" y2="{y2:.3f}" stroke="{color}" '
        f'stroke-width="{stroke * 1.8:.4f}"/></svg>'
    )
