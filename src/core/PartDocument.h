#pragma once

#include "core/Features.h"
#include "core/PrintModifier.h"

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

    std::vector<PrintModifier>& modifiers() { return m_modifiers; }
    const std::vector<PrintModifier>& modifiers() const { return m_modifiers; }

    void addFeature(FeaturePtr f);
    // Entfernt ein Feature. Skizzen, die noch von Features benutzt werden,
    // werden nicht entfernt (Rueckgabe false).
    bool removeFeature(const FeaturePtr& f);
    bool isUsed(const SketchPtr& s) const;

    // Bauteilende: Anzahl der wirksamen Features (alles danach wird nicht berechnet).
    // Neue Features werden am Bauteilende eingefuegt.
    int endOfPart() const;
    void setEndOfPart(int count);
    bool isAfterEndOfPart(const FeaturePtr& f) const;

    // Umbenennen mit Pruefung auf eindeutige Namen
    bool rename(const FeaturePtr& f, const std::string& name);

    // Berechnet den Koerper neu. Gibt false zurueck, wenn ein Feature
    // fehlschlaegt; errorFeature/errorMessage nennen dann die Ursache.
    bool recompute();

    const TopoDS_Shape& body() const { return m_body; }
    const FeaturePtr& errorFeature() const { return m_errorFeature; }
    const std::string& errorMessage() const { return m_errorMessage; }

    // Eindeutiger Name wie "Skizze3" oder "Extrusion2".
    std::string nextName(const std::string& prefix) const;

    bool exportStl(const std::string& path) const;

    // Dateiformat .fwpart (Text, zeilenweise). fromText ersetzt den Inhalt.
    std::string toText() const;
    bool fromText(const std::string& text, std::string* error = nullptr);
    void clear(std::string name = "Bauteil1");
    void setName(std::string name) { m_name = std::move(name); }

private:
    std::string m_name;
    std::vector<FeaturePtr> m_features;
    std::vector<PrintModifier> m_modifiers;
    int m_endOfPart = -1; // -1: ganz am Ende
    TopoDS_Shape m_body;
    FeaturePtr m_errorFeature;
    std::string m_errorMessage;
};

} // namespace fw
