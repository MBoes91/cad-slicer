#pragma once

#include "core/Feature.h"
#include "core/SketchGeometry.h"

#include <gp_Ax3.hxx>

namespace fw {

enum class SketchPlane { XY, XZ, YZ };

struct RectangleRefs {
    int lines[4];
    int widthDim, heightDim;
};

// 2D-Skizze auf einer Ursprungsebene: Geometrie, Abhaengigkeiten, Bemassungen.
class Sketch : public Feature {
public:
    Sketch(std::string name, SketchPlane plane, double offset = 0.0);

    FeatureType type() const override { return FeatureType::Sketch; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override { return body; }

    SketchPlane planeType() const { return m_planeType; }
    double offset() const { return m_offset; }
    void setOffset(double o) { m_offset = o; }
    gp_Ax3 plane() const;

    SketchGeometry& geometry() { return m_geo; }
    const SketchGeometry& geometry() const { return m_geo; }

    // Bequeme Helfer: Rechteck (achsparallel, bemasst) und Kreis (bemasst)
    RectangleRefs addRectangle(double x, double y, double w, double h);
    int addCircle(double cx, double cy, double r); // liefert Index der Durchmesser-Bemassung

    bool isEmpty() const;

    // Geschlossene Profilflaechen (Konstruktionslinien zaehlen nicht).
    // Ineinanderliegende Konturen werden abwechselnd als Material und Loch gewertet.
    TopoDS_Shape profile() const;

    // Kanten der Skizze zur Anzeige im 3D-Fenster.
    TopoDS_Shape wireframe() const;

private:
    SketchPlane m_planeType;
    double m_offset;
    SketchGeometry m_geo;
};

using SketchPtr = std::shared_ptr<Sketch>;

const char* planeName(SketchPlane p);

} // namespace fw
