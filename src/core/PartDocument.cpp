#include "core/PartDocument.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>

#include "core/Expression.h"

#include <algorithm>
#include <functional>
#include <set>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <exception>

namespace fw {

PartDocument::PartDocument(std::string name) : m_name(std::move(name)) {}

void PartDocument::addFeature(FeaturePtr f)
{
    if (m_endOfPart >= 0 && m_endOfPart < static_cast<int>(m_features.size())) {
        m_features.insert(m_features.begin() + m_endOfPart, std::move(f));
        ++m_endOfPart;
    } else {
        m_features.push_back(std::move(f));
    }
}

int PartDocument::endOfPart() const
{
    const int n = static_cast<int>(m_features.size());
    return m_endOfPart < 0 ? n : std::min(m_endOfPart, n);
}

void PartDocument::setEndOfPart(int count)
{
    m_endOfPart = (count < 0 || count >= static_cast<int>(m_features.size())) ? -1 : count;
}

bool PartDocument::isAfterEndOfPart(const FeaturePtr& f) const
{
    auto it = std::find(m_features.begin(), m_features.end(), f);
    return it != m_features.end() && (it - m_features.begin()) >= endOfPart();
}

bool PartDocument::rename(const FeaturePtr& f, const std::string& name)
{
    if (name.empty() || name.find_first_of(";=\n\r") != std::string::npos) return false;
    for (const auto& o : m_features)
        if (o != f && o->name() == name) return false;
    f->setName(name);
    return true;
}

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
    if (m_endOfPart >= 0 && (it - m_features.begin()) < m_endOfPart) --m_endOfPart;
    m_features.erase(it);
    return true;
}

bool PartDocument::recompute()
{
    m_errorFeature.reset();
    m_errorMessage.clear();
    assignParameterNames();
    std::string paramError;
    updateParameters(paramError);
    TopoDS_Shape body;
    const int end = endOfPart();
    for (int i = 0; i < end; ++i) {
        const FeaturePtr& f = m_features[static_cast<size_t>(i)];
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
    if (!m_errorFeature && !paramError.empty()) {
        m_errorMessage = paramError;
        return false;
    }
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
// Gleichungen und Kommentare duerfen ';' '=' '%' enthalten
std::string esc(const std::string& in)
{
    std::string out;
    for (char c : in) {
        if (c == '%') out += "%25";
        else if (c == ';') out += "%3B";
        else if (c == '=') out += "%3D";
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    return out;
}

std::string unesc(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            const std::string h = in.substr(i + 1, 2);
            if (h == "25") { out += '%'; i += 2; continue; }
            if (h == "3B") { out += ';'; i += 2; continue; }
            if (h == "3D") { out += '='; i += 2; continue; }
        }
        out += in[i];
    }
    return out;
}

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

const char* modifierRoleName(PrintModifier::Role r)
{
    switch (r) {
    case PrintModifier::Role::InfillZone: return "Infill-Zone";
    case PrintModifier::Role::SupportEnforcer: return "Stütze erzwingen";
    case PrintModifier::Role::SupportBlocker: return "Stütze sperren";
    }
    return "?";
}

void PartDocument::clear(std::string name)
{
    m_name = std::move(name);
    m_features.clear();
    m_modifiers.clear();
    m_userParams.clear();
    m_endOfPart = -1;
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
              << ";offset=" << s->offset();
            if (s->planeType() == SketchPlane::Custom) {
                const gp_Ax3& f = s->frame();
                o << ";frame=" << f.Location().X() << ',' << f.Location().Y() << ',' << f.Location().Z() << ','
                  << f.Direction().X() << ',' << f.Direction().Y() << ',' << f.Direction().Z() << ','
                  << f.XDirection().X() << ',' << f.XDirection().Y() << ',' << f.XDirection().Z();
            }
            if (s->visibility() != Sketch::Visibility::Auto) o << ";visible=" << static_cast<int>(s->visibility());
            o << ";suppressed=" << sup << '\n';
            const auto& g = s->geometry();
            for (size_t i = 1; i < g.points.size(); ++i)
                o << "pt = " << g.points[i].x << ';' << g.points[i].y << '\n';
            for (const auto& e : g.entities)
                o << "ent = " << static_cast<int>(e.type) << ';' << e.p0 << ';' << e.p1 << ';' << e.p2 << ';'
                  << e.r << ';' << (e.construction ? 1 : 0) << '\n';
            for (const auto& c : g.constraints) {
                o << "con = " << static_cast<int>(c.type) << ';' << c.a << ';' << c.b << ';' << c.value << ';'
                  << c.value2 << ';' << (c.driving ? 1 : 0) << ';' << c.labelX << ';' << c.labelY;
                if (!c.ref.param.empty() || !c.ref.expr.empty()) o << ';' << c.ref.param << ';' << esc(c.ref.expr);
                o << '\n';
            }
        } else if (auto e = std::dynamic_pointer_cast<ExtrudeFeature>(f)) {
            o << "extrude = " << safeName(e->name()) << ";sketch=" << safeName(e->sketch()->name())
              << ";distance=" << e->distance() << ";op=" << static_cast<int>(e->operation())
              << ";symmetric=" << (e->symmetric() ? 1 : 0) << ";suppressed=" << sup;
            if (!e->distanceRef().param.empty()) o << ";param=" << e->distanceRef().param;
            if (!e->distanceRef().expr.empty()) o << ";expr=" << esc(e->distanceRef().expr);
            o << '\n';
        } else if (auto r = std::dynamic_pointer_cast<RevolveFeature>(f)) {
            o << "revolve = " << safeName(r->name()) << ";sketch=" << safeName(r->sketch()->name())
              << ";axis=" << static_cast<int>(r->axis()) << ";angle=" << r->angle()
              << ";op=" << static_cast<int>(r->operation()) << ";suppressed=" << sup;
            if (!r->angleRef().param.empty()) o << ";param=" << r->angleRef().param;
            if (!r->angleRef().expr.empty()) o << ";expr=" << esc(r->angleRef().expr);
            o << '\n';
        }
    }
    for (const auto& m : m_modifiers)
        o << "modifier = " << safeName(m.name) << ";role=" << static_cast<int>(m.role) << ";shape=" << static_cast<int>(m.shape)
          << ";c=" << m.cx << ',' << m.cy << ',' << m.cz << ";s=" << m.sx << ',' << m.sy << ',' << m.sz
          << ";infill=" << m.infillDensity << ";pattern=" << m.infillPattern << '\n';
    for (const auto& u : m_userParams)
        o << "uparam = " << u.name << ";expr=" << esc(u.expr) << ";comment=" << esc(u.comment) << '\n';
    if (m_endOfPart >= 0 && m_endOfPart < static_cast<int>(m_features.size())) o << "endofpart = " << m_endOfPart << '\n';
    return o.str();
}

bool PartDocument::fromText(const std::string& text, std::string* error)
{
    std::string name = "Bauteil1";
    std::vector<FeaturePtr> features;
    std::map<std::string, SketchPtr> sketchesByName;
    std::vector<PrintModifier> modifiers;
    SketchPtr current;
    int endOfPart = -1;
    std::vector<UserParameter> userParams;

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
            } else if (key == "uparam") {
                const auto a = attrs(parts);
                UserParameter u;
                u.name = parts.at(0);
                if (!isValidParamName(u.name)) throw std::invalid_argument("Ungueltiger Parametername: " + u.name);
                u.expr = unesc(need(a, "expr"));
                if (a.count("comment")) u.comment = unesc(a.at("comment"));
                userParams.push_back(u);
            } else if (key == "endofpart") {
                endOfPart = static_cast<int>(num(val));
            } else if (key == "sketch") {
                const auto a = attrs(parts);
                const auto planeType = static_cast<SketchPlane>(static_cast<int>(num(need(a, "plane"))));
                if (planeType == SketchPlane::Custom) {
                    const auto v = split(need(a, "frame"), ',');
                    if (v.size() != 9) throw std::invalid_argument("Ungueltige Skizzenebene");
                    current = std::make_shared<Sketch>(parts.at(0),
                        gp_Ax3(gp_Pnt(num(v[0]), num(v[1]), num(v[2])), gp_Dir(num(v[3]), num(v[4]), num(v[5])),
                               gp_Dir(num(v[6]), num(v[7]), num(v[8]))));
                    current->setOffset(num(need(a, "offset")));
                } else {
                    current = std::make_shared<Sketch>(parts.at(0), planeType, num(need(a, "offset")));
                }
                current->setSuppressed(a.count("suppressed") && a.at("suppressed") == "1");
                if (a.count("visible")) current->setVisibility(static_cast<Sketch::Visibility>(static_cast<int>(num(a.at("visible")))));
                sketchesByName[current->name()] = current;
                features.push_back(current);
            } else if (key == "rect" || key == "circle" || key == "pt" || key == "ent" || key == "con") {
                if (!current) throw std::invalid_argument("Element ohne Skizze");
                auto& g = current->geometry();
                auto idx = [&](size_t i) { return static_cast<int>(num(parts.at(i))); };
                if (key == "pt") {
                    g.addPoint(num(parts.at(0)), num(parts.at(1)));
                } else if (key == "ent") {
                    SkEntity e;
                    e.type = static_cast<SkType>(idx(0));
                    e.p0 = idx(1); e.p1 = idx(2); e.p2 = idx(3);
                    e.r = num(parts.at(4));
                    e.construction = idx(5) != 0;
                    for (int pi : {e.p0, e.p1, e.p2})
                        if (pi >= static_cast<int>(g.points.size())) throw std::invalid_argument("Punktindex ungültig");
                    g.entities.push_back(e);
                } else if (key == "con") {
                    SkConstraint c;
                    c.type = static_cast<ConType>(idx(0));
                    c.a = idx(1); c.b = idx(2);
                    c.value = num(parts.at(3)); c.value2 = num(parts.at(4));
                    c.driving = idx(5) != 0;
                    c.labelX = num(parts.at(6)); c.labelY = num(parts.at(7));
                    if (parts.size() > 8) c.ref.param = parts[8];
                    if (parts.size() > 9) c.ref.expr = unesc(parts[9]);
                    g.constraints.push_back(c);
                } else if (key == "rect" && parts.size() >= 4) {   // Format der Version 0.1
                    current->addRectangle(num(parts[0]), num(parts[1]), num(parts[2]), num(parts[3]));
                } else if (key == "circle" && parts.size() >= 3) {
                    current->addCircle(num(parts[0]), num(parts[1]), num(parts[2]));
                } else {
                    throw std::invalid_argument("Unvollständiges Element");
                }
            } else if (key == "modifier") {
                const auto a = attrs(parts);
                PrintModifier m;
                m.name = parts.at(0);
                m.role = static_cast<PrintModifier::Role>(static_cast<int>(num(need(a, "role"))));
                m.shape = static_cast<PrintModifier::Shape>(static_cast<int>(num(need(a, "shape"))));
                const auto c3 = split(need(a, "c"), ','), s3 = split(need(a, "s"), ',');
                if (c3.size() != 3 || s3.size() != 3) throw std::invalid_argument("Modifikator unvollständig");
                m.cx = num(c3[0]); m.cy = num(c3[1]); m.cz = num(c3[2]);
                m.sx = num(s3[0]); m.sy = num(s3[1]); m.sz = num(s3[2]);
                if (a.count("infill")) m.infillDensity = num(a.at("infill"));
                if (a.count("pattern")) m.infillPattern = static_cast<int>(num(a.at("pattern")));
                modifiers.push_back(m);
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
                ParamRef ref;
                if (a.count("param")) ref.param = a.at("param");
                if (a.count("expr")) ref.expr = unesc(a.at("expr"));
                if (auto e = std::dynamic_pointer_cast<ExtrudeFeature>(f)) e->distanceRef() = ref;
                if (auto r = std::dynamic_pointer_cast<RevolveFeature>(f)) r->angleRef() = ref;
                features.push_back(f);
            }
        }
    } catch (const std::exception& e) {
        if (error) *error = "Zeile " + std::to_string(lineNo) + ": " + e.what();
        return false;
    }
    clear(name);
    m_features = std::move(features);
    m_modifiers = std::move(modifiers);
    setEndOfPart(endOfPart);
    m_userParams = std::move(userParams);
    return true;
}


// --- Parameter -----------------------------------------------------------------

std::vector<PartDocument::Slot> PartDocument::paramSlots() const
{
    std::vector<Slot> out;
    for (const auto& f : m_features) {
        if (auto s = std::dynamic_pointer_cast<Sketch>(f)) {
            for (auto& c : s->geometry().constraints) {
                if (!isDimension(c.type) || !c.driving) continue;
                out.push_back({&c.ref, &c.value, c.type == ConType::Angle, s.get(),
                               s->name() + ": " + constraintName(c.type)});
            }
        } else if (auto e = std::dynamic_pointer_cast<ExtrudeFeature>(f)) {
            out.push_back({&e->distanceRef(), e->distancePtr(), false, nullptr, e->name() + ": Abstand"});
        } else if (auto r = std::dynamic_pointer_cast<RevolveFeature>(f)) {
            out.push_back({&r->angleRef(), r->anglePtr(), true, nullptr, r->name() + ": Winkel"});
        }
    }
    return out;
}

void PartDocument::assignParameterNames()
{
    std::set<std::string> used;
    int next = 0;
    auto note = [&](const std::string& n) {
        used.insert(n);
        if (n.size() > 1 && n[0] == 'd' && n.find_first_not_of("0123456789", 1) == std::string::npos)
            next = std::max(next, std::stoi(n.substr(1)) + 1);
    };
    const auto sl = paramSlots();
    for (const auto& s : sl)
        if (!s.ref->param.empty()) note(s.ref->param);
    for (const auto& u : m_userParams) note(u.name);
    for (const auto& s : sl) {
        if (!s.ref->param.empty()) continue;
        std::string n;
        do { n = "d" + std::to_string(next++); } while (used.count(n));
        s.ref->param = n;
        used.insert(n);
    }
}

bool PartDocument::updateParameters(std::string& error)
{
    const auto sl = paramSlots();
    std::map<std::string, const Slot*> model;
    std::map<std::string, UserParameter*> user;
    for (const auto& s : sl) model[s.ref->param] = &s;
    for (auto& u : m_userParams) user[u.name] = &u;

    std::map<std::string, double> done;
    std::set<std::string> busy;
    std::function<double(const std::string&)> valueOf;
    const ParamLookup lookup = [&](const std::string& n, double& v) {
        if (!model.count(n) && !user.count(n)) return false;
        v = valueOf(n);
        return true;
    };
    valueOf = [&](const std::string& n) -> double {
        if (auto it = done.find(n); it != done.end()) return it->second;
        if (busy.count(n)) throw std::runtime_error("Zirkelbezug ueber '" + n + "'");
        busy.insert(n);
        double v = 0;
        if (auto m = model.find(n); m != model.end()) {
            const Slot& s = *m->second;
            v = s.ref->expr.empty() ? std::abs(*s.value) : evaluate(s.ref->expr, lookup);
        } else {
            v = evaluate(user.at(n)->expr, lookup);
        }
        busy.erase(n);
        done[n] = v;
        return v;
    };

    std::string current;
    try {
        for (auto& u : m_userParams) { current = u.name; u.value = valueOf(u.name); }
        for (const auto& s : sl) { current = s.ref->param; valueOf(s.ref->param); }
    } catch (const std::exception& e) {
        error = "Parameter " + current + ": " + e.what();
        return false;
    }

    std::set<Sketch*> dirty;
    for (const auto& s : sl) {
        if (s.ref->expr.empty()) continue;
        const double v = done.at(s.ref->param);
        const double nv = *s.value < 0 ? -v : v;
        if (std::abs(nv - *s.value) > 1e-12) {
            *s.value = nv;
            if (s.sketch) dirty.insert(s.sketch);
        }
    }
    for (Sketch* sk : dirty) {
        if (sk->geometry().solve().status != SolveStatus::Ok) {
            error = sk->name() + " laesst sich mit den Parameterwerten nicht loesen";
            return false;
        }
    }
    return true;
}

std::vector<ParameterInfo> PartDocument::parameters() const
{
    std::vector<ParameterInfo> out;
    for (const auto& s : paramSlots()) {
        ParameterInfo p;
        p.name = s.ref->param;
        p.expr = s.ref->expr;
        p.value = std::abs(*s.value);
        p.unit = s.angle ? "grd" : "mm";
        p.owner = s.owner;
        out.push_back(p);
    }
    for (const auto& u : m_userParams) {
        ParameterInfo p;
        p.name = u.name;
        p.expr = u.expr;
        p.value = u.value;
        p.unit = "";
        p.comment = u.comment;
        p.user = true;
        out.push_back(p);
    }
    return out;
}

bool PartDocument::setModelExpression(const std::string& name, const std::string& expr)
{
    for (const auto& s : paramSlots()) {
        if (s.ref->param != name) continue;
        if (isPlainNumber(expr)) {
            const double v = evaluate(expr, {});
            *s.value = *s.value < 0 ? -v : v;
            s.ref->expr.clear();
            if (s.sketch) s.sketch->geometry().solve();
        } else {
            s.ref->expr = expr;
        }
        return true;
    }
    return false;
}

int PartDocument::nextParameterIndex() const
{
    int next = 0;
    auto note = [&](const std::string& n) {
        if (n.size() > 1 && n[0] == 'd' && n.find_first_not_of("0123456789", 1) == std::string::npos)
            next = std::max(next, std::stoi(n.substr(1)) + 1);
    };
    for (const auto& f : m_features)
        if (auto s = std::dynamic_pointer_cast<Sketch>(f))
            for (const auto& c : s->geometry().constraints) note(c.ref.param);
    for (const auto& p : parameters()) note(p.name);
    return next;
}

bool PartDocument::parameterValue(const std::string& name, double& value) const
{
    for (const auto& p : parameters())
        if (p.name == name) { value = p.value; return true; }
    return false;
}

bool PartDocument::evaluateExpression(const std::string& expr, double& out, std::string* error) const
{
    std::map<std::string, double> values;
    for (const auto& p : parameters()) values[p.name] = p.value;
    try {
        out = evaluate(expr, [&](const std::string& n, double& v) {
            auto it = values.find(n);
            if (it == values.end()) return false;
            v = it->second;
            return true;
        });
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

} // namespace fw
