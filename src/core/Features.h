#pragma once

#include "core/Sketch.h"

namespace fw {

enum class BooleanOp { Join, Cut, Intersect };
const char* booleanOpName(BooleanOp op);

// Extrusion eines Skizzenprofils entlang der Skizzennormalen.
class ExtrudeFeature : public Feature {
public:
    ExtrudeFeature(std::string name, SketchPtr sketch, double distance,
                   BooleanOp op = BooleanOp::Join, bool symmetric = false);

    FeatureType type() const override { return FeatureType::Extrude; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override;

    SketchPtr sketch() const { return m_sketch; }
    void setSketch(SketchPtr s) { m_sketch = std::move(s); }
    double distance() const { return m_distance; }
    void setDistance(double d) { m_distance = d; }
    BooleanOp operation() const { return m_op; }
    void setOperation(BooleanOp op) { m_op = op; }
    bool symmetric() const { return m_symmetric; }
    void setSymmetric(bool s) { m_symmetric = s; }

private:
    SketchPtr m_sketch;
    double m_distance;
    BooleanOp m_op;
    bool m_symmetric;
};

enum class RevolveAxis { SketchX, SketchY };

// Drehung eines Skizzenprofils um eine Skizzenachse.
class RevolveFeature : public Feature {
public:
    RevolveFeature(std::string name, SketchPtr sketch, RevolveAxis axis,
                   double angleDeg = 360.0, BooleanOp op = BooleanOp::Join);

    FeatureType type() const override { return FeatureType::Revolve; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override;

    SketchPtr sketch() const { return m_sketch; }
    void setSketch(SketchPtr s) { m_sketch = std::move(s); }
    RevolveAxis axis() const { return m_axis; }
    void setAxis(RevolveAxis a) { m_axis = a; }
    double angle() const { return m_angle; }
    void setAngle(double a) { m_angle = a; }
    BooleanOp operation() const { return m_op; }
    void setOperation(BooleanOp op) { m_op = op; }

private:
    SketchPtr m_sketch;
    RevolveAxis m_axis;
    double m_angle;
    BooleanOp m_op;
};

// Verknuepft ein neues Werkzeug mit dem bisherigen Koerper.
TopoDS_Shape combine(const TopoDS_Shape& body, const TopoDS_Shape& tool, BooleanOp op);

} // namespace fw
