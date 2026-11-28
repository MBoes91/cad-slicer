#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Vec.hxx>

#include <string>
#include <vector>

namespace fw {

// Schnitt durch das Bauteil: Ebene senkrecht zur Achse (0 = X, 1 = Y, 2 = Z) bei pos.
struct SplitCut {
    int axis = 0;
    double pos = 0;
};

enum class SplitConnector { None, Pins, Peg, Dovetail, Screw };
const char* connectorName(SplitConnector c);

// Aufteilung eines zu grossen Bauteils fuer den Druck. Das Bauteil selbst bleibt
// unveraendert; die Teile entstehen nur in der 3D-Druck-Umgebung.
struct SplitPlan {
    std::vector<SplitCut> cuts;
    SplitConnector connector = SplitConnector::Pins;
    double diameter = 5.0;   // Stift-/Zapfendurchmesser, Schwalbenschwanzbreite, Schraube (M-Groesse)
    double length = 10.0;    // Stiftlaenge, Zapfenlaenge, Schwalbenschwanztiefe, Schraubenlaenge
    double clearance = 0.2;  // Spiel je Seite
    double margin = 3.0;     // Mindestabstand zum Rand der Schnittflaeche
    int count = 0;           // Verbinder je Schnittflaeche, 0 = automatisch
    bool printPins = true;   // Passstifte als eigenes Druckteil

    bool empty() const { return cuts.empty(); }
    // Standardmasse beim Wechsel der Verbindungsart
    void setDefaults(SplitConnector c);
};

struct SplitPart {
    std::string name;
    TopoDS_Shape shape;
    gp_Vec explode;       // Richtung fuer die Explosionsansicht (Vielfache des Abstands)
    bool pins = false;    // Druckteil mit den Passstiften
};

struct SplitResult {
    bool ok = false;
    std::string error;
    std::vector<SplitPart> parts;
    std::vector<std::string> warnings;
    int connectors = 0;
};

// Schnitte, mit denen jedes Teil in den Bauraum passt (gleichmaessig verteilt).
// reserve: Rand je Seite fuer Brim/Skirt
std::vector<SplitCut> autoCuts(const TopoDS_Shape& body, double bedX, double bedY, double maxZ,
                               const SplitPlan& plan, double reserve = 5.0);

SplitResult splitBody(const TopoDS_Shape& body, const SplitPlan& plan);

// Textform fuer die Bauteildatei ("cuts=0:100/1:50;conn=1;d=5;...")
std::string splitToText(const SplitPlan& plan);
bool splitFromText(const std::string& text, SplitPlan& plan);

} // namespace fw
