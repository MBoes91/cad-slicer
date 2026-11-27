#include "slicer/Slicer.h"
#include "slicer/GcodeWriter.h"

#include <clipper.hpp>

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace fw {

namespace {

using ClipperLib::IntPoint;
using ClipperLib::Path;
using ClipperLib::Paths;

constexpr double kScale = 1000.0; // Clipper rechnet ganzzahlig in Mikrometern

Path toPath(const Polygon2& poly)
{
    Path p;
    p.reserve(poly.size());
    for (const auto& v : poly)
        p.emplace_back(std::llround(v.x * kScale), std::llround(v.y * kScale));
    return p;
}

Polygon2 toPoly(const Path& p)
{
    Polygon2 poly;
    poly.reserve(p.size());
    for (const auto& v : p) poly.push_back({v.X / kScale, v.Y / kScale});
    return poly;
}

Paths offset(const Paths& in, double deltaMm)
{
    Paths out;
    ClipperLib::ClipperOffset co(2.0, 0.25 * kScale / 100.0);
    co.AddPaths(in, ClipperLib::jtMiter, ClipperLib::etClosedPolygon);
    co.Execute(out, deltaMm * kScale);
    return out;
}

Paths boolean(const Paths& a, const Paths& b, ClipperLib::ClipType type)
{
    Paths out;
    ClipperLib::Clipper c;
    c.AddPaths(a, ClipperLib::ptSubject, true);
    c.AddPaths(b, ClipperLib::ptClip, true);
    c.Execute(type, out, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    return out;
}

// Parallele Fuellinien im Abstand spacing unter dem Winkel angleDeg,
// beschnitten auf die Region. Das Linienraster ist global ausgerichtet,
// damit duenne Fuellung ueber die Schichten fluchtet.
std::vector<Polygon2> lineFill(const Paths& region, double spacing, double angleDeg)
{
    std::vector<Polygon2> result;
    if (region.empty() || spacing <= 0) return result;

    const double a = angleDeg * M_PI / 180.0;
    const double ca = std::cos(a), sa = std::sin(a);

    // Region um -a drehen, dann waagrechte Linien erzeugen
    Paths rotated;
    ClipperLib::cInt minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool first = true;
    for (const auto& p : region) {
        Path rp;
        for (const auto& v : p) {
            const double x = v.X, y = v.Y;
            IntPoint q(std::llround(x * ca + y * sa), std::llround(-x * sa + y * ca));
            if (first) { minX = maxX = q.X; minY = maxY = q.Y; first = false; }
            minX = std::min(minX, q.X); maxX = std::max(maxX, q.X);
            minY = std::min(minY, q.Y); maxY = std::max(maxY, q.Y);
            rp.push_back(q);
        }
        rotated.push_back(rp);
    }

    const double step = spacing * kScale;
    Paths lines;
    for (double y = std::ceil(minY / step) * step; y <= maxY; y += step) {
        const auto yi = static_cast<ClipperLib::cInt>(std::llround(y));
        lines.push_back({IntPoint(minX - 10, yi), IntPoint(maxX + 10, yi)});
    }

    ClipperLib::Clipper c;
    c.AddPaths(lines, ClipperLib::ptSubject, false);
    c.AddPaths(rotated, ClipperLib::ptClip, true);
    ClipperLib::PolyTree tree;
    c.Execute(ClipperLib::ctIntersection, tree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    Paths open;
    ClipperLib::OpenPathsFromPolyTree(tree, open);

    for (const auto& p : open) {
        Polygon2 poly;
        for (const auto& v : p) {
            const double x = v.X, y = v.Y;
            poly.push_back({(x * ca - y * sa) / kScale, (x * sa + y * ca) / kScale});
        }
        if (poly.size() >= 2) result.push_back(std::move(poly));
    }
    return result;
}

double dist2(const Vec2& a, const Vec2& b)
{
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}

// Ordnet offene Linien gierig nach kuerzestem Leerweg (inkl. Umdrehen).
void orderLines(std::vector<Polygon2>& lines, Vec2 pos)
{
    std::vector<Polygon2> ordered;
    ordered.reserve(lines.size());
    std::vector<bool> used(lines.size(), false);
    for (size_t n = 0; n < lines.size(); ++n) {
        size_t best = 0; bool rev = false; double bestD = 1e300;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (used[i]) continue;
            const double d1 = dist2(pos, lines[i].front()), d2 = dist2(pos, lines[i].back());
            if (d1 < bestD) { bestD = d1; best = i; rev = false; }
            if (d2 < bestD) { bestD = d2; best = i; rev = true; }
        }
        used[best] = true;
        Polygon2 l = std::move(lines[best]);
        if (rev) std::reverse(l.begin(), l.end());
        pos = l.back();
        ordered.push_back(std::move(l));
    }
    lines = std::move(ordered);
}

// Dreht eine geschlossene Kontur so, dass sie am Punkt naechst pos beginnt.
void rotateLoopTo(Polygon2& loop, const Vec2& pos)
{
    size_t best = 0; double bestD = 1e300;
    for (size_t i = 0; i < loop.size(); ++i) {
        const double d = dist2(loop[i], pos);
        if (d < bestD) { bestD = d; best = i; }
    }
    std::rotate(loop.begin(), loop.begin() + static_cast<long>(best), loop.end());
}

// Startpunkt hinten (groesstes Y, bei Gleichstand kleinstes X)
void rotateLoopToRear(Polygon2& loop)
{
    size_t best = 0;
    for (size_t i = 1; i < loop.size(); ++i)
        if (loop[i].y > loop[best].y + 1e-6 || (std::abs(loop[i].y - loop[best].y) <= 1e-6 && loop[i].x < loop[best].x))
            best = i;
    std::rotate(loop.begin(), loop.begin() + static_cast<long>(best), loop.end());
}

Paths outerOnly(const Paths& in)
{
    Paths out;
    for (const auto& p : in) if (ClipperLib::Orientation(p)) out.push_back(p);
    return out;
}

void addLoops(std::vector<ToolPath>& out, const Paths& paths, PathType type)
{
    for (const auto& p : paths)
        if (p.size() >= 3) out.push_back({type, toPoly(p), true});
}

} // namespace

struct Cancelled {};

gp_Vec bedPlacement(const TopoDS_Shape& body, const PrinterProfile& pp)
{
    Bnd_Box box;
    BRepBndLib::Add(body, box);
    if (box.IsVoid()) return {};
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    return gp_Vec(pp.bedX / 2 - (x0 + x1) / 2, pp.bedY / 2 - (y0 + y1) / 2, -z0);
}

SliceResult slice(const TopoDS_Shape& bodyIn, const PrinterProfile& pp, const ProgressFn& progress,
                  const std::vector<PrintModifier>& modifiers)
{
    SliceResult res;
    auto report = [&](double f) {
        if (progress && !progress(std::min(1.0, std::max(0.0, f)))) throw Cancelled{};
    };
    if (bodyIn.IsNull()) { res.error = "Kein Koerper zum Slicen vorhanden"; return res; }

    try {
        // --- Teil auf dem Bett platzieren ------------------------------------
        Bnd_Box box;
        BRepBndLib::Add(bodyIn, box);
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        res.sizeX = x1 - x0; res.sizeY = y1 - y0; res.sizeZ = z1 - z0;
        const double margin = pp.brimWidth + (pp.skirtLoops > 0 ? pp.skirtDistance + pp.skirtLoops * pp.lineWidth : 0.0);
        if (res.sizeX + 2 * margin > pp.bedX || res.sizeY + 2 * margin > pp.bedY || res.sizeZ > pp.maxZ) {
            std::ostringstream s;
            s << std::fixed << std::setprecision(1) << "Teil (" << res.sizeX << " x " << res.sizeY << " x " << res.sizeZ
              << " mm) passt nicht in den Bauraum (" << pp.bedX << " x " << pp.bedY << " x " << pp.maxZ << " mm)";
            res.error = s.str();
            return res;
        }
        gp_Trsf move;
        const gp_Vec shift(pp.bedX / 2 - (x0 + x1) / 2, pp.bedY / 2 - (y0 + y1) / 2, -z0);
        move.SetTranslation(shift);

        // Grundriss eines Modifikators in Hoehe z (Bettkoordinaten); leer, wenn ausserhalb
        auto footprint = [&](const PrintModifier& m, double z) {
            Paths out;
            const double cz = m.cz + shift.Z();
            if (z < cz - m.sz / 2 || z > cz + m.sz / 2) return out;
            const double cx = m.cx + shift.X(), cy = m.cy + shift.Y();
            Polygon2 poly;
            if (m.shape == PrintModifier::Shape::Box) {
                poly = {{cx - m.sx / 2, cy - m.sy / 2}, {cx + m.sx / 2, cy - m.sy / 2},
                        {cx + m.sx / 2, cy + m.sy / 2}, {cx - m.sx / 2, cy + m.sy / 2}};
            } else {
                const int segs = 64;
                for (int k = 0; k < segs; ++k) {
                    const double a = 2 * M_PI * k / segs;
                    poly.push_back({cx + m.sx / 2 * std::cos(a), cy + m.sx / 2 * std::sin(a)});
                }
            }
            out.push_back(toPath(poly));
            return out;
        };
        auto roleFootprint = [&](PrintModifier::Role role, double z) {
            Paths all;
            for (const auto& m : modifiers)
                if (m.role == role) all = boolean(all, footprint(m, z), ClipperLib::ctUnion);
            return all;
        };
        const TopoDS_Shape body = BRepBuilderAPI_Transform(bodyIn, move, true).Shape();

        report(0.0);
        const std::vector<Triangle> tris = triangulate(body);
        if (tris.empty()) { res.error = "Triangulierung fehlgeschlagen"; return res; }

        // --- Schichtkonturen ----------------------------------------------------
        std::vector<double> tops;
        for (double z = pp.firstLayerHeight; z <= res.sizeZ + 1e-6; z += pp.layerHeight) tops.push_back(z);
        if (tops.empty()) { res.error = "Teil ist niedriger als eine Schicht"; return res; }

        const size_t n = tops.size();
        std::vector<Paths> region(n);
        for (size_t i = 0; i < n; ++i) {
            report(0.05 + 0.30 * static_cast<double>(i) / static_cast<double>(n));
            const double h = i == 0 ? pp.firstLayerHeight : pp.layerHeight;
            const double zMid = tops[i] - h / 2.0;
            Paths raw;
            for (const auto& loop : sliceAt(tris, zMid)) raw.push_back(toPath(loop));
            ClipperLib::Clipper c;
            c.AddPaths(raw, ClipperLib::ptSubject, true);
            c.Execute(ClipperLib::ctUnion, region[i], ClipperLib::pftEvenOdd, ClipperLib::pftEvenOdd);
            ClipperLib::CleanPolygons(region[i], 0.005 * kScale);
        }

        const double w = pp.lineWidth;
        std::vector<Paths> inner(n);
        for (size_t i = 0; i < n; ++i)
            inner[i] = offset(region[i], -(pp.perimeters * w - pp.infillOverlap * w));

        // --- Stuetzbereiche ----------------------------------------------------------
        std::vector<Paths> support(n);
        const auto mode = static_cast<SupportMode>(pp.supportMode);
        const bool hasEnforcer = std::any_of(modifiers.begin(), modifiers.end(),
            [](const PrintModifier& m) { return m.role == PrintModifier::Role::SupportEnforcer; });
        if ((mode != SupportMode::None || hasEnforcer) && n > 1) {
            const double allowed = pp.layerHeight * std::tan(pp.supportAngle * M_PI / 180.0);
            // Ueberhang je Schicht: was nicht von der Schicht darunter (plus erlaubtem Versatz) getragen wird
            std::vector<Paths> overhang(n);
            for (size_t i = 1; i < n; ++i) {
                Paths o = boolean(region[i], offset(region[i - 1], allowed), ClipperLib::ctDifference);
                // Kleinstflaechen und Kanten-Artefakte entfernen
                o = offset(offset(o, -w / 2), w / 2);
                Paths big;
                for (auto& p : o)
                    if (std::abs(ClipperLib::Area(p)) / (kScale * kScale) >= pp.supportMinArea) big.push_back(p);
                overhang[i] = mode == SupportMode::None ? Paths{} : big;
                // Erzwungene Stuetzen: jede nach unten zeigende Flaeche im Modifikator
                const double zMid = tops[i] - pp.layerHeight / 2;
                Paths forced = roleFootprint(PrintModifier::Role::SupportEnforcer, zMid);
                if (!forced.empty()) {
                    forced = boolean(boolean(region[i], region[i - 1], ClipperLib::ctDifference), forced, ClipperLib::ctIntersection);
                    overhang[i] = boolean(overhang[i], forced, ClipperLib::ctUnion);
                }
            }
            const size_t gap = static_cast<size_t>(std::max(0, pp.supportZGapLayers));
            Paths carry;
            std::vector<Paths> below(n);
            for (size_t j = 1; j < n; ++j)
                below[j] = boolean(below[j - 1], region[j - 1], ClipperLib::ctUnion);
            for (size_t jj = n; jj-- > 0;) {
                report(0.35 + 0.15 * static_cast<double>(n - jj) / static_cast<double>(n));
                const size_t src = jj + 1 + gap;
                if (src < n && !overhang[src].empty())
                    carry = boolean(carry, overhang[src], ClipperLib::ctUnion);
                if (carry.empty()) continue;
                // Stuetze endet, wo sie auf dem Teil aufsetzt
                carry = boolean(carry, region[jj], ClipperLib::ctDifference);
                Paths s = boolean(carry, offset(region[jj], pp.supportXYDistance), ClipperLib::ctDifference);
                if (mode == SupportMode::BuildPlateOnly) {
                    s = boolean(s, below[jj], ClipperLib::ctDifference);
                    carry = boolean(carry, below[jj], ClipperLib::ctDifference);
                }
                const double zMid = tops[jj] - (jj == 0 ? pp.firstLayerHeight : pp.layerHeight) / 2;
                const Paths blocked = roleFootprint(PrintModifier::Role::SupportBlocker, zMid);
                if (!blocked.empty()) {
                    s = boolean(s, blocked, ClipperLib::ctDifference);
                    Paths keep;
                    for (auto& p : s)
                        if (std::abs(ClipperLib::Area(p)) / (kScale * kScale) >= pp.supportMinArea) keep.push_back(p);
                    s = keep;
                }
                support[jj] = s;
            }
        }

        // --- Werkzeugwege je Schicht -------------------------------------------
        res.layers.resize(n);
        for (size_t i = 0; i < n; ++i) {
            report(0.50 + 0.30 * static_cast<double>(i) / static_cast<double>(n));
            SlicedLayer& L = res.layers[i];
            L.z = tops[i];
            L.height = i == 0 ? pp.firstLayerHeight : pp.layerHeight;

            if (i == 0) {
                const int brimLoops = static_cast<int>(std::lround(pp.brimWidth / w));
                for (int k = 0; k < brimLoops; ++k)
                    addLoops(L.paths, outerOnly(offset(region[0], w / 2 + k * w)), PathType::Brim);
                for (int k = pp.skirtLoops - 1; k >= 0; --k)
                    addLoops(L.paths, outerOnly(offset(region[0], brimLoops * w + pp.skirtDistance + k * w)),
                             PathType::Skirt);
            }

            // Waende von innen nach aussen
            for (int k = pp.perimeters - 1; k >= 0; --k)
                addLoops(L.paths, offset(region[i], -(w / 2 + k * w)),
                         k == 0 ? PathType::OuterWall : PathType::InnerWall);

            // Massive Bereiche: nicht von allen Nachbarschichten ueberdeckt
            Paths covered;
            bool firstCover = true;
            auto cover = [&](long j) {
                const Paths& other = (j < 0 || j >= static_cast<long>(n)) ? Paths{} : region[j];
                if (firstCover) { covered = other; firstCover = false; }
                else covered = boolean(covered, other, ClipperLib::ctIntersection);
            };
            for (int k = 1; k <= pp.bottomLayers; ++k) cover(static_cast<long>(i) - k);
            for (int k = 1; k <= pp.topLayers; ++k) cover(static_cast<long>(i) + k);

            Paths solid = firstCover ? Paths{} : boolean(inner[i], covered, ClipperLib::ctDifference);
            Paths sparse = boolean(inner[i], solid, ClipperLib::ctDifference);
            const double angle = pp.infillAngle + ((i % 2 == 0) ? 0.0 : 90.0);

            for (auto& l : lineFill(solid, w, angle))
                L.paths.push_back({PathType::Solid, std::move(l), false});
            auto fillSparse = [&](const Paths& area, double density, int pattern) {
                if (area.empty() || density <= 0.001) return;
                std::vector<Polygon2> infill;
                if (density >= 0.999) {
                    infill = lineFill(area, w, angle);
                } else if (static_cast<InfillPattern>(pattern) == InfillPattern::Grid) {
                    infill = lineFill(area, 2 * w / density, pp.infillAngle);
                    auto cross = lineFill(area, 2 * w / density, pp.infillAngle + 90.0);
                    infill.insert(infill.end(), cross.begin(), cross.end());
                } else {
                    infill = lineFill(area, w / density, angle);
                }
                for (auto& l : infill) L.paths.push_back({PathType::Infill, std::move(l), false});
            };
            // Infill-Zonen (in Reihenfolge) schneiden ihren Bereich aus der normalen Fuellung
            const double zMid = tops[i] - L.height / 2;
            for (const auto& m : modifiers) {
                if (m.role != PrintModifier::Role::InfillZone || sparse.empty()) continue;
                const Paths fp = footprint(m, zMid);
                if (fp.empty()) continue;
                const Paths zone = boolean(sparse, fp, ClipperLib::ctIntersection);
                sparse = boolean(sparse, fp, ClipperLib::ctDifference);
                fillSparse(zone, m.infillDensity, m.infillPattern);
            }
            fillSparse(sparse, pp.infillDensity, pp.infillPattern);
        }

        // --- Stuetzen fuellen (Linien in gleicher Richtung, damit sie sich stapeln)
        for (size_t i = 0; i < n; ++i) {
            if (support[i].empty()) continue;
            for (auto& l : lineFill(support[i], w / std::max(0.05, pp.supportDensity), 0.0))
                res.layers[i].paths.push_back({PathType::Support, std::move(l), false});
        }

        // --- Hoehen-Ereignisse den Schichten zuordnen ------------------------------
        {
            std::vector<HeightEvent> events = pp.events;
            std::stable_sort(events.begin(), events.end(),
                             [](const HeightEvent& a, const HeightEvent& b) { return a.z < b.z; });
            size_t next = 0;
            for (size_t i = 0; i < n && next < events.size(); ++i) {
                const double bottom = tops[i] - res.layers[i].height;
                while (next < events.size() && events[next].z <= bottom + 1e-6)
                    res.layers[i].events.push_back(events[next++]);
            }
            for (; next < events.size(); ++next) {
                std::ostringstream s;
                s << std::fixed << std::setprecision(2) << heightEventName(events[next].type)
                  << " bei Z = " << events[next].z << " mm liegt über dem Teil und wird ignoriert";
                res.warnings.push_back(s.str());
            }
        }

        // --- G-Code --------------------------------------------------------------
        GcodeWriter g(pp);
        g.comment("Erzeugt mit FormWerk");
        {
            std::ostringstream s;
            s << std::fixed << std::setprecision(2) << "Schichthoehe: " << pp.layerHeight << " mm, Schichten: " << n;
            g.comment(s.str());
        }
        g.startGcode();
        g.comment("LAYER_COUNT:" + std::to_string(n));

        for (size_t i = 0; i < n; ++i) {
            report(0.80 + 0.20 * static_cast<double>(i) / static_cast<double>(n));
            SlicedLayer& L = res.layers[i];
            g.comment("LAYER:" + std::to_string(i));
            for (const auto& e : L.events) g.heightEvent(e);
            if (i == 1) {
                if (pp.nozzleTemp != pp.nozzleTempFirstLayer) g.raw("M104 S" + std::to_string(pp.nozzleTemp));
                if (pp.bedTemp != pp.bedTempFirstLayer) g.raw("M140 S" + std::to_string(pp.bedTemp));
            }
            g.retract();
            g.moveZ(L.z);
            if (static_cast<int>(i) + 1 == pp.fanFromLayer) g.setFan(pp.fanPercent);

            // Reihenfolge innerhalb der Typen nach Leerweg optimieren
            std::vector<ToolPath> ordered;
            Vec2 pos{g.x(), g.y()};
            const std::vector<PathType> order = pp.outerWallFirst
                ? std::vector<PathType>{PathType::Skirt, PathType::Brim, PathType::OuterWall, PathType::InnerWall, PathType::Solid, PathType::Infill, PathType::Support}
                : std::vector<PathType>{PathType::Skirt, PathType::Brim, PathType::InnerWall, PathType::OuterWall, PathType::Solid, PathType::Infill, PathType::Support};
            const bool rear = static_cast<SeamPosition>(pp.seamPosition) == SeamPosition::Rear;
            for (PathType t : order) {
                const bool isWall = t == PathType::OuterWall || t == PathType::InnerWall;
                std::vector<Polygon2> loops, lines;
                for (auto& tp : L.paths)
                    if (tp.type == t) (tp.closed ? loops : lines).push_back(tp.points);
                std::vector<bool> used(loops.size(), false);
                for (size_t c = 0; c < loops.size(); ++c) {
                    size_t best = 0; double bestD = 1e300;
                    for (size_t j = 0; j < loops.size(); ++j) {
                        if (used[j]) continue;
                        for (const auto& v : loops[j]) {
                            const double d = dist2(v, pos);
                            if (d < bestD) { bestD = d; best = j; }
                        }
                    }
                    used[best] = true;
                    if (rear && isWall) rotateLoopToRear(loops[best]);
                    else rotateLoopTo(loops[best], pos);
                    pos = loops[best].front();
                    ordered.push_back({t, loops[best], true});
                }
                orderLines(lines, pos);
                if (!lines.empty()) pos = lines.back().back();
                for (auto& l : lines) ordered.push_back({t, std::move(l), false});
            }
            L.paths = std::move(ordered);

            PathType current = PathType::Infill;
            bool firstPath = true;
            for (const auto& tp : L.paths) {
                if (firstPath || tp.type != current) {
                    static const char* names[] = {"SKIRT", "WALL-OUTER", "WALL-INNER", "SKIN", "FILL", "SKIRT", "SUPPORT"};
                    g.comment(std::string("TYPE:") + names[static_cast<int>(tp.type)]);
                    current = tp.type;
                    firstPath = false;
                }
                double speed = pp.printSpeed;
                switch (tp.type) {
                case PathType::OuterWall: speed = pp.outerWallSpeed; break;
                case PathType::InnerWall: speed = pp.innerWallSpeed; break;
                case PathType::Solid: speed = pp.topBottomSpeed; break;
                case PathType::Skirt: case PathType::Brim: speed = pp.firstLayerSpeed; break;
                case PathType::Infill: speed = pp.printSpeed; break;
                case PathType::Support: speed = pp.supportSpeed; break;
                }
                if (i == 0) speed = std::min(speed, pp.firstLayerSpeed);

                g.travel(tp.points.front().x, tp.points.front().y);
                for (size_t k = 1; k < tp.points.size(); ++k)
                    g.extrudeTo(tp.points[k].x, tp.points[k].y, speed, L.height);
                if (tp.closed)
                    g.extrudeTo(tp.points.front().x, tp.points.front().y, speed, L.height);
            }
        }
        g.endGcode();

        res.filamentMm = g.filamentUsedMm();
        res.estimatedSeconds = g.estimatedSeconds();
        const double areaMm2 = M_PI * pp.filamentDiameter * pp.filamentDiameter / 4.0;
        res.filamentGrams = res.filamentMm * areaMm2 / 1000.0 * pp.filamentDensity;
        res.filamentCost = res.filamentGrams / 1000.0 * pp.filamentCostPerKg;
        std::ostringstream header;
        header << std::fixed << std::setprecision(0) << ";TIME:" << res.estimatedSeconds << '\n'
               << std::setprecision(4) << ";Filament used: " << res.filamentMm / 1000.0 << "m\n";
        res.gcode = header.str() + g.str();
        res.ok = true;
    } catch (const Cancelled&) {
        res = SliceResult{};
        res.cancelled = true;
        res.error = "Abgebrochen";
    } catch (const Standard_Failure& e) {
        res.error = std::string("Geometriefehler: ") + (e.GetMessageString() ? e.GetMessageString() : "");
    } catch (const std::exception& e) {
        res.error = e.what();
    }
    return res;
}

} // namespace fw
