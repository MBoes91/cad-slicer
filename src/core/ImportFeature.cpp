#include "core/ImportFeature.h"
#include "core/Features.h"

#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeFix_Solid.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <RWStl.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pln.hxx>
#include <map>
#include <vector>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace fw {

namespace {
std::map<std::string, TopoDS_Shape>& importCache()
{
    static std::map<std::string, TopoDS_Shape> cache;
    return cache;
}
} // namespace

ImportFeature::ImportFeature(std::string name, TopoDS_Shape shape, std::string source, std::string id)
    : Feature(std::move(name)), m_shape(std::move(shape)), m_source(std::move(source)), m_id(std::move(id))
{
    if (m_id.empty()) {
        static unsigned counter = 0;
        m_id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" + std::to_string(++counter);
    }
    importCache()[m_id] = m_shape;
}

TopoDS_Shape ImportFeature::cached(const std::string& id)
{
    auto it = importCache().find(id);
    return it == importCache().end() ? TopoDS_Shape() : it->second;
}

TopoDS_Shape ImportFeature::apply(const TopoDS_Shape& body) const
{
    if (m_shape.IsNull()) throw std::runtime_error("Importierter Koerper ist leer");
    return combine(body, m_shape, BooleanOp::Join);
}

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

TopoDS_Shape readStl(const std::string& path)
{
    // Netz lesen und daraus direkt eine Huelle mit gemeinsamen Kanten bauen
    // (deutlich schneller als nachtraegliches Vernaehen)
    Handle(Poly_Triangulation) tri = RWStl::ReadFile(path.c_str());
    if (tri.IsNull() || tri->NbTriangles() == 0) throw std::runtime_error("STL-Datei konnte nicht gelesen werden");
    const int nn = tri->NbNodes();
    std::vector<TopoDS_Vertex> verts(static_cast<size_t>(nn) + 1);
    BRep_Builder bb;
    for (int i = 1; i <= nn; ++i) bb.MakeVertex(verts[static_cast<size_t>(i)], tri->Node(i), 1e-6);
    std::map<std::pair<int, int>, TopoDS_Edge> edges;
    auto edge = [&](int a, int b) -> TopoDS_Edge {
        const auto key = std::minmax(a, b);
        auto it = edges.find(key);
        if (it == edges.end()) {
            const TopoDS_Edge e = BRepBuilderAPI_MakeEdge(verts[static_cast<size_t>(key.first)], verts[static_cast<size_t>(key.second)]);
            it = edges.emplace(key, e).first;
        }
        return a == key.first ? it->second : TopoDS::Edge(it->second.Reversed());
    };
    TopoDS_Shell shell;
    bb.MakeShell(shell);
    int faces = 0;
    for (int t = 1; t <= tri->NbTriangles(); ++t) {
        int n1, n2, n3;
        tri->Triangle(t).Get(n1, n2, n3);
        const gp_Pnt p1 = tri->Node(n1), p2 = tri->Node(n2), p3 = tri->Node(n3);
        const gp_Vec nrm = gp_Vec(p1, p2).Crossed(gp_Vec(p1, p3));
        if (n1 == n2 || n2 == n3 || n1 == n3 || nrm.Magnitude() < 1e-12) continue; // entartetes Dreieck
        TopoDS_Wire w;
        bb.MakeWire(w);
        bb.Add(w, edge(n1, n2));
        bb.Add(w, edge(n2, n3));
        bb.Add(w, edge(n3, n1));
        w.Closed(true);
        BRepBuilderAPI_MakeFace mf(gp_Pln(p1, gp_Dir(nrm)), w, true);
        if (!mf.IsDone()) continue;
        bb.Add(shell, mf.Face());
        ++faces;
    }
    if (faces == 0) throw std::runtime_error("STL-Datei enthaelt keine Dreiecke");
    shell.Closed(BRep_Tool::IsClosed(shell));
    TopoDS_Solid solid;
    bb.MakeSolid(solid);
    bb.Add(solid, shell);
    // Innen/aussen richtig ausrichten: bei negativem Volumen zeigen die Normalen nach innen
    GProp_GProps props;
    BRepGProp::VolumeProperties(solid, props);
    if (props.Mass() < 0) solid.Reverse();
    return solid;
}

TopoDS_Shape readStep(const std::string& path)
{
    STEPControl_Reader reader;
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone)
        throw std::runtime_error("STEP-Datei konnte nicht gelesen werden");
    reader.TransferRoots();
    TopoDS_Shape s = reader.OneShape();
    if (s.IsNull()) throw std::runtime_error("STEP-Datei enthaelt keine Geometrie");
    bool hasSolid = false;
    for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) hasSolid = true;
    if (!hasSolid) throw std::runtime_error("STEP-Datei enthaelt keinen Volumenkoerper");
    return s;
}

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64encode(const std::string& in)
{
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8)
                           | static_cast<unsigned char>(in[i + 2]);
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i < in.size()) {
        unsigned v = static_cast<unsigned char>(in[i]) << 16;
        if (i + 1 < in.size()) v |= static_cast<unsigned char>(in[i + 1]) << 8;
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63];
        out += i + 1 < in.size() ? kB64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string b64decode(const std::string& in)
{
    int map[256];
    std::fill(std::begin(map), std::end(map), -1);
    for (int i = 0; i < 64; ++i) map[static_cast<unsigned char>(kB64[i])] = i;
    std::string out;
    unsigned v = 0;
    int bits = 0;
    for (unsigned char c : in) {
        if (map[c] < 0) continue;
        v = (v << 6) | static_cast<unsigned>(map[c]);
        bits += 6;
        if (bits >= 8) { bits -= 8; out += static_cast<char>((v >> bits) & 0xFF); }
    }
    return out;
}

} // namespace

TopoDS_Shape importShape(const std::string& path)
{
    const std::string p = lower(path);
    auto ends = [&](const char* ext) { const std::string e(ext); return p.size() >= e.size() && p.compare(p.size() - e.size(), e.size(), e) == 0; };
    if (ends(".stl")) return readStl(path);
    if (ends(".step") || ends(".stp")) return readStep(path);
    throw std::runtime_error("Unbekanntes Dateiformat (STEP oder STL erwartet)");
}

bool exportStep(const TopoDS_Shape& shape, const std::string& path)
{
    if (shape.IsNull()) return false;
    STEPControl_Writer writer;
    if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) return false;
    return writer.Write(path.c_str()) == IFSelect_RetDone;
}

std::string shapeToString(const TopoDS_Shape& s)
{
    std::ostringstream o;
    BRepTools::Write(s, o);
    return b64encode(o.str());
}

TopoDS_Shape shapeFromString(const std::string& s)
{
    std::istringstream in(b64decode(s));
    TopoDS_Shape shape;
    BRep_Builder b;
    BRepTools::Read(shape, in, b);
    return shape;
}

} // namespace fw
