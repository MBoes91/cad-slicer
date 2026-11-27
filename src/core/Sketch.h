#pragma once

#include "core/Feature.h"

#include <gp_Ax3.hxx>

#include <vector>

namespace fw {

enum class SketchPlane { XY, XZ, YZ };

struct SketchRect {
    double x = 0, y = 0;      // linke untere Ecke in Skizzenkoordinaten
    double width = 10, height = 10;
};

struct SketchCircle {
    double cx = 0, cy = 0;
    double radius = 5;
};

// Vorlaeufige 2D-Skizze. Die Elemente sind direkt ueber ihre Masse definiert
// (das sind die "Bemassungen"). Ein allgemeiner Abhaengigkeits-Solver fuer
// Linien, Boegen und Bedingungen wie parallel/koinzident folgt im naechsten Schritt.
class Sketch : public Feature {
public:
    Sketch(std::string name, SketchPlane plane, double offset = 0.0);

    FeatureType type() const override { return FeatureType::Sketch; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override { return body; }

    SketchPlane planeType() const { return m_planeType; }
    double offset() const { return m_offset; }
    gp_Ax3 plane() const;

    std::vector<SketchRect>& rectangles() { return m_rects; }
    const std::vector<SketchRect>& rectangles() const { return m_rects; }
    std::vector<SketchCircle>& circles() { return m_circles; }
    const std::vector<SketchCircle>& circles() const { return m_circles; }

    bool isEmpty() const { return m_rects.empty() && m_circles.empty(); }

    // Geschlossene Profilflaechen der Skizze. Kreise, die vollstaendig in einem
    // Rechteck liegen, werden als Loecher ausgespart.
    TopoDS_Shape profile() const;

    // Kanten der Skizze zur Anzeige im 3D-Fenster.
    TopoDS_Shape wireframe() const;

private:
    SketchPlane m_planeType;
    double m_offset;
    std::vector<SketchRect> m_rects;
    std::vector<SketchCircle> m_circles;
};

using SketchPtr = std::shared_ptr<Sketch>;

const char* planeName(SketchPlane p);

} // namespace fw
