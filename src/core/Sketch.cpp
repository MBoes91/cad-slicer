#include "core/Sketch.h"

#include <BRepAlgoAPI_Cut.hxx>
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
    }
    return "?";
}

Sketch::Sketch(std::string name, SketchPlane plane, double offset)
    : Feature(std::move(name)), m_planeType(plane), m_offset(offset)
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

TopoDS_Shape Sketch::profile() const
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
    return comp;
}

} // namespace fw
