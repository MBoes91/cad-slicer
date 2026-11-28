#pragma once

#include "core/Features.h"
#include "core/ModifyFeatures.h"
#include "core/ImportFeature.h"
#include "core/PrintModifier.h"
#include "core/Split.h"

#include <string>
#include <vector>

namespace fw {

struct UserParameter {
    std::string name, expr, comment;
    double value = 0;
};

// Eintrag der Parametertabelle (fx)
struct ParameterInfo {
    std::string name, expr, unit, owner, comment;
    double value = 0;
    bool user = false;
};

// Ein Bauteil: geordnete Liste von Features, aus der der Koerper
// parametrisch neu berechnet wird.
class PartDocument {
public:
    explicit PartDocument(std::string name = "Bauteil1");

    const std::string& name() const { return m_name; }
    const std::vector<FeaturePtr>& features() const { return m_features; }

    std::vector<PrintModifier>& modifiers() { return m_modifiers; }
    const std::vector<PrintModifier>& modifiers() const { return m_modifiers; }

    // Aufteilung fuer den Druck (nur 3D-Druck-Umgebung, aendert den Koerper nicht)
    SplitPlan& splitPlan() { return m_split; }
    const SplitPlan& splitPlan() const { return m_split; }

    void addFeature(FeaturePtr f);
    // Entfernt ein Feature. Features, die noch von anderen benutzt werden
    // (Skizzen, Muster-Quellen), werden nicht entfernt (Rueckgabe false).
    bool removeFeature(const FeaturePtr& f);
    bool isUsed(const SketchPtr& s) const;
    // true, wenn ein anderes Feature dieses benutzt (Skizze, Muster-Quelle ...)
    bool isUsed(const Feature* f) const;

    // Bauteilende: Anzahl der wirksamen Features (alles danach wird nicht berechnet).
    // Neue Features werden am Bauteilende eingefuegt.
    int endOfPart() const;
    void setEndOfPart(int count);
    bool isAfterEndOfPart(const FeaturePtr& f) const;

    // Umbenennen mit Pruefung auf eindeutige Namen
    bool rename(const FeaturePtr& f, const std::string& name);

    // Parameter (fx): jede Bemassung und jeder Feature-Wert ist ein Modellparameter
    // (d0, d1, ...), dazu kommen Benutzerparameter. Gleichungen duerfen alle
    // Parameter verwenden; recompute() wertet sie aus.
    std::vector<UserParameter>& userParameters() { return m_userParams; }
    const std::vector<UserParameter>& userParameters() const { return m_userParams; }
    std::vector<ParameterInfo> parameters() const;
    // Setzt Gleichung (oder Zahl) eines Modellparameters; false bei unbekanntem Namen
    bool setModelExpression(const std::string& name, const std::string& expr);
    // Wertet einen Ausdruck mit den aktuellen Parameterwerten aus
    bool evaluateExpression(const std::string& expr, double& out, std::string* error = nullptr) const;
    void assignParameterNames();
    // Naechste freie Nummer fuer "d<N>" und Wert eines Parameters
    int nextParameterIndex() const;
    bool parameterValue(const std::string& name, double& value) const;

    // Berechnet den Koerper neu. Gibt false zurueck, wenn ein Feature
    // fehlschlaegt; errorFeature/errorMessage nennen dann die Ursache.
    bool recompute();
    // Koerper unmittelbar vor einem Feature (z. B. fuer die Kantenauswahl einer Rundung)
    TopoDS_Shape bodyBefore(const FeaturePtr& f) const;

    const TopoDS_Shape& body() const { return m_body; }
    const FeaturePtr& errorFeature() const { return m_errorFeature; }
    const std::string& errorMessage() const { return m_errorMessage; }

    // Eindeutiger Name wie "Skizze3" oder "Extrusion2".
    std::string nextName(const std::string& prefix) const;

    bool exportStl(const std::string& path) const;
    bool exportStep(const std::string& path) const { return fw::exportStep(m_body, path); }

    // Dateiformat .fwpart (Text, zeilenweise). fromText ersetzt den Inhalt.
    // embedImports: importierte Koerper vollstaendig einbetten (zum Speichern);
    // sonst nur ihre Kennung (fuer Rueckgaengig, im selben Programmlauf)
    std::string toText(bool embedImports = false) const;
    bool fromText(const std::string& text, std::string* error = nullptr);
    void clear(std::string name = "Bauteil1");
    void setName(std::string name) { m_name = std::move(name); }

private:
    struct Slot {
        ParamRef* ref;
        double* value;
        bool angle;
        Sketch* sketch;
        std::string owner;
    };
    std::vector<Slot> paramSlots() const;
    bool updateParameters(std::string& error);

    std::string m_name;
    std::vector<UserParameter> m_userParams;
    std::vector<FeaturePtr> m_features;
    std::vector<PrintModifier> m_modifiers;
    SplitPlan m_split;
    int m_endOfPart = -1; // -1: ganz am Ende
    TopoDS_Shape m_body;
    FeaturePtr m_errorFeature;
    std::string m_errorMessage;
};

} // namespace fw
