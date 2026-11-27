#pragma once

#include "core/SketchGeometry.h"

#include <TopoDS_Shape.hxx>

#include <memory>
#include <string>
#include <vector>

namespace fw {

enum class FeatureType { Sketch, Extrude, Revolve, Hole, Fillet, Chamfer, Pattern, Mirror };

// Ein Zahlenwert eines Features, der als Modellparameter (fx) erscheint
struct ParamSlot {
    std::string key;    // Schluessel in der Datei
    std::string label;  // Anzeige, z. B. "Durchmesser"
    ParamRef* ref;
    double* value;
    bool angle = false;
};

class Feature;

// Basisklasse fuer alle Eintraege im Feature-Baum (Modellbrowser).
// Jedes Feature wird bei der Neuberechnung der Reihe nach auf den
// bisherigen Koerper angewendet.
class Feature {
public:
    explicit Feature(std::string name) : m_name(std::move(name)) {}
    virtual ~Feature() = default;

    virtual FeatureType type() const = 0;

    // Wendet das Feature auf den bisherigen Koerper an und liefert den neuen.
    // Wirft Standard_Failure bzw. std::runtime_error bei ungueltiger Geometrie.
    virtual TopoDS_Shape apply(const TopoDS_Shape& body) const = 0;

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    // Zahlenwerte fuer die Parametertabelle
    virtual std::vector<ParamSlot> params() { return {}; }
    // true, wenn dieses Feature das andere (Skizze oder Feature) benutzt
    virtual bool uses(const Feature* other) const { (void)other; return false; }

    bool isSuppressed() const { return m_suppressed; }
    void setSuppressed(bool s) { m_suppressed = s; }

private:
    std::string m_name;
    bool m_suppressed = false;
};

using FeaturePtr = std::shared_ptr<Feature>;

enum class BooleanOp { Join, Cut, Intersect };

// Feature, das einen Werkzeugkoerper erzeugt und mit dem Koerper verknuepft
// (Extrusion, Drehung, Bohrung). Muster und Spiegeln vervielfaeltigen das Werkzeug.
class ToolFeature {
public:
    virtual ~ToolFeature() = default;
    virtual TopoDS_Shape tool(const TopoDS_Shape& body) const = 0;
    virtual BooleanOp toolOp() const = 0;
};

} // namespace fw
