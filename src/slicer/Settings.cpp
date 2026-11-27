#include "slicer/Settings.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <stdexcept>
#include <sstream>

namespace fw {

const char* heightEventName(HeightEventType t)
{
    switch (t) {
    case HeightEventType::Pause: return "Pause";
    case HeightEventType::FilamentChange: return "Filamentwechsel (M600)";
    case HeightEventType::NozzleTemp: return "Düsentemperatur";
    case HeightEventType::BedTemp: return "Betttemperatur";
    case HeightEventType::FanPercent: return "Lüfter";
    case HeightEventType::SpeedPercent: return "Geschwindigkeit";
    case HeightEventType::FlowPercent: return "Fluss";
    case HeightEventType::CustomGcode: return "Eigener G-Code";
    }
    return "?";
}

std::vector<PrinterProfile> builtinProfiles()
{
    std::vector<PrinterProfile> out;
    PrinterProfile standard;
    out.push_back(standard);

    PrinterProfile fine = standard;
    fine.name = "Ender 3 V2 - PLA Fein 0,12 mm";
    fine.layerHeight = 0.12;
    fine.topLayers = 6;
    fine.bottomLayers = 5;
    fine.printSpeed = 40;
    fine.innerWallSpeed = 35;
    fine.outerWallSpeed = 25;
    out.push_back(fine);

    PrinterProfile fast = standard;
    fast.name = "Ender 3 V2 - PLA Schnell 0,28 mm";
    fast.layerHeight = 0.28;
    fast.firstLayerHeight = 0.28;
    fast.topLayers = 3;
    fast.bottomLayers = 3;
    fast.infillDensity = 0.15;
    fast.printSpeed = 70;
    fast.innerWallSpeed = 60;
    fast.outerWallSpeed = 40;
    out.push_back(fast);

    PrinterProfile petg = standard;
    petg.name = "Ender 3 V2 - PETG Standard 0,2 mm";
    petg.nozzleTemp = 235;
    petg.nozzleTempFirstLayer = 240;
    petg.bedTemp = 75;
    petg.bedTempFirstLayer = 80;
    petg.fanPercent = 40;
    petg.fanFromLayer = 3;
    petg.printSpeed = 40;
    petg.innerWallSpeed = 35;
    petg.outerWallSpeed = 25;
    petg.retractLength = 6.0;
    petg.retractSpeed = 40;
    petg.filamentDensity = 1.27;
    out.push_back(petg);
    return out;
}

const std::vector<SettingDef>& settingDefs()
{
    using P = PrinterProfile;
    auto D = [](const char* key, const char* group, const char* label, double P::* m, double mn, double mx,
                const char* unit, bool expert = false, const char* tip = "", double scale = 1.0) {
        SettingDef s{key, group, label, SettingKind::Double, mn, mx, unit, expert, tip};
        s.d = m;
        s.displayScale = scale;
        return s;
    };
    auto I = [](const char* key, const char* group, const char* label, int P::* m, double mn, double mx,
                const char* unit, bool expert = false, const char* tip = "") {
        SettingDef s{key, group, label, SettingKind::Int, mn, mx, unit, expert, tip};
        s.i = m;
        return s;
    };
    auto B = [](const char* key, const char* group, const char* label, bool P::* m, bool expert = false,
                const char* tip = "") {
        SettingDef s{key, group, label, SettingKind::Bool, 0, 1, "", expert, tip};
        s.b = m;
        return s;
    };
    auto C = [](const char* key, const char* group, const char* label, int P::* m,
                std::vector<const char*> choices, bool expert = false, const char* tip = "") {
        SettingDef s{key, group, label, SettingKind::Choice, 0, static_cast<double>(choices.size() - 1), "", expert, tip, choices};
        s.i = m;
        return s;
    };
    auto T = [](const char* key, const char* group, const char* label, std::string P::* m, bool expert = true,
                const char* tip = "") {
        SettingDef s{key, group, label, SettingKind::Text, 0, 0, "", expert, tip};
        s.s = m;
        return s;
    };

    static const std::vector<SettingDef> defs = {
        D("layer_height", "Qualität", "Schichthöhe", &P::layerHeight, 0.04, 0.32, "mm", false,
          "Höhe jeder Schicht. Kleiner = feiner, aber langsamer. Für 0,4-mm-Düse sinnvoll: 0,08 bis 0,28 mm."),
        D("first_layer_height", "Qualität", "Höhe erste Schicht", &P::firstLayerHeight, 0.08, 0.4, "mm", false,
          "Eine etwas dickere erste Schicht haftet besser und gleicht Bettunebenheiten aus."),
        D("line_width", "Qualität", "Linienbreite", &P::lineWidth, 0.2, 0.8, "mm", true,
          "Breite einer extrudierten Linie. Meist gleich dem Düsendurchmesser."),

        I("perimeters", "Wände", "Anzahl Wandlinien", &P::perimeters, 1, 20, "", false,
          "Mehr Wände machen das Teil stabiler, besonders bei Schraubenlöchern."),
        B("outer_wall_first", "Wände", "Außenwand zuerst", &P::outerWallFirst, true,
          "Bessere Maßhaltigkeit außen, dafür schlechtere Überhänge."),
        C("seam_position", "Wände", "Z-Naht", &P::seamPosition, {"Kürzester Weg", "Hinten"}, true,
          "Wo jede Wandlinie beginnt. \"Hinten\" versteckt die Naht auf der Rückseite."),

        I("top_layers", "Deck-/Bodenschichten", "Deckschichten", &P::topLayers, 0, 50, "", false,
          "Anzahl geschlossener Schichten oben."),
        I("bottom_layers", "Deck-/Bodenschichten", "Bodenschichten", &P::bottomLayers, 0, 50, "", false,
          "Anzahl geschlossener Schichten unten."),
        D("top_bottom_speed", "Deck-/Bodenschichten", "Geschwindigkeit Deck/Boden", &P::topBottomSpeed, 5, 200, "mm/s", true),

        D("infill_density", "Füllung", "Fülldichte", &P::infillDensity, 0, 1, "%", false,
          "0 % = hohl, 100 % = massiv. Für Gebrauchsteile 20 bis 40 %.", 100.0),
        C("infill_pattern", "Füllung", "Muster", &P::infillPattern, {"Linien", "Gitter"}, false,
          "Linien: schnell, Richtung wechselt je Schicht. Gitter: beide Richtungen in jeder Schicht, stabiler."),
        D("infill_angle", "Füllung", "Winkel", &P::infillAngle, -90, 90, "°", true),
        D("infill_overlap", "Füllung", "Überlappung mit Wand", &P::infillOverlap, 0, 0.5, "%", true,
          "Wie weit die Füllung in die Wände ragt, damit sie gut verbunden ist.", 100.0),
        D("infill_speed", "Füllung", "Geschwindigkeit Füllung", &P::printSpeed, 5, 200, "mm/s"),

        D("outer_wall_speed", "Geschwindigkeit", "Außenwand", &P::outerWallSpeed, 5, 200, "mm/s"),
        D("inner_wall_speed", "Geschwindigkeit", "Innenwände", &P::innerWallSpeed, 5, 200, "mm/s"),
        D("first_layer_speed", "Geschwindigkeit", "Erste Schicht", &P::firstLayerSpeed, 5, 100, "mm/s"),
        D("travel_speed", "Geschwindigkeit", "Leerfahrt", &P::travelSpeed, 20, 300, "mm/s", true),

        I("nozzle_temp", "Temperatur", "Düse", &P::nozzleTemp, 150, 260, "°C", false,
          "Der Ender 3 V2 mit PTFE-Schlauch sollte dauerhaft nicht über 240 °C laufen."),
        I("nozzle_temp_first_layer", "Temperatur", "Düse erste Schicht", &P::nozzleTempFirstLayer, 150, 260, "°C"),
        I("bed_temp", "Temperatur", "Bett", &P::bedTemp, 0, 100, "°C"),
        I("bed_temp_first_layer", "Temperatur", "Bett erste Schicht", &P::bedTempFirstLayer, 0, 100, "°C"),

        I("fan_percent", "Kühlung", "Lüfterleistung", &P::fanPercent, 0, 100, "%"),
        I("fan_from_layer", "Kühlung", "Lüfter ab Schicht", &P::fanFromLayer, 1, 100, "", false,
          "Die ersten Schichten ohne Lüfter haften besser."),

        D("retract_length", "Rückzug", "Rückzugslänge", &P::retractLength, 0, 10, "mm", false,
          "Gegen Fäden. Ender 3 V2 (Bowden): 4 bis 6 mm."),
        D("retract_speed", "Rückzug", "Rückzugsgeschwindigkeit", &P::retractSpeed, 5, 100, "mm/s"),
        D("retract_min_travel", "Rückzug", "Rückzug ab Leerweg", &P::retractMinTravel, 0, 20, "mm", true),
        D("z_hop", "Rückzug", "Z-Sprung", &P::zHop, 0, 5, "mm", true,
          "Hebt die Düse bei Leerfahrten an, damit sie nicht über das Teil kratzt."),

        C("support_mode", "Stützen", "Stützstrukturen", &P::supportMode, {"Keine", "Überall", "Nur vom Druckbett"}, false,
          "\"Nur vom Druckbett\": keine Stützen, die auf dem Teil selbst stehen würden."),
        D("support_angle", "Stützen", "Überhangwinkel", &P::supportAngle, 0, 89, "°", false,
          "Flächen, die flacher als dieser Winkel (gemessen von der Senkrechten) überhängen, werden gestützt. Üblich: 45 bis 60°."),
        D("support_density", "Stützen", "Stützdichte", &P::supportDensity, 0.05, 1, "%", false,
          "Dichter = stabiler, aber schwerer zu entfernen.", 100.0),
        D("support_xy_distance", "Stützen", "Abstand zum Teil (seitlich)", &P::supportXYDistance, 0, 5, "mm", true),
        I("support_z_gap_layers", "Stützen", "Abstand zum Teil (Schichten oben)", &P::supportZGapLayers, 0, 5, "", true,
          "Luft zwischen Stütze und Teil, damit sich die Stütze leicht lösen lässt."),
        D("support_speed", "Stützen", "Geschwindigkeit Stützen", &P::supportSpeed, 5, 200, "mm/s", true),
        D("support_min_area", "Stützen", "Kleinste Stützfläche", &P::supportMinArea, 0, 50, "mm²", true,
          "Kleinere Überhangflächen werden nicht gestützt."),

        I("skirt_loops", "Haftung", "Skirt-Linien", &P::skirtLoops, 0, 20, "", false,
          "Linien um das Teil herum, um die Düse vorzufüllen."),
        D("skirt_distance", "Haftung", "Skirt-Abstand", &P::skirtDistance, 0, 50, "mm"),
        D("brim_width", "Haftung", "Brim-Breite", &P::brimWidth, 0, 30, "mm", false,
          "Ein breiter Rand direkt am Teil gegen Ablösen (Warping). 0 = aus."),

        D("filament_diameter", "Material", "Filamentdurchmesser", &P::filamentDiameter, 1.5, 3.0, "mm", true),
        D("flow_percent", "Material", "Fluss", &P::flowPercent, 50, 150, "%", false,
          "Multipliziert die Extrusionsmenge. Bei Über-/Unterextrusion anpassen."),
        D("filament_density", "Material", "Dichte", &P::filamentDensity, 0.5, 3.0, "g/cm³", true),
        D("filament_cost", "Material", "Preis", &P::filamentCostPerKg, 0, 200, "€/kg", true),

        D("bed_x", "Maschine", "Bettbreite (X)", &P::bedX, 50, 1000, "mm", true),
        D("bed_y", "Maschine", "Betttiefe (Y)", &P::bedY, 50, 1000, "mm", true),
        D("max_z", "Maschine", "Maximale Höhe (Z)", &P::maxZ, 50, 1000, "mm", true),
        D("nozzle_diameter", "Maschine", "Düsendurchmesser", &P::nozzleDiameter, 0.1, 1.2, "mm", true),
        D("pause_park_x", "Maschine", "Parkposition X (Pause)", &P::pauseParkX, 0, 1000, "mm", true),
        D("pause_park_y", "Maschine", "Parkposition Y (Pause)", &P::pauseParkY, 0, 1000, "mm", true),
        D("pause_lift_z", "Maschine", "Anheben bei Pause", &P::pauseLiftZ, 0, 100, "mm", true),
        T("start_gcode", "Maschine", "Start-G-Code", &P::startGcode, true,
          "Wird nach dem Aufheizen ausgeführt. Platzhalter: {nozzle_temp} {bed_temp} {bed_x} {bed_y}"),
        T("end_gcode", "Maschine", "End-G-Code", &P::endGcode, true,
          "Platzhalter: {nozzle_temp} {bed_temp} {bed_x} {bed_y}"),
    };
    return defs;
}

std::vector<std::string> settingGroups()
{
    std::vector<std::string> groups;
    for (const auto& d : settingDefs()) {
        bool known = false;
        for (const auto& g : groups) known = known || g == d.group;
        if (!known) groups.push_back(d.group);
    }
    return groups;
}

namespace {

std::string escape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') continue;
        else out += c;
    }
    return out;
}

std::string unescape(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out += s[i] == 'n' ? '\n' : s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// Locale-unabhaengig (Punkt als Dezimaltrennzeichen)
double toDouble(const std::string& v)
{
    std::istringstream is(v);
    is.imbue(std::locale::classic());
    double d;
    if (!(is >> d)) throw std::invalid_argument(v);
    return d;
}

int toInt(const std::string& v) { return static_cast<int>(std::lround(toDouble(v))); }

} // namespace

std::string serializeProfile(const PrinterProfile& p)
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(10);
    o << "# FormWerk-Druckprofil\n";
    o << "name = " << escape(p.name) << '\n';
    for (const auto& d : settingDefs()) {
        o << d.key << " = ";
        switch (d.kind) {
        case SettingKind::Double: o << p.*(d.d); break;
        case SettingKind::Int:
        case SettingKind::Choice: o << p.*(d.i); break;
        case SettingKind::Bool: o << (p.*(d.b) ? 1 : 0); break;
        case SettingKind::Text: o << escape(p.*(d.s)); break;
        }
        o << '\n';
    }
    for (const auto& e : p.events)
        o << "event = " << e.z << ';' << static_cast<int>(e.type) << ';' << e.value << ';' << escape(e.gcode) << '\n';
    return o.str();
}

bool deserializeProfile(const std::string& text, PrinterProfile& p, std::string* error)
{
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    p.events.clear();
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        try {
            if (key == "name") { p.name = unescape(val); continue; }
            if (key == "event") {
                HeightEvent e;
                std::istringstream es(val);
                std::string part;
                std::getline(es, part, ';'); e.z = toDouble(part);
                std::getline(es, part, ';'); e.type = static_cast<HeightEventType>(toInt(part));
                std::getline(es, part, ';'); e.value = toDouble(part);
                std::string rest;
                std::getline(es, rest);
                e.gcode = unescape(rest);
                p.events.push_back(e);
                continue;
            }
            for (const auto& d : settingDefs()) {
                if (key != d.key) continue;
                switch (d.kind) {
                case SettingKind::Double: p.*(d.d) = toDouble(val); break;
                case SettingKind::Int:
                case SettingKind::Choice: p.*(d.i) = toInt(val); break;
                case SettingKind::Bool: p.*(d.b) = val == "1" || val == "true"; break;
                case SettingKind::Text: p.*(d.s) = unescape(val); break;
                }
            }
        } catch (const std::exception&) {
            if (error) *error = "Ungültiger Wert in Zeile " + std::to_string(lineNo) + ": " + key;
            return false;
        }
    }
    return true;
}

} // namespace fw
