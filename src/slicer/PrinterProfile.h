#pragma once

#include <string>
#include <vector>

namespace fw {

// Ereignis ab einer bestimmten Druckhoehe (wird vor der ersten Schicht
// ausgefuehrt, die vollstaendig ueber dieser Hoehe liegt).
enum class HeightEventType {
    Pause,          // Kopf parken, M0 (am Display fortsetzen)
    FilamentChange, // M600
    NozzleTemp,     // M104 S<value>
    BedTemp,        // M140 S<value>
    FanPercent,     // M106 S<value*2.55>
    SpeedPercent,   // M220 S<value>
    FlowPercent,    // M221 S<value>
    CustomGcode     // freier G-Code
};
const char* heightEventName(HeightEventType t);

struct HeightEvent {
    double z = 10.0;
    HeightEventType type = HeightEventType::Pause;
    double value = 0.0;
    std::string gcode;   // bei CustomGcode; bei Pause optional die Displaymeldung
};

enum class InfillPattern { Lines, Grid };
enum class SeamPosition { Nearest, Rear };
enum class SupportMode { None, Everywhere, BuildPlateOnly };

// Drucker- und Druckeinstellungen. Standardwerte: Creality Ender 3 V2
// (Marlin, Bowden-Extruder, 0,4-mm-Duese) mit PLA.
struct PrinterProfile {
    std::string name = "Ender 3 V2 - PLA Standard 0,2 mm";

    // Maschine
    double bedX = 220.0, bedY = 220.0, maxZ = 250.0;
    double nozzleDiameter = 0.4;
    std::string startGcode =
        "G28 ; Referenzfahrt\n"
        "G92 E0\n"
        "G1 Z2.0 F3000\n"
        "G1 X0.1 Y20 Z0.3 F5000.0 ; Reinigungslinie\n"
        "G1 X0.1 Y200.0 Z0.3 F1500.0 E15\n"
        "G1 X0.4 Y200.0 Z0.3 F5000.0\n"
        "G1 X0.4 Y20 Z0.3 F1500.0 E30\n"
        "G92 E0\n"
        "G1 Z2.0 F3000";
    std::string endGcode =
        "G91 ; relativ\n"
        "G1 E-2 F2700\n"
        "G1 E-2 Z0.2 F2400\n"
        "G1 X5 Y5 F3000\n"
        "G1 Z10\n"
        "G90 ; absolut\n"
        "G1 X0 Y{bed_y} ; Teil praesentieren\n"
        "M106 S0\n"
        "M104 S0\n"
        "M140 S0\n"
        "M84 X Y E";

    // Material
    double filamentDiameter = 1.75;
    double flowPercent = 100.0;
    double filamentDensity = 1.24;    // g/cm3
    double filamentCostPerKg = 20.0;  // EUR

    // Temperatur
    int nozzleTemp = 200;
    int nozzleTempFirstLayer = 205;
    int bedTemp = 60;
    int bedTempFirstLayer = 60;

    // Kuehlung
    int fanPercent = 100;
    int fanFromLayer = 2;          // 1-basiert

    // Qualitaet
    double layerHeight = 0.2;
    double firstLayerHeight = 0.2;
    double lineWidth = 0.4;

    // Waende
    int perimeters = 2;
    bool outerWallFirst = false;
    int seamPosition = static_cast<int>(SeamPosition::Nearest);

    // Deck-/Bodenschichten
    int topLayers = 4;
    int bottomLayers = 4;

    // Fuellung
    double infillDensity = 0.20;   // 0..1
    int infillPattern = static_cast<int>(InfillPattern::Lines);
    double infillAngle = 45.0;
    double infillOverlap = 0.15;   // Anteil der Linienbreite

    // Geschwindigkeiten in mm/s
    double printSpeed = 50.0;      // Fuellung
    double innerWallSpeed = 45.0;
    double outerWallSpeed = 30.0;
    double topBottomSpeed = 40.0;
    double firstLayerSpeed = 20.0;
    double travelSpeed = 150.0;

    // Rueckzug (Bowden)
    double retractLength = 5.0;
    double retractSpeed = 45.0;
    double retractMinTravel = 1.5;
    double zHop = 0.0;

    // Haftung
    int skirtLoops = 2;
    double skirtDistance = 5.0;
    double brimWidth = 0.0;

    // Stuetzen
    int supportMode = static_cast<int>(SupportMode::None);
    double supportAngle = 50.0;        // Grad von der Senkrechten
    double supportDensity = 0.15;      // 0..1
    double supportXYDistance = 0.8;    // mm
    int supportZGapLayers = 1;
    double supportSpeed = 50.0;
    double supportMinArea = 1.0;       // mm2

    // Pause / Filamentwechsel
    double pauseParkX = 0.0, pauseParkY = 200.0, pauseLiftZ = 10.0;

    // Hoehen-Ereignisse
    std::vector<HeightEvent> events;
};

// Mitgelieferte Profile fuer den Ender 3 V2.
std::vector<PrinterProfile> builtinProfiles();

} // namespace fw
