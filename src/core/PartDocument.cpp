#include "core/PartDocument.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>

#include <algorithm>
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

} // namespace fw
