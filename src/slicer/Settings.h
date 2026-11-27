#pragma once

#include "slicer/PrinterProfile.h"

#include <string>
#include <vector>

namespace fw {

enum class SettingKind { Double, Int, Bool, Choice, Text };

// Beschreibung einer Einstellung. Daraus werden der Einstellungsdialog
// und das Dateiformat der Profile automatisch erzeugt.
struct SettingDef {
    const char* key;
    const char* group;
    const char* label;
    SettingKind kind;
    double min = 0, max = 0;
    const char* unit = "";
    bool expert = false;
    const char* tooltip = "";
    std::vector<const char*> choices = {};
    double PrinterProfile::* d = nullptr;
    int PrinterProfile::* i = nullptr;
    bool PrinterProfile::* b = nullptr;
    std::string PrinterProfile::* s = nullptr;
    double displayScale = 1.0; // z. B. 100 fuer Anteil -> Prozent
};

const std::vector<SettingDef>& settingDefs();
std::vector<std::string> settingGroups();

// Profil als Textdatei (key = value, je Zeile).
std::string serializeProfile(const PrinterProfile& p);
bool deserializeProfile(const std::string& text, PrinterProfile& p, std::string* error = nullptr);

} // namespace fw
