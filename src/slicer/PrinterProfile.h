#pragma once

#include <string>

namespace fw {

// Drucker- und Druckeinstellungen. Standardwerte: Creality Ender 3 V2
// (Marlin, Bowden-Extruder, 0,4-mm-Duese) mit PLA.
struct PrinterProfile {
    std::string name = "Creality Ender 3 V2";

    // Maschine
    double bedX = 220.0, bedY = 220.0, maxZ = 250.0;
    double nozzleDiameter = 0.4;
    double filamentDiameter = 1.75;

    // Material (PLA)
    int nozzleTemp = 200;
    int bedTemp = 60;
    int fanPercent = 100;
    int fanFromLayer = 2;          // Luefter ab Schicht 2 (1-basiert)

    // Qualitaet
    double layerHeight = 0.2;
    double firstLayerHeight = 0.2;
    double lineWidth = 0.4;
    int perimeters = 2;
    int topLayers = 4;
    int bottomLayers = 4;
    double infillDensity = 0.20;   // 0..1
    double infillOverlap = 0.15;   // Anteil der Linienbreite

    // Geschwindigkeiten in mm/s
    double printSpeed = 50.0;
    double outerWallSpeed = 30.0;
    double firstLayerSpeed = 20.0;
    double travelSpeed = 150.0;

    // Rueckzug (Bowden)
    double retractLength = 5.0;
    double retractSpeed = 45.0;
    double retractMinTravel = 1.5;
    double zHop = 0.0;

    // Skirt
    int skirtLoops = 2;
    double skirtDistance = 5.0;
};

} // namespace fw
