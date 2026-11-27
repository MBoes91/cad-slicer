#pragma once

#include <TopoDS_Shape.hxx>

#include <memory>
#include <string>

namespace fw {

enum class FeatureType { Sketch, Extrude, Revolve };

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

    bool isSuppressed() const { return m_suppressed; }
    void setSuppressed(bool s) { m_suppressed = s; }

private:
    std::string m_name;
    bool m_suppressed = false;
};

using FeaturePtr = std::shared_ptr<Feature>;

} // namespace fw
