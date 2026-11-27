#include "core/Features.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <gp_Trsf.hxx>
#include <BRepBuilderAPI_Transform.hxx>

#include <cmath>
#include <stdexcept>

namespace fw {

const char* booleanOpName(BooleanOp op)
{
    switch (op) {
    case BooleanOp::Join: return "Verbinden";
    case BooleanOp::Cut: return "Differenz";
    case BooleanOp::Intersect: return "Schnittmenge";
    }
    return "?";
}

TopoDS_Shape combine(const TopoDS_Shape& body, const TopoDS_Shape& tool, BooleanOp op)
{
    TopoDS_Shape result;
    if (body.IsNull()) {
        if (op != BooleanOp::Join)
            throw std::runtime_error("Differenz/Schnittmenge braucht einen vorhandenen Koerper");
        return tool;
    }
    switch (op) {
    case BooleanOp::Join: result = BRepAlgoAPI_Fuse(body, tool).Shape(); break;
    case BooleanOp::Cut: result = BRepAlgoAPI_Cut(body, tool).Shape(); break;
    case BooleanOp::Intersect: result = BRepAlgoAPI_Common(body, tool).Shape(); break;
    }
    ShapeUpgrade_UnifySameDomain unify(result, true, true, true);
    unify.Build();
    return unify.Shape();
}

ExtrudeFeature::ExtrudeFeature(std::string name, SketchPtr sketch, double distance,
                               BooleanOp op, bool symmetric)
    : Feature(std::move(name)), m_sketch(std::move(sketch)), m_distance(distance),
      m_op(op), m_symmetric(symmetric)
{
}

TopoDS_Shape ExtrudeFeature::apply(const TopoDS_Shape& body) const
{
    return combine(body, tool(body), m_op);
}

TopoDS_Shape ExtrudeFeature::tool(const TopoDS_Shape&) const
{
    if (!m_sketch) throw std::runtime_error("Extrusion ohne Skizze");
    if (std::abs(m_distance) < 1e-6) throw std::runtime_error("Extrusionsabstand ist 0");

    TopoDS_Shape profile = m_sketch->profile();
    const gp_Dir n = m_sketch->plane().Direction();
    if (m_symmetric) {
        gp_Trsf t;
        t.SetTranslation(gp_Vec(n) * (-m_distance / 2.0));
        profile = BRepBuilderAPI_Transform(profile, t, true).Shape();
    }
    return BRepPrimAPI_MakePrism(profile, gp_Vec(n) * m_distance).Shape();
}

RevolveFeature::RevolveFeature(std::string name, SketchPtr sketch, RevolveAxis axis,
                               double angleDeg, BooleanOp op)
    : Feature(std::move(name)), m_sketch(std::move(sketch)), m_axis(axis),
      m_angle(angleDeg), m_op(op)
{
}

TopoDS_Shape RevolveFeature::apply(const TopoDS_Shape& body) const
{
    return combine(body, tool(body), m_op);
}

TopoDS_Shape RevolveFeature::tool(const TopoDS_Shape&) const
{
    if (!m_sketch) throw std::runtime_error("Drehung ohne Skizze");
    if (m_angle <= 0 || m_angle > 360) throw std::runtime_error("Drehwinkel muss zwischen 0 und 360 Grad liegen");

    const gp_Ax3 ax = m_sketch->plane();
    const gp_Ax1 axis(ax.Location(), m_axis == RevolveAxis::SketchX ? ax.XDirection() : ax.YDirection());
    return BRepPrimAPI_MakeRevol(m_sketch->profile(), axis, m_angle * M_PI / 180.0).Shape();
}

} // namespace fw
