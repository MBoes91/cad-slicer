#pragma once

#include <functional>
#include <string>
#include <vector>

namespace fw {

// Liefert den Wert eines Parameters; false, wenn der Name unbekannt ist.
using ParamLookup = std::function<bool(const std::string& name, double& value)>;

// Wertet einen Ausdruck wie "d0 * 2 + 5 mm" oder "Breite / 2" aus.
// Erlaubt: + - * / ^, Klammern, Zahlen mit Punkt oder Komma, Einheiten
// mm, cm, m, in, grd/deg/°, Funktionen sin cos tan (Grad), sqrt abs round
// floor ceil, min/max (Argumente mit ';' getrennt), Konstante PI.
// Wirft std::runtime_error mit einer deutschen Meldung.
double evaluate(const std::string& expr, const ParamLookup& lookup);

// Namen von Parametern, die im Ausdruck vorkommen.
std::vector<std::string> referencedNames(const std::string& expr);

// true fuer eine reine Zahl mit optionaler Einheit, z. B. "12,5 mm".
bool isPlainNumber(const std::string& expr);

// Gueltiger Parametername: Buchstabe am Anfang, dann Buchstaben, Ziffern, '_'.
bool isValidParamName(const std::string& name);

} // namespace fw
