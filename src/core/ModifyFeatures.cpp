#include "core/ModifyFeatures.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Bnd_Box.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace fw {

namespace {

gp_Dir axisDir(WorldAxis a)
{
    switch (a) {
    case WorldAxis::X: return gp::DX();
    case WorldAxis::Y: return gp::DY();
    case WorldAxis::Z: break;
    }
    return gp::DZ();
}

TopoDS_Shape fuseAll(const std::vector<TopoDS_Shape>& parts)
{
    TopoDS_Shape out;
    for (const auto& p : parts)
        out = out.IsNull() ? p : BRepAlgoAPI_Fuse(out, p).Shape();
    return out;
}

TopoDS_Shape moved(const TopoDS_Shape& s, const gp_Trsf& t)
{
    return BRepBuilderAPI_Transform(s, t, true).Shape();
}

double diagonal(const TopoDS_Shape& s)
{
    if (s.IsNull()) return 100;
    Bnd_Box b;
    BRepBndLib::Add(s, b);
    if (b.IsVoid()) return 100;
    return std::sqrt(b.SquareExtent());
}

// Werkzeuge der Quell-Features (unterdrueckte uebersprungen)
std::vector<std::pair<TopoDS_Shape, BooleanOp>> sourceTools(const std::vector<FeaturePtr>& src, const TopoDS_Shape& body)
{
    std::vector<std::pair<TopoDS_Shape, BooleanOp>> out;
    for (const auto& f : src) {
        if (f->isSuppressed()) continue;
        auto t = dynamic_cast<const ToolFeature*>(f.get());
        if (!t) throw std::runtime_error(f->name() + " laesst sich nicht vervielfaeltigen");
        out.emplace_back(t->tool(body), t->toolOp());
    }
    return out;
}

} // namespace

const char* axisName(WorldAxis a)
{
    switch (a) {
    case WorldAxis::X: return "X-Achse";
    case WorldAxis::Y: return "Y-Achse";
    case WorldAxis::Z: break;
    }
    return "Z-Achse";
}

// --- Bohrung ---------------------------------------------------------------------

HoleFeature::HoleFeature(std::string name, SketchPtr sketch)
    : Feature(std::move(name)), m_sketch(std::move(sketch))
{
}

std::vector<ParamSlot> HoleFeature::params()
{
    std::vector<ParamSlot> out{{"dia", "Durchmesser", &m_refs[0], &diameter, false}};
    if (!throughAll) out.push_back({"depth", "Tiefe", &m_refs[1], &depth, false});
    if (kind == Kind::Counterbore) {
        out.push_back({"cbd", "Senkungsdurchmesser", &m_refs[2], &cbDiameter, false});
        out.push_back({"cbdepth", "Senkungstiefe", &m_refs[3], &cbDepth, false});
    } else if (kind == Kind::Countersink) {
        out.push_back({"csd", "Kegelsenkungsdurchmesser", &m_refs[4], &csDiameter, false});
        out.push_back({"csangle", "Kegelsenkungswinkel", &m_refs[5], &csAngle, true});
    }
    if (!throughAll) out.push_back({"tip", "Bohrspitze", &m_refs[6], &tipAngle, true});
    return out;
}

std::vector<gp_Pnt> HoleFeature::centers() const
{
    std::vector<gp_Pnt> out;
    if (!m_sketch) return out;
    const auto& g = m_sketch->geometry();
    const gp_Ax3 ax = m_sketch->plane();
    auto world = [&](const SkPoint& p) {
        return ax.Location().Translated(gp_Vec(ax.XDirection()) * p.x + gp_Vec(ax.YDirection()) * p.y);
    };
    for (size_t i = 1; i < g.points.size(); ++i)
        if (g.points[i].center) out.push_back(world(g.points[i]));
    if (out.empty())
        for (const auto& e : g.entities)
            if (e.type == SkType::Circle && !e.construction) out.push_back(world(g.points[static_cast<size_t>(e.p0)]));
    return out;
}

gp_Dir HoleFeature::direction() const
{
    const gp_Dir n = m_sketch->plane().Direction();
    return flip ? n : n.Reversed();
}

TopoDS_Shape HoleFeature::tool(const TopoDS_Shape& body) const
{
    if (!m_sketch) throw std::runtime_error("Bohrung ohne Skizze");
    const auto pts = centers();
    if (pts.empty()) throw std::runtime_error("Die Skizze enthaelt keine Bohrungsmittelpunkte (Punkt oder Kreis)");
    if (diameter <= 0) throw std::runtime_error("Bohrungsdurchmesser muss groesser als 0 sein");
    const double r = diameter / 2;
    const double len = throughAll ? 2 * diagonal(body) : depth;
    if (len <= 0) throw std::runtime_error("Bohrungstiefe muss groesser als 0 sein");
    if (kind == Kind::Counterbore && (cbDiameter <= diameter || cbDepth <= 0 || cbDepth >= len))
        throw std::runtime_error("Senkung muss groesser als die Bohrung und weniger tief sein");
    if (kind == Kind::Countersink && (csDiameter <= diameter || csAngle <= 0 || csAngle >= 180))
        throw std::runtime_error("Kegelsenkung muss groesser als die Bohrung sein (Winkel 1-179 Grad)");

    const gp_Dir d = direction();
    const double over = 0.05; // ueber die Flaeche hinaus, damit der Schnitt sauber ist
    std::vector<TopoDS_Shape> parts;
    for (const gp_Pnt& c : pts) {
        const gp_Pnt start = c.Translated(gp_Vec(d) * -over);
        parts.push_back(BRepPrimAPI_MakeCylinder(gp_Ax2(start, d), r, len + over).Shape());
        if (!throughAll && tipAngle > 0 && tipAngle < 180) {
            const double h = r / std::tan(tipAngle * M_PI / 360.0);
            parts.push_back(BRepPrimAPI_MakeCone(gp_Ax2(c.Translated(gp_Vec(d) * len), d), r, 0, h).Shape());
        }
        if (kind == Kind::Counterbore) {
            parts.push_back(BRepPrimAPI_MakeCylinder(gp_Ax2(start, d), cbDiameter / 2, cbDepth + over).Shape());
        } else if (kind == Kind::Countersink) {
            const double h = (csDiameter - diameter) / 2 / std::tan(csAngle * M_PI / 360.0);
            const double rTop = csDiameter / 2 + over * std::tan(csAngle * M_PI / 360.0);
            parts.push_back(BRepPrimAPI_MakeCone(gp_Ax2(start, d), rTop, r, h + over).Shape());
        }
    }
    return fuseAll(parts);
}

TopoDS_Shape HoleFeature::apply(const TopoDS_Shape& body) const
{
    if (body.IsNull()) throw std::runtime_error("Bohrung braucht einen vorhandenen Koerper");
    return combine(body, tool(body), BooleanOp::Cut);
}

// --- Rundung / Fase ----------------------------------------------------------------

gp_Pnt edgeMidpoint(const TopoDS_Edge& e)
{
    BRepAdaptor_Curve c(e);
    return c.Value((c.FirstParameter() + c.LastParameter()) / 2);
}

std::vector<ParamSlot> EdgeFeature::params()
{
    return {{"size", sizeLabel(), &m_ref, &size, false}};
}

std::vector<TopoDS_Edge> EdgeFeature::findEdges(const TopoDS_Shape& body) const
{
    if (body.IsNull()) throw std::runtime_error("Kein Koerper vorhanden");
    if (edges.empty()) throw std::runtime_error("Keine Kanten gewaehlt");
    if (size <= 0) throw std::runtime_error(std::string(sizeLabel()) + " muss groesser als 0 sein");
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(body, TopAbs_EDGE, map);
    std::vector<TopoDS_Edge> out;
    for (const gp_Pnt& p : edges) {
        const TopoDS_Shape v = BRepBuilderAPI_MakeVertex(p).Shape();
        double best = 1e100;
        int bestIdx = 0;
        for (int i = 1; i <= map.Extent(); ++i) {
            BRepExtrema_DistShapeShape dist(v, map(i));
            if (dist.IsDone() && dist.Value() < best) { best = dist.Value(); bestIdx = i; }
        }
        if (bestIdx == 0 || best > 0.5) throw std::runtime_error("Kante nicht mehr gefunden - bitte neu waehlen");
        const TopoDS_Edge e = TopoDS::Edge(map(bestIdx));
        if (std::none_of(out.begin(), out.end(), [&](const TopoDS_Edge& o) { return o.IsSame(e); }))
            out.push_back(e);
    }
    return out;
}

TopoDS_Shape FilletFeature::apply(const TopoDS_Shape& body) const
{
    BRepFilletAPI_MakeFillet mk(body);
    for (const auto& e : findEdges(body)) mk.Add(size, e);
    mk.Build();
    if (!mk.IsDone()) throw std::runtime_error("Rundung nicht moeglich (Radius zu gross?)");
    return mk.Shape();
}

TopoDS_Shape ChamferFeature::apply(const TopoDS_Shape& body) const
{
    BRepFilletAPI_MakeChamfer mk(body);
    for (const auto& e : findEdges(body)) mk.Add(size, e);
    mk.Build();
    if (!mk.IsDone()) throw std::runtime_error("Fase nicht moeglich (Abstand zu gross?)");
    return mk.Shape();
}

// --- Muster ------------------------------------------------------------------------

std::vector<ParamSlot> PatternFeature::params()
{
    if (kind == Kind::Circular) return {{"angle", "Winkel", &m_refs[0], &angle, true}};
    std::vector<ParamSlot> out{{"s1", "Abstand 1", &m_refs[1], &spacing1, false}};
    if (useDir2) out.push_back({"s2", "Abstand 2", &m_refs[2], &spacing2, false});
    return out;
}

bool PatternFeature::uses(const Feature* o) const
{
    return std::any_of(sources.begin(), sources.end(), [&](const FeaturePtr& f) { return f.get() == o; });
}

TopoDS_Shape PatternFeature::apply(const TopoDS_Shape& body) const
{
    if (sources.empty()) throw std::runtime_error("Keine Features fuer das Muster gewaehlt");
    std::vector<gp_Trsf> moves;
    if (kind == Kind::Rectangular) {
        if (count1 < 1 || (useDir2 && count2 < 1)) throw std::runtime_error("Anzahl muss mindestens 1 sein");
        const gp_Vec v1 = gp_Vec(axisDir(dir1)) * (flip1 ? -spacing1 : spacing1);
        const gp_Vec v2 = gp_Vec(axisDir(dir2)) * (flip2 ? -spacing2 : spacing2);
        const int n2 = useDir2 ? count2 : 1;
        for (int j = 0; j < n2; ++j)
            for (int i = 0; i < count1; ++i) {
                if (i == 0 && j == 0) continue;
                gp_Trsf t;
                t.SetTranslation(v1 * i + v2 * j);
                moves.push_back(t);
            }
    } else {
        if (count < 2) throw std::runtime_error("Anzahl muss mindestens 2 sein");
        if (angle <= 0 || angle > 360) throw std::runtime_error("Winkel muss zwischen 0 und 360 Grad liegen");
        const double step = std::abs(angle - 360) < 1e-9 ? 360.0 / count : angle / (count - 1);
        for (int i = 1; i < count; ++i) {
            gp_Trsf t;
            t.SetRotation(gp_Ax1(gp::Origin(), axisDir(axis)), step * i * M_PI / 180.0);
            moves.push_back(t);
        }
    }
    const auto tools = sourceTools(sources, body);
    TopoDS_Shape result = body;
    for (const auto& [shape, op] : tools) {
        std::vector<TopoDS_Shape> copies;
        for (const auto& t : moves) copies.push_back(moved(shape, t));
        if (!copies.empty()) result = combine(result, fuseAll(copies), op);
    }
    return result;
}

// --- Spiegeln ----------------------------------------------------------------------

bool MirrorFeature::uses(const Feature* o) const
{
    return std::any_of(sources.begin(), sources.end(), [&](const FeaturePtr& f) { return f.get() == o; });
}

TopoDS_Shape MirrorFeature::apply(const TopoDS_Shape& body) const
{
    const gp_Dir n = plane == 0 ? gp::DZ() : plane == 1 ? gp::DY() : gp::DX();
    gp_Trsf t;
    t.SetMirror(gp_Ax2(gp::Origin(), n));
    if (sources.empty()) {
        if (body.IsNull()) throw std::runtime_error("Kein Koerper zum Spiegeln");
        return combine(body, moved(body, t), BooleanOp::Join);
    }
    TopoDS_Shape result = body;
    for (const auto& [shape, op] : sourceTools(sources, body))
        result = combine(result, moved(shape, t), op);
    return result;
}

} // namespace fw
