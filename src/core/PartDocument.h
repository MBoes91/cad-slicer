#pragma once

#include "core/Features.h"

#include <string>
#include <vector>

namespace fw {

// Ein Bauteil: geordnete Liste von Features, aus der der Koerper
// parametrisch neu berechnet wird.
class PartDocument {
public:
    explicit PartDocument(std::string name = "Bauteil1");

    const std::string& name() const { return m_name; }
    const std::vector<FeaturePtr>& features() const { return m_features; }

    void addFeature(FeaturePtr f);
    // Entfernt ein Feature. Skizzen, die noch von Features benutzt werden,
    // werden nicht entfernt (Rueckgabe false).
    bool removeFeature(const FeaturePtr& f);
    bool isUsed(const SketchPtr& s) const;

    // Berechnet den Koerper neu. Gibt false zurueck, wenn ein Feature
    // fehlschlaegt; errorFeature/errorMessage nennen dann die Ursache.
    bool recompute();

    const TopoDS_Shape& body() const { return m_body; }
    const FeaturePtr& errorFeature() const { return m_errorFeature; }
    const std::string& errorMessage() const { return m_errorMessage; }

    // Eindeutiger Name wie "Skizze3" oder "Extrusion2".
    std::string nextName(const std::string& prefix) const;

    bool exportStl(const std::string& path) const;

private:
    std::string m_name;
    std::vector<FeaturePtr> m_features;
    TopoDS_Shape m_body;
    FeaturePtr m_errorFeature;
    std::string m_errorMessage;
};

} // namespace fw
