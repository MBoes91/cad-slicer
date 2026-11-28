#include "core/Sketch.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BOPAlgo_Builder.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <queue>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Builder.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Curve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <ElSLib.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace fw {

const char* planeName(SketchPlane p)
{
    switch (p) {
    case SketchPlane::XY: return "XY-Ebene";
    case SketchPlane::XZ: return "XZ-Ebene";
    case SketchPlane::YZ: return "YZ-Ebene";
    case SketchPlane::Custom: return "Fläche";
    }
    return "?";
}

Sketch::Sketch(std::string name, SketchPlane plane, double offset)
    : Feature(std::move(name)), m_planeType(plane), m_offset(offset)
{
}

Sketch::Sketch(std::string name, const gp_Ax3& frame)
    : Feature(std::move(name)), m_planeType(SketchPlane::Custom), m_offset(0.0), m_frame(frame)
{
}

gp_Ax3 Sketch::plane() const
{
    switch (m_planeType) {
    case SketchPlane::XY:
        return gp_Ax3(gp_Pnt(0, 0, m_offset), gp::DZ(), gp::DX());
    case SketchPlane::XZ:
        // Blick von vorne: Skizzen-X = Welt-X, Skizzen-Y = Welt-Z
        return gp_Ax3(gp_Pnt(0, m_offset, 0), -gp::DY(), gp::DX());
    case SketchPlane::YZ:
        return gp_Ax3(gp_Pnt(m_offset, 0, 0), gp::DX(), gp::DY());
    case SketchPlane::Custom: {
        gp_Ax3 a = m_frame;
        if (m_offset != 0.0) a.Translate(gp_Vec(a.Direction()) * m_offset);
        return a;
    }
    }
    return gp_Ax3();
}

RectangleRefs Sketch::addRectangle(double x, double y, double w, double h)
{
    auto& g = m_geo;
    const int p0 = g.addPoint(x, y), p1 = g.addPoint(x + w, y), p2 = g.addPoint(x + w, y + h), p3 = g.addPoint(x, y + h);
    RectangleRefs r{};
    r.lines[0] = g.addLine(p0, p1);
    r.lines[1] = g.addLine(p1, p2);
    r.lines[2] = g.addLine(p2, p3);
    r.lines[3] = g.addLine(p3, p0);
    g.addConstraint({ConType::Horizontal, r.lines[0]});
    g.addConstraint({ConType::Horizontal, r.lines[2]});
    g.addConstraint({ConType::Vertical, r.lines[1]});
    g.addConstraint({ConType::Vertical, r.lines[3]});
    SkConstraint dw{ConType::DistanceX, p0, p1, w};
    dw.labelY = -6;
    r.widthDim = g.addConstraint(dw);
    SkConstraint dh{ConType::DistanceY, p1, p2, h};
    dh.labelX = 6;
    r.heightDim = g.addConstraint(dh);
    return r;
}

int Sketch::addCircle(double cx, double cy, double r)
{
    auto& g = m_geo;
    const int c = g.addPoint(cx, cy);
    const int e = g.addCircle(c, r);
    SkConstraint d{ConType::Diameter, e, -1, 2 * r};
    d.labelX = r * 0.7 + 3;
    d.labelY = r * 0.7 + 3;
    return g.addConstraint(d);
}

bool Sketch::hasCenterPoints() const
{
    return std::any_of(m_geo.points.begin(), m_geo.points.end(), [](const SkPoint& p) { return p.center; });
}

bool Sketch::isEmpty() const
{
    return std::none_of(m_geo.entities.begin(), m_geo.entities.end(),
                        [](const SkEntity& e) { return !e.construction; });
}

namespace {

gp_Pnt toWorld(const gp_Ax3& ax, double u, double v)
{
    gp_Pnt p = ax.Location();
    p.Translate(gp_Vec(ax.XDirection()) * u + gp_Vec(ax.YDirection()) * v);
    return p;
}

TopoDS_Edge makeEdge(const gp_Ax3& ax, const SketchGeometry& g, const SkEntity& e, bool reversed = false)
{
    const auto& P = g.points;
    switch (e.type) {
    case SkType::Line: {
        gp_Pnt a = toWorld(ax, P[e.p0].x, P[e.p0].y), b = toWorld(ax, P[e.p1].x, P[e.p1].y);
        if (reversed) std::swap(a, b);
        if (a.Distance(b) < 1e-7) throw std::runtime_error("Linie mit Laenge 0");
        return BRepBuilderAPI_MakeEdge(a, b);
    }
    case SkType::Circle: {
        if (e.r < 1e-7) throw std::runtime_error("Kreis mit Radius 0");
        gp_Ax2 a2(toWorld(ax, P[e.p0].x, P[e.p0].y), ax.Direction(), ax.XDirection());
        return BRepBuilderAPI_MakeEdge(gp_Circ(a2, e.r));
    }
    case SkType::Arc: {
        const double r = std::hypot(P[e.p1].x - P[e.p0].x, P[e.p1].y - P[e.p0].y);
        if (r < 1e-7) throw std::runtime_error("Bogen mit Radius 0");
        gp_Ax2 a2(toWorld(ax, P[e.p0].x, P[e.p0].y), ax.Direction(), ax.XDirection());
        Handle(Geom_TrimmedCurve) arc = GC_MakeArcOfCircle(gp_Circ(a2, r), toWorld(ax, P[e.p1].x, P[e.p1].y),
                                                           toWorld(ax, P[e.p2].x, P[e.p2].y), true).Value();
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(arc);
        if (reversed) edge.Reverse();
        return edge;
    }
    }
    return {};
}

} // namespace

std::vector<Sketch::Region> Sketch::regions() const
{
    const gp_Ax3 ax = plane();
    const gp_Pln pln(ax);
    // 1. Alle Kanten gegenseitig an Schnittpunkten teilen
    BOPAlgo_Builder gf;
    int n = 0;
    for (const auto& e : m_geo.entities) {
        if (e.construction) continue;
        try { gf.AddArgument(makeEdge(ax, m_geo, e)); ++n; } catch (...) {}
    }
    if (n == 0) return {};
    gf.SetFuzzyValue(1e-6);
    gf.Perform();
    if (gf.HasErrors()) throw std::runtime_error("Skizzenkanten konnten nicht verschnitten werden");

    // 2. Offene Enden (Kanten mit freiem Endpunkt) wiederholt entfernen
    std::vector<TopoDS_Edge> edges;
    for (TopExp_Explorer ex(gf.Shape(), TopAbs_EDGE); ex.More(); ex.Next()) edges.push_back(TopoDS::Edge(ex.Current()));
    for (bool changed = true; changed;) {
        changed = false;
        TopoDS_Compound c;
        BRep_Builder b;
        b.MakeCompound(c);
        for (const auto& e : edges) b.Add(c, e);
        TopTools_IndexedDataMapOfShapeListOfShape vmap;
        TopExp::MapShapesAndAncestors(c, TopAbs_VERTEX, TopAbs_EDGE, vmap);
        std::vector<TopoDS_Edge> keep;
        for (const auto& e : edges) {
            TopoDS_Vertex v0, v1;
            TopExp::Vertices(e, v0, v1);
            const bool closed = !v0.IsNull() && v0.IsSame(v1);
            bool free = false;
            for (const TopoDS_Vertex& v : {v0, v1}) {
                if (v.IsNull() || closed) continue;
                int deg = 0;
                for (TopTools_ListIteratorOfListOfShape it(vmap.FindFromKey(v)); it.More(); it.Next()) ++deg;
                if (deg < 2) free = true;
            }
            if (free) changed = true;
            else keep.push_back(e);
        }
        edges.swap(keep);
    }
    if (edges.empty()) return {};

    // 3. Grosse Ebenenflaeche mit den Kanten teilen
    Bnd_Box box;
    for (const auto& e : edges) BRepBndLib::Add(e, box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    const double big = 10.0 + std::max({x1 - x0, y1 - y0, z1 - z0}) * 2.0;
    const gp_Pnt mid((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    double mu, mv;
    ElSLib::Parameters(pln, mid, mu, mv);
    const gp_Pln centred(gp_Ax3(ElSLib::Value(mu, mv, pln), ax.Direction(), ax.XDirection()));
    TopoDS_Face sheet = BRepBuilderAPI_MakeFace(centred, -big, big, -big, big).Face();
    TopTools_IndexedMapOfShape outerEdges;
    TopExp::MapShapes(sheet, TopAbs_EDGE, outerEdges);

    BRepAlgoAPI_Splitter sp;
    TopTools_ListOfShape args, tools;
    args.Append(sheet);
    for (const auto& e : edges) tools.Append(e);
    sp.SetArguments(args);
    sp.SetTools(tools);
    sp.SetFuzzyValue(1e-6);
    sp.Build();
    if (!sp.IsDone()) throw std::runtime_error("Skizzenbereiche konnten nicht gebildet werden");

    std::vector<TopoDS_Face> faces;
    for (TopExp_Explorer ex(sp.Shape(), TopAbs_FACE); ex.More(); ex.Next()) faces.push_back(TopoDS::Face(ex.Current()));

    // 4. Ebenen: Aussenraum = 0, je gekreuzter Kante +1 (Breitensuche)
    TopoDS_Compound all;
    BRep_Builder bb;
    bb.MakeCompound(all);
    for (const auto& f : faces) bb.Add(all, f);
    TopTools_IndexedDataMapOfShapeListOfShape emap;
    TopExp::MapShapesAndAncestors(all, TopAbs_EDGE, TopAbs_FACE, emap);
    auto indexOf = [&](const TopoDS_Shape& f) {
        for (size_t i = 0; i < faces.size(); ++i) if (faces[i].IsSame(f)) return static_cast<int>(i);
        return -1;
    };
    std::vector<int> level(faces.size(), -1);
    std::queue<int> q;
    for (size_t i = 0; i < faces.size(); ++i) {
        // Aussenstueck: Teil seines Randes liegt auf dem Rand der grossen Flaeche
        for (TopExp_Explorer ex(faces[i], TopAbs_EDGE); ex.More(); ex.Next()) {
            const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
            for (TopExp_Explorer vx(e, TopAbs_VERTEX); vx.More(); vx.Next()) {
                const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vx.Current()));
                double u, v;
                ElSLib::Parameters(centred, p, u, v);
                if (std::abs(std::abs(u) - big) < 1e-6 || std::abs(std::abs(v) - big) < 1e-6) { level[i] = 0; break; }
            }
            if (level[i] == 0) break;
        }
        if (level[i] == 0) q.push(static_cast<int>(i));
    }
    while (!q.empty()) {
        const int i = q.front();
        q.pop();
        for (TopExp_Explorer ex(faces[static_cast<size_t>(i)], TopAbs_EDGE); ex.More(); ex.Next()) {
            const int k = emap.FindIndex(ex.Current());
            if (k == 0) continue;
            for (TopTools_ListIteratorOfListOfShape it(emap.FindFromIndex(k)); it.More(); it.Next()) {
                const int j = indexOf(it.Value());
                if (j >= 0 && level[static_cast<size_t>(j)] < 0) {
                    level[static_cast<size_t>(j)] = level[static_cast<size_t>(i)] + 1;
                    q.push(j);
                }
            }
        }
    }
    std::vector<Region> out;
    for (size_t i = 0; i < faces.size(); ++i) {
        if (level[i] == 0) continue;
        out.push_back({faces[i], level[i] < 0 ? 1 : level[i]});
    }
    return out;
}

namespace {

// Liegt p in der Flaeche (Rand zaehlt nicht)?
bool faceContains(const TopoDS_Face& f, const gp_Pnt& p)
{
    BRepClass_FaceClassifier cls(f, p, 1e-6);
    return cls.State() == TopAbs_IN;
}

TopoDS_Shape fuseFaces(const std::vector<TopoDS_Face>& faces)
{
    if (faces.empty()) throw std::runtime_error("Skizze enthaelt kein geschlossenes Profil");
    TopoDS_Shape result = faces.front();
    for (size_t i = 1; i < faces.size(); ++i) result = BRepAlgoAPI_Fuse(result, faces[i]).Shape();
    ShapeUpgrade_UnifySameDomain unify(result, true, true, true);
    unify.Build();
    return unify.Shape();
}

} // namespace

TopoDS_Shape Sketch::profile(const std::vector<gp_Pnt>& picks) const
{
    if (!picks.empty()) {
        std::vector<TopoDS_Face> chosen;
        for (const auto& r : regions())
            for (const auto& p : picks)
                if (faceContains(r.face, p)) { chosen.push_back(r.face); break; }
        if (chosen.empty()) throw std::runtime_error("Gewaehlte Profilbereiche nicht mehr vorhanden");
        return fuseFaces(chosen);
    }
    try {
        return simpleProfile();
    } catch (const std::exception&) {
        // Verzweigte oder offene Konturen: Bereiche bilden wie in Inventor
        std::vector<TopoDS_Face> chosen;
        for (const auto& r : regions())
            if (r.level % 2 == 1) chosen.push_back(r.face);
        if (chosen.empty()) throw std::runtime_error("Skizze enthaelt kein geschlossenes Profil");
        return fuseFaces(chosen);
    }
}

TopoDS_Shape Sketch::simpleProfile() const
{
    const gp_Ax3 ax = plane();
    const gp_Pln pln(ax);
    const auto& g = m_geo;

    // Endpunkte nach Lage zusammenfassen (robust gegen getrennte, aber deckungsgleiche Punkte)
    std::vector<int> cluster(g.points.size(), -1);
    std::vector<SkPoint> reps;
    auto clusterOf = [&](int p) {
        if (cluster[static_cast<size_t>(p)] >= 0) return cluster[static_cast<size_t>(p)];
        for (size_t k = 0; k < reps.size(); ++k)
            if (std::hypot(reps[k].x - g.points[p].x, reps[k].y - g.points[p].y) < 1e-6)
                return cluster[static_cast<size_t>(p)] = static_cast<int>(k);
        reps.push_back(g.points[p]);
        return cluster[static_cast<size_t>(p)] = static_cast<int>(reps.size()) - 1;
    };

    std::vector<TopoDS_Wire> wires;
    struct Edge { int entity, a, b; };
    std::vector<Edge> edges;
    for (size_t i = 0; i < g.entities.size(); ++i) {
        const auto& e = g.entities[i];
        if (e.construction) continue;
        if (e.type == SkType::Circle) {
            wires.push_back(BRepBuilderAPI_MakeWire(makeEdge(ax, g, e)));
        } else {
            edges.push_back({static_cast<int>(i), clusterOf(e.p1 >= 0 && e.type == SkType::Arc ? e.p1 : e.p0),
                             clusterOf(e.type == SkType::Arc ? e.p2 : e.p1)});
        }
    }

    // Konturen ablaufen
    std::multimap<int, size_t> byNode;
    for (size_t i = 0; i < edges.size(); ++i) {
        byNode.insert({edges[i].a, i});
        byNode.insert({edges[i].b, i});
    }
    for (const auto& kv : byNode)
        if (byNode.count(kv.first) != 2)
            throw std::runtime_error("Skizze enthaelt offene oder verzweigte Konturen");

    std::vector<bool> used(edges.size(), false);
    for (size_t start = 0; start < edges.size(); ++start) {
        if (used[start]) continue;
        BRepBuilderAPI_MakeWire mw;
        size_t cur = start;
        int node = edges[start].a;
        while (!used[cur]) {
            used[cur] = true;
            const Edge& e = edges[cur];
            const bool reversed = e.a != node;
            mw.Add(makeEdge(ax, g, g.entities[static_cast<size_t>(e.entity)], reversed));
            node = reversed ? e.a : e.b;
            auto range = byNode.equal_range(node);
            for (auto it = range.first; it != range.second; ++it)
                if (!used[it->second]) { cur = it->second; break; }
        }
        if (!mw.IsDone()) throw std::runtime_error("Kontur konnte nicht geschlossen werden");
        wires.push_back(mw.Wire());
    }
    if (wires.empty()) throw std::runtime_error("Skizze enthaelt kein geschlossenes Profil");

    // Flaechen und Verschachtelungstiefe (gerade = Material, ungerade = Loch)
    std::vector<TopoDS_Face> faces;
    for (const auto& w : wires) {
        BRepBuilderAPI_MakeFace mf(pln, w, true);
        if (!mf.IsDone()) throw std::runtime_error("Profilflaeche konnte nicht erzeugt werden");
        TopoDS_Face f = mf.Face();
        faces.push_back(f);
    }
    auto samplePoint = [&](const TopoDS_Wire& w) {
        TopoDS_Vertex v;
        for (TopExp_Explorer ex(w, TopAbs_VERTEX); ex.More(); ex.Next()) { v = TopoDS::Vertex(ex.Current()); break; }
        if (!v.IsNull()) return BRep_Tool::Pnt(v);
        // Kreis ohne Ecken: Punkt auf der Kurve
        TopExp_Explorer ex(w, TopAbs_EDGE);
        double f0, f1;
        Handle(Geom_Curve) c = BRep_Tool::Curve(TopoDS::Edge(ex.Current()), f0, f1);
        return c->Value(f0);
    };
    std::vector<int> depth(faces.size(), 0);
    for (size_t i = 0; i < faces.size(); ++i) {
        const gp_Pnt p = samplePoint(wires[i]);
        for (size_t j = 0; j < faces.size(); ++j) {
            if (i == j) continue;
            BRepClass_FaceClassifier cls(faces[j], p, 1e-6);
            if (cls.State() == TopAbs_IN) ++depth[i];
        }
    }
    std::vector<size_t> order(faces.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return depth[a] < depth[b]; });

    TopoDS_Shape result;
    for (size_t i : order) {
        if (result.IsNull()) {
            if (depth[i] % 2 == 0) result = faces[i];
            continue;
        }
        if (depth[i] % 2 == 0) result = BRepAlgoAPI_Fuse(result, faces[i]).Shape();
        else result = BRepAlgoAPI_Cut(result, faces[i]).Shape();
    }
    if (result.IsNull()) throw std::runtime_error("Skizze enthaelt kein geschlossenes Profil");
    ShapeUpgrade_UnifySameDomain unify(result, true, true, true);
    unify.Build();
    return unify.Shape();
}

TopoDS_Shape Sketch::wireframe() const
{
    const gp_Ax3 ax = plane();
    TopoDS_Compound comp;
    BRep_Builder b;
    b.MakeCompound(comp);
    for (const auto& e : m_geo.entities) {
        if (e.construction) continue;
        try { b.Add(comp, makeEdge(ax, m_geo, e)); } catch (...) {}
    }
    // Mittelpunkte als kleines Kreuz
    for (size_t i = 1; i < m_geo.points.size(); ++i) {
        if (!m_geo.points[i].center) continue;
        const auto& p = m_geo.points[i];
        auto at = [&](double dx, double dy) {
            return ax.Location().Translated(gp_Vec(ax.XDirection()) * (p.x + dx) + gp_Vec(ax.YDirection()) * (p.y + dy));
        };
        const double s = 1.5;
        b.Add(comp, BRepBuilderAPI_MakeEdge(at(-s, 0), at(s, 0)).Edge());
        b.Add(comp, BRepBuilderAPI_MakeEdge(at(0, -s), at(0, s)).Edge());
    }
    return comp;
}

} // namespace fw
