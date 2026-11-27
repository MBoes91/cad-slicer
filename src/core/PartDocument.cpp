#include "core/PartDocument.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>

#include <algorithm>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <exception>

namespace fw {

PartDocument::PartDocument(std::string name) : m_name(std::move(name)) {}

void PartDocument::addFeature(FeaturePtr f) { m_features.push_back(std::move(f)); }

bool PartDocument::isUsed(const SketchPtr& s) const
{
    for (const auto& f : m_features) {
        if (auto e = std::dynamic_pointer_cast<ExtrudeFeature>(f); e && e->sketch() == s) return true;
        if (auto r = std::dynamic_pointer_cast<RevolveFeature>(f); r && r->sketch() == s) return true;
    }
    return false;
}

bool PartDocument::removeFeature(const FeaturePtr& f)
{
    if (auto s = std::dynamic_pointer_cast<Sketch>(f); s && isUsed(s)) return false;
    auto it = std::find(m_features.begin(), m_features.end(), f);
    if (it == m_features.end()) return false;
    m_features.erase(it);
    return true;
}

bool PartDocument::recompute()
{
    m_errorFeature.reset();
    m_errorMessage.clear();
    TopoDS_Shape body;
    for (const auto& f : m_features) {
        if (f->isSuppressed()) continue;
        try {
            body = f->apply(body);
        } catch (const Standard_Failure& e) {
            m_errorFeature = f;
            m_errorMessage = e.GetMessageString() ? e.GetMessageString() : "Geometriefehler";
        } catch (const std::exception& e) {
            m_errorFeature = f;
            m_errorMessage = e.what();
        }
        if (m_errorFeature) break;
    }
    m_body = body;
    return !m_errorFeature;
}

std::string PartDocument::nextName(const std::string& prefix) const
{
    for (int i = 1;; ++i) {
        const std::string candidate = prefix + std::to_string(i);
        bool taken = std::any_of(m_features.begin(), m_features.end(),
                                 [&](const FeaturePtr& f) { return f->name() == candidate; });
        if (!taken) return candidate;
    }
}

bool PartDocument::exportStl(const std::string& path) const
{
    if (m_body.IsNull()) return false;
    BRepMesh_IncrementalMesh mesh(m_body, 0.01, false, 0.2, true);
    StlAPI_Writer writer;
    writer.ASCIIMode() = false;
    return writer.Write(m_body, path.c_str());
}

namespace {

std::string safeName(std::string s)
{
    for (char& c : s) if (c == ';' || c == '\n' || c == '\r' || c == '=') c = '_';
    return s;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, sep)) out.push_back(cur);
    return out;
}

double num(const std::string& v)
{
    std::istringstream is(v);
    is.imbue(std::locale::classic());
    double d;
    if (!(is >> d)) throw std::invalid_argument("Zahl erwartet: " + v);
    return d;
}

// "key=value"-Paare ab Index 1
std::map<std::string, std::string> attrs(const std::vector<std::string>& parts)
{
    std::map<std::string, std::string> m;
    for (size_t i = 1; i < parts.size(); ++i) {
        const auto eq = parts[i].find('=');
        if (eq != std::string::npos) m[parts[i].substr(0, eq)] = parts[i].substr(eq + 1);
    }
    return m;
}

std::string need(const std::map<std::string, std::string>& m, const char* key)
{
    auto it = m.find(key);
    if (it == m.end()) throw std::invalid_argument(std::string("Angabe fehlt: ") + key);
    return it->second;
}

} // namespace

void PartDocument::clear(std::string name)
{
    m_name = std::move(name);
    m_features.clear();
    m_body.Nullify();
    m_errorFeature.reset();
    m_errorMessage.clear();
}

std::string PartDocument::toText() const
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(12);
    o << "# FormWerk-Bauteil\nformat = 1\npart = " << safeName(m_name) << '\n';
    for (const auto& f : m_features) {
        const int sup = f->isSuppressed() ? 1 : 0;
        if (auto s = std::dynamic_pointer_cast<Sketch>(f)) {
            o << "sketch = " << safeName(s->name()) << ";plane=" << static_cast<int>(s->planeType())
              << ";offset=" << s->offset() << ";suppressed=" << sup << '\n';
            for (const auto& r : s->rectangles())
                o << "rect = " << r.x << ';' << r.y << ';' << r.width << ';' << r.height << '\n';
            for (const auto& c : s->circles())
                o << "circle = " << c.cx << ';' << c.cy << ';' << c.radius << '\n';
        } else if (auto e = std::dynamic_pointer_cast<ExtrudeFeature>(f)) {
            o << "extrude = " << safeName(e->name()) << ";sketch=" << safeName(e->sketch()->name())
              << ";distance=" << e->distance() << ";op=" << static_cast<int>(e->operation())
              << ";symmetric=" << (e->symmetric() ? 1 : 0) << ";suppressed=" << sup << '\n';
        } else if (auto r = std::dynamic_pointer_cast<RevolveFeature>(f)) {
            o << "revolve = " << safeName(r->name()) << ";sketch=" << safeName(r->sketch()->name())
              << ";axis=" << static_cast<int>(r->axis()) << ";angle=" << r->angle()
              << ";op=" << static_cast<int>(r->operation()) << ";suppressed=" << sup << '\n';
        }
    }
    return o.str();
}

bool PartDocument::fromText(const std::string& text, std::string* error)
{
    std::string name = "Bauteil1";
    std::vector<FeaturePtr> features;
    std::map<std::string, SketchPtr> sketchesByName;
    SketchPtr current;

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    try {
        while (std::getline(in, line)) {
            ++lineNo;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find(" = ");
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const std::string val = line.substr(eq + 3);
            const auto parts = split(val, ';');
            if (key == "format") {
                if (num(val) > 1) throw std::invalid_argument("Datei stammt aus einer neueren Version");
            } else if (key == "part") {
                name = val;
            } else if (key == "sketch") {
                const auto a = attrs(parts);
                current = std::make_shared<Sketch>(parts.at(0), static_cast<SketchPlane>(static_cast<int>(num(need(a, "plane")))),
                                                   num(need(a, "offset")));
                current->setSuppressed(a.count("suppressed") && a.at("suppressed") == "1");
                sketchesByName[current->name()] = current;
                features.push_back(current);
            } else if (key == "rect" || key == "circle") {
                if (!current) throw std::invalid_argument("Element ohne Skizze");
                if (key == "rect" && parts.size() >= 4)
                    current->rectangles().push_back({num(parts[0]), num(parts[1]), num(parts[2]), num(parts[3])});
                else if (key == "circle" && parts.size() >= 3)
                    current->circles().push_back({num(parts[0]), num(parts[1]), num(parts[2])});
                else
                    throw std::invalid_argument("Unvollständiges Element");
            } else if (key == "extrude" || key == "revolve") {
                const auto a = attrs(parts);
                auto it = sketchesByName.find(need(a, "sketch"));
                if (it == sketchesByName.end()) throw std::invalid_argument("Unbekannte Skizze: " + need(a, "sketch"));
                const auto op = static_cast<BooleanOp>(static_cast<int>(num(need(a, "op"))));
                FeaturePtr f;
                if (key == "extrude")
                    f = std::make_shared<ExtrudeFeature>(parts.at(0), it->second, num(need(a, "distance")), op,
                                                         a.count("symmetric") && a.at("symmetric") == "1");
                else
                    f = std::make_shared<RevolveFeature>(parts.at(0), it->second,
                                                         static_cast<RevolveAxis>(static_cast<int>(num(need(a, "axis")))),
                                                         num(need(a, "angle")), op);
                f->setSuppressed(a.count("suppressed") && a.at("suppressed") == "1");
                features.push_back(f);
            }
        }
    } catch (const std::exception& e) {
        if (error) *error = "Zeile " + std::to_string(lineNo) + ": " + e.what();
        return false;
    }
    clear(name);
    m_features = std::move(features);
    return true;
}

} // namespace fw
