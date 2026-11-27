#include "slicer/MeshSlicer.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <cmath>
#include <map>
#include <utility>

namespace fw {

std::vector<Triangle> triangulate(const TopoDS_Shape& shape, double deflection)
{
    std::vector<Triangle> out;
    BRepMesh_IncrementalMesh mesher(shape, deflection, false, 0.2, true);
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;
        const gp_Trsf trsf = loc.Transformation();
        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            int n[3];
            tri->Triangle(i).Get(n[0], n[1], n[2]);
            Triangle t;
            for (int k = 0; k < 3; ++k) {
                gp_Pnt p = tri->Node(n[k]).Transformed(trsf);
                t.v[k] = {p.X(), p.Y(), p.Z()};
            }
            out.push_back(t);
        }
    }
    return out;
}

namespace {

using Key = std::pair<long long, long long>;
constexpr double kQuant = 1e4; // 0,1 um Raster

Key keyOf(const Vec2& p)
{
    return {std::llround(p.x * kQuant), std::llround(p.y * kQuant)};
}

// Schnittpunkt der Kante a-b mit der Ebene z. Die Endpunkte werden
// kanonisch sortiert, damit benachbarte Dreiecke bitgleiche Punkte liefern.
Vec2 edgeCut(std::array<double, 3> a, std::array<double, 3> b, double z)
{
    if (b < a) std::swap(a, b);
    const double t = (z - a[2]) / (b[2] - a[2]);
    return {a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])};
}

} // namespace

std::vector<Polygon2> sliceAt(const std::vector<Triangle>& tris, double z)
{
    std::vector<std::pair<Vec2, Vec2>> segs;
    for (const auto& t : tris) {
        Vec2 pts[2];
        int n = 0;
        for (int k = 0; k < 3 && n < 2; ++k) {
            const auto& a = t.v[k];
            const auto& b = t.v[(k + 1) % 3];
            // halboffenes Intervall, damit Knoten genau auf z nicht doppelt zaehlen
            const bool aBelow = a[2] < z, bBelow = b[2] < z;
            if (aBelow != bBelow) pts[n++] = edgeCut(a, b, z);
        }
        if (n == 2 && keyOf(pts[0]) != keyOf(pts[1])) segs.push_back({pts[0], pts[1]});
    }

    std::multimap<Key, size_t> byPoint;
    for (size_t i = 0; i < segs.size(); ++i) {
        byPoint.insert({keyOf(segs[i].first), i});
        byPoint.insert({keyOf(segs[i].second), i});
    }

    std::vector<bool> used(segs.size(), false);
    std::vector<Polygon2> loops;

    auto findNext = [&](const Vec2& p) -> long {
        auto range = byPoint.equal_range(keyOf(p));
        for (auto it = range.first; it != range.second; ++it)
            if (!used[it->second]) return static_cast<long>(it->second);
        // Fallback: naechster freier Endpunkt innerhalb 5 um
        long best = -1;
        double bestD = 0.005 * 0.005;
        for (size_t i = 0; i < segs.size(); ++i) {
            if (used[i]) continue;
            for (const Vec2& q : {segs[i].first, segs[i].second}) {
                const double d = (q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y);
                if (d < bestD) { bestD = d; best = static_cast<long>(i); }
            }
        }
        return best;
    };

    for (size_t start = 0; start < segs.size(); ++start) {
        if (used[start]) continue;
        used[start] = true;
        Polygon2 loop{segs[start].first, segs[start].second};
        const Key startKey = keyOf(loop.front());
        while (true) {
            const Vec2 tail = loop.back();
            if (loop.size() > 2 && keyOf(tail) == startKey) { loop.pop_back(); break; }
            const long nx = findNext(tail);
            if (nx < 0) break; // offene Kontur (nicht geschlossener Koerper)
            used[nx] = true;
            const auto& s = segs[nx];
            const double d1 = std::hypot(s.first.x - tail.x, s.first.y - tail.y);
            const double d2 = std::hypot(s.second.x - tail.x, s.second.y - tail.y);
            loop.push_back(d1 <= d2 ? s.second : s.first);
        }
        if (loop.size() >= 3) loops.push_back(std::move(loop));
    }
    return loops;
}

} // namespace fw
