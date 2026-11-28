#pragma once

#include "core/Feature.h"
#include "core/SketchGeometry.h"

#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <TopoDS_Face.hxx>
#include <vector>

namespace fw {

// Custom: beliebige Ebene (z. B. auf einer Bauteilflaeche), Lage in frame()
enum class SketchPlane { XY, XZ, YZ, Custom };

struct RectangleRefs {
    int lines[4];
    int widthDim, heightDim;
};

// 2D-Skizze auf einer Ursprungsebene: Geometrie, Abhaengigkeiten, Bemassungen.
class Sketch : public Feature {
public:
    Sketch(std::string name, SketchPlane plane, double offset = 0.0);
    // Skizze auf einer frei liegenden Ebene (Ursprung, Normale, X-Richtung)
    Sketch(std::string name, const gp_Ax3& frame);

    FeatureType type() const override { return FeatureType::Sketch; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override { return body; }

    SketchPlane planeType() const { return m_planeType; }
    double offset() const { return m_offset; }
    void setOffset(double o) { m_offset = o; }
    gp_Ax3 plane() const;
    const gp_Ax3& frame() const { return m_frame; }

    SketchGeometry& geometry() { return m_geo; }
    const SketchGeometry& geometry() const { return m_geo; }

    // Bequeme Helfer: Rechteck (achsparallel, bemasst) und Kreis (bemasst)
    RectangleRefs addRectangle(double x, double y, double w, double h);
    int addCircle(double cx, double cy, double r); // liefert Index der Durchmesser-Bemassung

    bool isEmpty() const;
    bool hasCenterPoints() const;

    // Sichtbarkeit im 3D-Fenster: Auto = sichtbar, solange kein Feature sie verbraucht
    enum class Visibility { Auto, Shown, Hidden };
    Visibility visibility() const { return m_visibility; }
    void setVisibility(Visibility v) { m_visibility = v; }

    // Geschlossene Profilflaechen (Konstruktionslinien zaehlen nicht).
    // Ineinanderliegende Konturen werden abwechselnd als Material und Loch gewertet.
    // Sind Konturen verzweigt (z. B. Linie quer durch ein Rechteck), werden wie in
    // Inventor die einzelnen Bereiche gebildet; offene Linien zaehlen nicht.
    // picks: Punkte in den gewuenschten Bereichen (leer = Standardauswahl)
    TopoDS_Shape profile(const std::vector<gp_Pnt>& picks = {}) const;

    // Einzelne geschlossene Bereiche der Skizze (fuer die Profilauswahl).
    // level: Zahl der Kanten, die man vom Aussenraum bis zum Bereich kreuzt
    // (ungerade = gehoert zur Standardauswahl).
    struct Region { TopoDS_Face face; int level = 1; };
    std::vector<Region> regions() const;

    // Kanten der Skizze zur Anzeige im 3D-Fenster.
    TopoDS_Shape wireframe() const;

private:
    TopoDS_Shape simpleProfile() const;

    SketchPlane m_planeType;
    double m_offset;
    gp_Ax3 m_frame; // nur fuer SketchPlane::Custom
    Visibility m_visibility = Visibility::Auto;
    SketchGeometry m_geo;
};

using SketchPtr = std::shared_ptr<Sketch>;

const char* planeName(SketchPlane p);

} // namespace fw
