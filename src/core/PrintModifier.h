#pragma once

#include <string>

namespace fw {

// Raumbereich, der beim Slicen besondere Regeln bekommt (in Bauteilkoordinaten).
struct PrintModifier {
    enum class Role { InfillZone, SupportEnforcer, SupportBlocker };
    enum class Shape { Box, Cylinder };

    std::string name = "Modifikator1";
    Role role = Role::InfillZone;
    Shape shape = Shape::Box;
    double cx = 0, cy = 0, cz = 0;   // Mittelpunkt
    double sx = 20, sy = 20, sz = 20; // Groesse (Zylinder: sx = Durchmesser, sz = Hoehe)

    // Nur fuer Infill-Zonen
    double infillDensity = 0.5;       // 0..1
    int infillPattern = 1;            // wie PrinterProfile::infillPattern
};

const char* modifierRoleName(PrintModifier::Role r);

} // namespace fw
