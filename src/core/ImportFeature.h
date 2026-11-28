#pragma once

#include "core/Feature.h"

#include <string>

namespace fw {

// Importierter Koerper (STEP oder STL). Die Geometrie wird im Bauteil gespeichert,
// die Originaldatei wird danach nicht mehr gebraucht.
class ImportFeature : public Feature {
public:
    // id leer: neue Kennung erzeugen. Die Geometrie wird unter der Kennung zwischengespeichert,
    // damit Rueckgaengig-Schritte sie nicht jedes Mal kopieren muessen.
    ImportFeature(std::string name, TopoDS_Shape shape, std::string source, std::string id = {});
    FeatureType type() const override { return FeatureType::Import; }
    TopoDS_Shape apply(const TopoDS_Shape& body) const override;

    const TopoDS_Shape& shape() const { return m_shape; }
    const std::string& source() const { return m_source; }
    const std::string& id() const { return m_id; }
    // Zwischengespeicherte Geometrie zu einer Kennung (Null, wenn unbekannt)
    static TopoDS_Shape cached(const std::string& id);

private:
    TopoDS_Shape m_shape;
    std::string m_source; // Dateiname, nur zur Anzeige
    std::string m_id;
};

// Liest STEP (.step/.stp) oder STL. Wirft std::runtime_error mit verstaendlicher Meldung.
TopoDS_Shape importShape(const std::string& path);
bool exportStep(const TopoDS_Shape& shape, const std::string& path);

// Koerper als Text (BRep, Base64) fuer das Dateiformat
std::string shapeToString(const TopoDS_Shape& s);
TopoDS_Shape shapeFromString(const std::string& s);

} // namespace fw
