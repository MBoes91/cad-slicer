// Einfache Tests ohne Framework: Bauteil modellieren, slicen, G-Code pruefen.
#include "core/PartDocument.h"
#include "slicer/Slicer.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static double volume(const TopoDS_Shape& s)
{
    GProp_GProps p;
    BRepGProp::VolumeProperties(s, p);
    return p.Mass();
}

static void testExtrudeWithHole()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->rectangles().push_back({0, 0, 40, 30});
    sk->circles().push_back({20, 15, 5});
    doc.addFeature(sk);
    auto ex = std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 10.0);
    doc.addFeature(ex);
    CHECK(doc.recompute());
    const double expected = 40 * 30 * 10 - M_PI * 25 * 10;
    CHECK(std::abs(volume(doc.body()) - expected) < 1.0);

    // parametrisch aendern
    ex->setDistance(20.0);
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - 2 * expected) < 2.0);

    // Differenz-Extrusion von oben
    auto sk2 = std::make_shared<fw::Sketch>("Skizze2", fw::SketchPlane::XY, 20.0);
    sk2->rectangles().push_back({0, 0, 10, 30});
    doc.addFeature(sk2);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion2", sk2, -5.0, fw::BooleanOp::Cut));
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - (2 * expected - 10 * 30 * 5)) < 2.0);
    CHECK(!doc.removeFeature(sk));
}

static void testRevolve()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XZ);
    sk->rectangles().push_back({0, 0, 10, 20}); // Zylinder r=10, h=20 um Skizzen-Y (Welt-Z)
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::RevolveFeature>("Drehung1", sk, fw::RevolveAxis::SketchY));
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - M_PI * 100 * 20) < 5.0);
}

static void testSlice()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->rectangles().push_back({0, 0, 20, 20});
    sk->circles().push_back({10, 10, 4});
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 5.0));
    CHECK(doc.recompute());

    fw::PrinterProfile pp;
    fw::SliceResult r = fw::slice(doc.body(), pp);
    if (!r.ok) std::printf("Slicer-Fehler: %s\n", r.error.c_str());
    CHECK(r.ok);
    CHECK(r.layers.size() == 25);
    CHECK(r.gcode.find("M109 S200") != std::string::npos);
    CHECK(r.gcode.find("G28") != std::string::npos);
    CHECK(r.gcode.find(";LAYER:24") != std::string::npos);
    CHECK(r.filamentMm > 100 && r.filamentMm < 2000);

    // Alle Druckbewegungen innerhalb des Teils (mittig bei 110/110, +/-10 mm + Skirt)
    std::istringstream in(r.gcode);
    std::string line;
    bool inLayers = false;
    double minX = 1e9, maxX = -1e9;
    while (std::getline(in, line)) {
        if (line.rfind(";LAYER:", 0) == 0) inLayers = true;
        if (line.rfind(";Ende", 0) == 0) inLayers = false;
        if (!inLayers || line.rfind("G1 ", 0) != 0) continue;
        auto px = line.find(" X");
        if (px == std::string::npos) continue;
        double x = std::stod(line.substr(px + 2));
        minX = std::min(minX, x); maxX = std::max(maxX, x);
    }
    CHECK(minX > 110 - 10 - 7 && maxX < 110 + 10 + 7);

    // Zu grosses Teil wird abgelehnt
    sk->rectangles()[0].width = 300;
    CHECK(doc.recompute());
    CHECK(!fw::slice(doc.body(), pp).ok);
}

int main()
{
    testExtrudeWithHole();
    testRevolve();
    testSlice();
    if (g_failed) { std::printf("%d Pruefung(en) fehlgeschlagen\n", g_failed); return 1; }
    std::printf("Alle Tests bestanden\n");
    return 0;
}
