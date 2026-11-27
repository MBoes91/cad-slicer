#pragma once

#include <string>
#include <vector>

namespace fw {

// 2D-Skizzengeometrie mit geometrischen Abhaengigkeiten und Bemassungen.
// Punkt 0 ist immer der projizierte Ursprung (fest).

struct SkPoint {
    double x = 0, y = 0;
    bool center = false; // Mittelpunkt (Skizzenpunkt fuer Bohrungen), bleibt auch ohne Geometrie erhalten
};

enum class SkType { Line, Circle, Arc };

struct SkEntity {
    SkType type = SkType::Line;
    int p0 = -1, p1 = -1, p2 = -1; // Linie: p0-p1. Kreis: p0 = Mitte. Bogen: p0 = Mitte, p1 = Start, p2 = Ende (gegen Uhrzeigersinn)
    double r = 0;                   // nur Kreis (Bogen: Radius ergibt sich aus den Punkten)
    bool construction = false;
};

enum class ConType {
    Coincident,     // a, b: Punkte
    Horizontal,     // a: Linie
    Vertical,       // a: Linie
    Parallel,       // a, b: Linien
    Perpendicular,  // a, b: Linien
    Equal,          // a, b: Linien oder Kreise/Boegen
    Fix,            // a: Punkt (value, value2 = Lage)
    Tangent,        // a: Linie oder Kreis/Bogen, b: Kreis/Bogen
    PointOnEntity,  // a: Punkt, b: Linie oder Kreis/Bogen
    Midpoint,       // a: Punkt, b: Linie
    Distance,       // a, b: Punkte (Bemassung)
    DistanceX,      // a, b: Punkte, b.x - a.x (Bemassung)
    DistanceY,      // a, b: Punkte, b.y - a.y (Bemassung)
    Radius,         // a: Kreis/Bogen (Bemassung)
    Diameter,       // a: Kreis/Bogen (Bemassung)
    Angle           // a, b: Linien, Winkel in Grad (Bemassung)
};
const char* constraintName(ConType t);
bool isDimension(ConType t);

// Name und Gleichung eines Modellparameters, z. B. "d3" mit "d0 * 2" (leer = fester Wert)
struct ParamRef {
    std::string param;
    std::string expr;
};

struct SkConstraint {
    ConType type = ConType::Coincident;
    int a = -1, b = -1;
    double value = 0, value2 = 0;
    bool driving = true;          // false = getriebene Bemassung (nur Anzeige)
    double labelX = 0, labelY = 0; // Lage der Masszahl (Skizzenkoordinaten, relativ)
    ParamRef ref;                 // Modellparameter (nur Bemassungen)
};

enum class SolveStatus { Ok, Conflict, Failed };

struct SolveResult {
    SolveStatus status = SolveStatus::Ok;
    int dof = 0;            // verbleibende Freiheitsgrade (0 = vollstaendig bestimmt)
    double residual = 0;
};

class SketchGeometry {
public:
    SketchGeometry();

    std::vector<SkPoint> points;
    std::vector<SkEntity> entities;
    std::vector<SkConstraint> constraints;

    int addPoint(double x, double y);
    int addLine(int p0, int p1, bool construction = false);
    int addCircle(int center, double r, bool construction = false);
    int addArc(int center, int start, int end, bool construction = false);
    int addConstraint(const SkConstraint& c);

    // Loest das Gleichungssystem ausgehend vom aktuellen Zustand
    // (bleibt so nah wie moeglich an der aktuellen Lage).
    SolveResult solve();

    // Wie solve(), aber Punkt `dragPoint` soll moeglichst bei (x, y) landen.
    SolveResult drag(int dragPoint, double x, double y);

    // Prueft, ob eine neue Abhaengigkeit die Skizze ueberbestimmen wuerde.
    // Rueckgabe: true = kann hinzugefuegt werden.
    bool canAdd(const SkConstraint& c) const;

    // Aktueller Messwert einer Bemassung (z. B. fuer neue Bemassungen).
    double measure(const SkConstraint& c) const;

    double radius(int entity) const;

    // Entfernt Entitaet bzw. Punkt samt abhaengigen Abhaengigkeiten.
    void removeEntity(int e);
    void removeConstraint(int c);
    // Entfernt Punkte, die von nichts mehr benutzt werden (ausser Ursprung).
    void purgeUnusedPoints();

    int degreesOfFreedom() const;

private:
    std::vector<double> state() const;
    void setState(const std::vector<double>& x);
    std::vector<double> residuals(const std::vector<double>& x, const std::vector<SkConstraint>& cons) const;
    int rank(const std::vector<double>& x, const std::vector<SkConstraint>& cons) const;
    SolveResult solveImpl(int dragPoint, double dx, double dy);
};

} // namespace fw
