#pragma once

#include "core/Features.h"

#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>

#include <vector>

namespace fw {

// Bohrung an den Mittelpunkten der Skizze (Werkzeug "Punkt"; ohne solche an den Kreismitten),
// gebohrt entgegen der Skizzennormalen (umkehrbar).
class HoleFeature : public Feature, public ToolFeature {
public:
    enum class Kind { Simple, Counterbore, Countersink };   // Einfach, Senkung, Kegelsenkung
    HoleFeature(std::string name, SketchPtr sketch);

    FeatureType type() const override { return FeatureType::Hole; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override;
    TopoDS_Shape tool(const TopoDS_Shape& body) const override;
    BooleanOp toolOp() const override { return BooleanOp::Cut; }
    std::vector<ParamSlot> params() override;
    bool uses(const Feature* o) const override { return o == m_sketch.get(); }

    // Bohrungsmitten in Weltkoordinaten
    std::vector<gp_Pnt> centers() const;
    gp_Dir direction() const;

    SketchPtr sketch() const { return m_sketch; }
    void setSketch(SketchPtr s) { m_sketch = std::move(s); }

    Kind kind = Kind::Simple;
    bool throughAll = false;
    bool flip = false;
    double diameter = 6, depth = 10;
    double cbDiameter = 11, cbDepth = 4;        // Senkung
    double csDiameter = 12, csAngle = 90;       // Kegelsenkung
    double tipAngle = 118;                      // Bohrspitze (0 = flach)

private:
    SketchPtr m_sketch;
    ParamRef m_refs[7];
};

// Rundung bzw. Fase an Kanten. Kanten werden ueber ihren Mittelpunkt gespeichert
// und nach jeder Aenderung als naechstgelegene Kante wiedergefunden.
class EdgeFeature : public Feature {
public:
    EdgeFeature(std::string name, double size) : Feature(std::move(name)), size(size) {}
    std::vector<ParamSlot> params() override;
    std::vector<gp_Pnt> edges;
    double size;
protected:
    // Liefert die Kanten des Koerpers zu den gespeicherten Punkten
    std::vector<TopoDS_Edge> findEdges(const TopoDS_Shape& body) const;
    virtual const char* sizeLabel() const = 0;
    ParamRef m_ref;
};

class FilletFeature : public EdgeFeature {
public:
    explicit FilletFeature(std::string name, double radius = 2) : EdgeFeature(std::move(name), radius) {}
    FeatureType type() const override { return FeatureType::Fillet; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override;
protected:
    const char* sizeLabel() const override { return "Radius"; }
};

class ChamferFeature : public EdgeFeature {
public:
    explicit ChamferFeature(std::string name, double distance = 1) : EdgeFeature(std::move(name), distance) {}
    FeatureType type() const override { return FeatureType::Chamfer; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override;
protected:
    const char* sizeLabel() const override { return "Abstand"; }
};

// Mittelpunkt einer Kante (zum Speichern der Kantenauswahl)
gp_Pnt edgeMidpoint(const TopoDS_Edge& e);

enum class WorldAxis { X, Y, Z };

// Rechteckige oder runde Anordnung von Features (Extrusion, Drehung, Bohrung)
class PatternFeature : public Feature {
public:
    enum class Kind { Rectangular, Circular };
    PatternFeature(std::string name, Kind kind) : Feature(std::move(name)), kind(kind) {}

    FeatureType type() const override { return FeatureType::Pattern; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override;
    std::vector<ParamSlot> params() override;
    bool uses(const Feature* o) const override;

    Kind kind;
    std::vector<FeaturePtr> sources;
    // Rechteckig: Richtung 1 (und optional 2)
    WorldAxis dir1 = WorldAxis::X;
    bool flip1 = false;
    int count1 = 3;
    double spacing1 = 20;
    bool useDir2 = false;
    WorldAxis dir2 = WorldAxis::Y;
    bool flip2 = false;
    int count2 = 2;
    double spacing2 = 20;
    // Rund: Achse durch den Ursprung
    WorldAxis axis = WorldAxis::Z;
    int count = 6;
    double angle = 360;

private:
    ParamRef m_refs[3];
};

// Spiegeln von Features oder des ganzen Koerpers (sources leer) an einer Ursprungsebene
class MirrorFeature : public Feature {
public:
    explicit MirrorFeature(std::string name) : Feature(std::move(name)) {}
    FeatureType type() const override { return FeatureType::Mirror; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override;
    bool uses(const Feature* o) const override;

    std::vector<FeaturePtr> sources;
    int plane = 2; // 0 = XY, 1 = XZ, 2 = YZ
};

const char* axisName(WorldAxis a);

} // namespace fw
