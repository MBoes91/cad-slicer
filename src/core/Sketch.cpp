#include "core/Sketch.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRep_Builder.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

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

namespace {

gp_Pnt toWorld(const gp_Ax3& ax, double u, double v)
{
    gp_Pnt p = ax.Location();
    p.Translate(gp_Vec(ax.XDirection()) * u + gp_Vec(ax.YDirection()) * v);
    return p;
}

TopoDS_Wire rectWire(const gp_Ax3& ax, const SketchRect& r)
{
    if (r.width <= 0 || r.height <= 0)
        throw std::runtime_error("Rechteck braucht positive Breite und Hoehe");
    BRepBuilderAPI_MakePolygon poly;
    poly.Add(toWorld(ax, r.x, r.y));
    poly.Add(toWorld(ax, r.x + r.width, r.y));
    poly.Add(toWorld(ax, r.x + r.width, r.y + r.height));
    poly.Add(toWorld(ax, r.x, r.y + r.height));
    poly.Close();
    return poly.Wire();
}

TopoDS_Wire circleWire(const gp_Ax3& ax, const SketchCircle& c)
{
    if (c.radius <= 0)
        throw std::runtime_error("Kreis braucht einen positiven Radius");
    gp_Ax2 a2(toWorld(ax, c.cx, c.cy), ax.Direction(), ax.XDirection());
    TopoDS_Edge e = BRepBuilderAPI_MakeEdge(gp_Circ(a2, c.radius));
    return BRepBuilderAPI_MakeWire(e);
}

bool circleInsideRect(const SketchCircle& c, const SketchRect& r)
{
    return c.cx - c.radius > r.x && c.cx + c.radius < r.x + r.width
        && c.cy - c.radius > r.y && c.cy + c.radius < r.y + r.height;
}

} // namespace

TopoDS_Shape Sketch::profile() const
{
    const gp_Ax3 ax = plane();
    const gp_Pln pln(ax);
    TopoDS_Shape result;

    auto addFace = [&](const TopoDS_Shape& face, bool cut) {
        if (result.IsNull()) {
            if (!cut) result = face;
            return;
        }
        if (cut) result = BRepAlgoAPI_Cut(result, face).Shape();
        else     result = BRepAlgoAPI_Fuse(result, face).Shape();
    };

    for (const auto& r : m_rects)
        addFace(BRepBuilderAPI_MakeFace(pln, rectWire(ax, r)).Face(), false);

    for (const auto& c : m_circles) {
        bool hole = false;
        for (const auto& r : m_rects)
            hole = hole || circleInsideRect(c, r);
        addFace(BRepBuilderAPI_MakeFace(pln, circleWire(ax, c)).Face(), hole);
    }

    if (result.IsNull())
        throw std::runtime_error("Skizze enthaelt kein geschlossenes Profil");

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
    for (const auto& r : m_rects)
        if (r.width > 0 && r.height > 0) b.Add(comp, rectWire(ax, r));
    for (const auto& c : m_circles)
        if (c.radius > 0) b.Add(comp, circleWire(ax, c));
    return comp;
}

} // namespace fw
