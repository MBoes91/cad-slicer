#include "app/ToolpathPreview.h"

#include <Graphic3d_ArrayOfSegments.hxx>
#include <algorithm>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>

namespace {

Quantity_Color colorFor(fw::PathType t, bool current)
{
    const double k = current ? 1.0 : 0.75; // aeltere Schichten etwas dunkler
    auto c = [k](double r, double g, double b) { return Quantity_Color(r * k, g * k, b * k, Quantity_TOC_sRGB); };
    switch (t) {
    case fw::PathType::Skirt: return c(0.55, 0.55, 0.55);
    case fw::PathType::Brim: return c(0.35, 0.55, 0.85);
    case fw::PathType::OuterWall: return c(0.90, 0.25, 0.20);
    case fw::PathType::InnerWall: return c(0.95, 0.65, 0.15);
    case fw::PathType::Solid: return c(0.75, 0.30, 0.75);
    case fw::PathType::Infill: return c(0.20, 0.65, 0.35);
    }
    return c(1, 1, 1);
}

} // namespace

void ToolpathPreview::Compute(const Handle(PrsMgr_PresentationManager)&,
                              const Handle(Prs3d_Presentation)& prs, const Standard_Integer)
{
    const int last = std::min<int>(m_maxLayer, static_cast<int>(m_result.layers.size()) - 1);
    int nbVerts = 0;
    for (int i = 0; i <= last; ++i)
        for (const auto& tp : m_result.layers[static_cast<size_t>(i)].paths)
            nbVerts += 2 * static_cast<int>(tp.points.size());
    if (nbVerts == 0) return;

    Handle(Graphic3d_ArrayOfSegments) arr =
        new Graphic3d_ArrayOfSegments(nbVerts, 0, Graphic3d_ArrayFlags_VertexColor);
    for (int i = 0; i <= last; ++i) {
        const auto& L = m_result.layers[static_cast<size_t>(i)];
        for (const auto& tp : L.paths) {
            const Quantity_Color col = colorFor(tp.type, i == last);
            const size_t n = tp.points.size();
            const size_t segs = tp.closed ? n : n - 1;
            for (size_t k = 0; k < segs; ++k) {
                const auto& a = tp.points[k];
                const auto& b = tp.points[(k + 1) % n];
                arr->AddVertex(gp_Pnt(a.x, a.y, L.z), col);
                arr->AddVertex(gp_Pnt(b.x, b.y, L.z), col);
            }
        }
    }
    Handle(Graphic3d_Group) g = prs->NewGroup();
    g->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(Quantity_NOC_WHITE, Aspect_TOL_SOLID, 1.5));
    g->AddPrimitiveArray(arr);
}
