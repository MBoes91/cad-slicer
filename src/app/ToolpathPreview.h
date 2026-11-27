#pragma once

#include "slicer/Slicer.h"

#include <AIS_InteractiveObject.hxx>

// Zeigt die Werkzeugwege des Slicers (bis zu einer Schicht) farbig an.
class ToolpathPreview : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTI_INLINE(ToolpathPreview, AIS_InteractiveObject)
public:
    ToolpathPreview(const fw::SliceResult& result, int maxLayer)
        : m_result(result), m_maxLayer(maxLayer) {}

private:
    void Compute(const Handle(PrsMgr_PresentationManager)& mgr,
                 const Handle(Prs3d_Presentation)& prs, const Standard_Integer mode) override;
    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override {}

    const fw::SliceResult& m_result;
    int m_maxLayer;
};
