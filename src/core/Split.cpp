#include "core/Split.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace fw {

const char* connectorName(SplitConnector c)
{
    switch (c) {
    case SplitConnector::None: return "Ohne Verbinder";
    case SplitConnector::Pins: return "Passstifte";
    case SplitConnector::Peg: return "Zapfen";
    case SplitConnector::Dovetail: return "Schwalbenschwanz";
    case SplitConnector::Screw: return "Schraube und Mutter";
    }
    return "?";
}

void SplitPlan::setDefaults(SplitConnector c)
{
    connector = c;
    switch (c) {
    case SplitConnector::Pins: diameter = 5; length = 10; break;
    case SplitConnector::Peg: diameter = 6; length = 8; break;
    case SplitConnector::Dovetail: diameter = 10; length = 6; break;
    case SplitConnector::Screw: diameter = 3; length = 20; break;
    case SplitConnector::None: break;
    }
}

namespace {

struct Box3 {
    double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    double ext(int a) const { return hi[a] - lo[a]; }
};

Box3 bbox(const TopoDS_Shape& s)
{
    Bnd_Box b;
    BRepBndLib::Add(s, b);
    Box3 r;
    if (b.IsVoid()) return r;
    b.Get(r.lo[0], r.lo[1], r.lo[2], r.hi[0], r.hi[1], r.hi[2]);
    return r;
}

gp_Dir axisDir(int a) { return a == 0 ? gp::DX() : a == 1 ? gp::DY() : gp::DZ(); }
double coord(const gp_Pnt& p, int a) { return a == 0 ? p.X() : a == 1 ? p.Y() : p.Z(); }
void inPlane(int axis, int& u, int& v)
{
    u = axis == 0 ? 1 : 0;
    v = axis == 2 ? 1 : 2;
}

double volume(const TopoDS_Shape& s)
{
    GProp_GProps p;
    BRepGProp::VolumeProperties(s, p);
    return std::abs(p.Mass());
}

std::vector<TopoDS_Shape> solidsOf(const TopoDS_Shape& s)
{
    std::vector<TopoDS_Shape> out;
    for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next())
        if (volume(ex.Current()) > 1.0) out.push_back(ex.Current());
    return out;
}

TopoDS_Shape compound(const std::vector<TopoDS_Shape>& v)
{
    TopoDS_Compound c;
    BRep_Builder b;
    b.MakeCompound(c);
    for (const auto& s : v) b.Add(c, s);
    return c;
}

TopoDS_Shape boolCommon(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    BRepAlgoAPI_Common op(a, b);
    if (!op.IsDone()) throw std::runtime_error("Schnitt fehlgeschlagen");
    return op.Shape();
}
TopoDS_Shape boolCut(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    BRepAlgoAPI_Cut op(a, b);
    if (!op.IsDone()) throw std::runtime_error("Verbinder konnte nicht eingearbeitet werden");
    return op.Shape();
}
TopoDS_Shape boolFuse(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    BRepAlgoAPI_Fuse op(a, b);
    if (!op.IsDone()) throw std::runtime_error("Verbinder konnte nicht angefuegt werden");
    ShapeUpgrade_UnifySameDomain u(op.Shape(), true, true, true);
    u.Build();
    return u.Shape();
}

// Grosser Quader auf einer Seite der Schnittebene
TopoDS_Shape halfSpace(const Box3& b, int axis, double pos, bool low)
{
    double lo[3], hi[3];
    for (int i = 0; i < 3; ++i) { lo[i] = b.lo[i] - 10; hi[i] = b.hi[i] + 10; }
    if (low) hi[axis] = pos;
    else lo[axis] = pos;
    return BRepPrimAPI_MakeBox(gp_Pnt(lo[0], lo[1], lo[2]), gp_Pnt(hi[0], hi[1], hi[2])).Shape();
}

std::vector<TopoDS_Face> sectionFaces(const TopoDS_Shape& piece, int axis, double pos, const Box3& b)
{
    gp_Pnt o((b.lo[0] + b.hi[0]) / 2, (b.lo[1] + b.hi[1]) / 2, (b.lo[2] + b.hi[2]) / 2);
    if (axis == 0) o.SetX(pos); else if (axis == 1) o.SetY(pos); else o.SetZ(pos);
    const double big = std::max({b.ext(0), b.ext(1), b.ext(2)}) + 20;
    const TopoDS_Face sheet = BRepBuilderAPI_MakeFace(gp_Pln(o, axisDir(axis)), -big, big, -big, big).Face();
    std::vector<TopoDS_Face> out;
    BRepAlgoAPI_Common op(piece, sheet);
    if (!op.IsDone()) return out;
    for (TopExp_Explorer ex(op.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
        GProp_GProps p;
        BRepGProp::SurfaceProperties(ex.Current(), p);
        if (p.Mass() > 1.0) out.push_back(TopoDS::Face(ex.Current()));
    }
    return out;
}

double faceArea(const TopoDS_Face& f, gp_Pnt* centroid = nullptr)
{
    GProp_GProps p;
    BRepGProp::SurfaceProperties(f, p);
    if (centroid) *centroid = p.CentreOfMass();
    return p.Mass();
}

// Gut verteilte Punkte in der Schnittflaeche mit Mindestabstand keep zum Rand
// und zu den anderen Schnitten
std::vector<gp_Pnt> connectorPoints(const TopoDS_Face& f, int axis, double pos, double keep,
                                    const std::vector<SplitCut>& cuts, int count)
{
    int ua, va;
    inPlane(axis, ua, va);
    const Box3 fb = bbox(f);

    // Rand abtasten
    std::vector<std::pair<double, double>> rim;
    for (TopExp_Explorer ex(f, TopAbs_EDGE); ex.More(); ex.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
        double len = 0;
        try { len = GCPnts_AbscissaPoint::Length(c); } catch (...) { continue; }
        const int n = std::clamp(static_cast<int>(len / 0.5), 8, 2000);
        for (int i = 0; i <= n; ++i) {
            const gp_Pnt p = c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * i / n);
            rim.emplace_back(coord(p, ua), coord(p, va));
        }
    }
    if (rim.empty()) return {};

    gp_Pnt centroid;
    const double area = faceArea(f, &centroid);
    const double cu = coord(centroid, ua), cv = coord(centroid, va);

    struct Cand { double u, v, rimDist; };
    std::vector<Cand> cands;
    const double step = std::max(0.5, std::max(fb.ext(ua), fb.ext(va)) / 60.0);
    for (double u = fb.lo[ua] + step / 2; u < fb.hi[ua]; u += step) {
        for (double v = fb.lo[va] + step / 2; v < fb.hi[va]; v += step) {
            double dmin = 1e300;
            for (const auto& r : rim) dmin = std::min(dmin, std::hypot(r.first - u, r.second - v));
            if (dmin < keep) continue;
            gp_Pnt p;
            double xyz[3];
            xyz[axis] = pos; xyz[ua] = u; xyz[va] = v;
            p.SetCoord(xyz[0], xyz[1], xyz[2]);
            bool nearCut = false;
            for (const auto& c : cuts)
                if (c.axis != axis && std::abs(coord(p, c.axis) - c.pos) < keep) nearCut = true;
            if (nearCut) continue;
            BRepClass_FaceClassifier cls(f, p, 1e-6);
            if (cls.State() != TopAbs_IN) continue;
            cands.push_back({u, v, dmin});
        }
    }
    if (cands.empty()) return {};

    int want = count;
    if (want <= 0) want = area < 400 ? 1 : area < 2500 ? 2 : area < 10000 ? 3 : 4;

    std::vector<Cand> chosen;
    if (want == 1) {
        // moeglichst weit vom Rand (stabil), bei Gleichstand nahe der Mitte
        chosen.push_back(*std::max_element(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
            if (std::abs(a.rimDist - b.rimDist) > step * 0.5) return a.rimDist < b.rimDist;
            return std::hypot(a.u - cu, a.v - cv) > std::hypot(b.u - cu, b.v - cv);
        }));
    } else {
        chosen.push_back(*std::max_element(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
            return std::hypot(a.u - cu, a.v - cv) < std::hypot(b.u - cu, b.v - cv);
        }));
        while (static_cast<int>(chosen.size()) < want) {
            double best = -1;
            const Cand* bestC = nullptr;
            for (const auto& c : cands) {
                double d = 1e300;
                for (const auto& s : chosen) d = std::min(d, std::hypot(c.u - s.u, c.v - s.v));
                if (d > best) { best = d; bestC = &c; }
            }
            if (!bestC || best < 2 * keep) break;
            chosen.push_back(*bestC);
        }
    }
    std::vector<gp_Pnt> out;
    for (const auto& c : chosen) {
        double xyz[3];
        xyz[axis] = pos; xyz[ua] = c.u; xyz[va] = c.v;
        out.emplace_back(xyz[0], xyz[1], xyz[2]);
    }
    return out;
}

// Zylinder entlang der Achse von Abstand a bis b (vorzeichenbehaftet) ab p
TopoDS_Shape axialCylinder(const gp_Pnt& p, int axis, double r, double a, double b)
{
    const gp_Pnt start = p.Translated(gp_Vec(axisDir(axis)) * a);
    return BRepPrimAPI_MakeCylinder(gp_Ax2(start, axisDir(axis)), r, b - a).Shape();
}

// Sechskantprisma (Schluesselweite sw) entlang der Achse von a bis b
TopoDS_Shape axialHex(const gp_Pnt& p, int axis, double sw, double a, double b)
{
    int ua, va;
    inPlane(axis, ua, va);
    const double R = sw / 2 / std::cos(M_PI / 6);
    BRepBuilderAPI_MakePolygon poly;
    for (int i = 0; i < 6; ++i) {
        double xyz[3];
        xyz[axis] = coord(p, axis) + a;
        xyz[ua] = coord(p, ua) + R * std::cos(i * M_PI / 3);
        xyz[va] = coord(p, va) + R * std::sin(i * M_PI / 3);
        poly.Add(gp_Pnt(xyz[0], xyz[1], xyz[2]));
    }
    poly.Close();
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(poly.Wire(), true).Face();
    return BRepPrimAPI_MakePrism(face, gp_Vec(axisDir(axis)) * (b - a)).Shape();
}

struct ScrewSize { double d, nutAF, nutH, headD; };
ScrewSize screwSize(double m)
{
    static const ScrewSize table[] = {
        {2, 4.0, 1.6, 3.8}, {2.5, 5.0, 2.0, 4.5}, {3, 5.5, 2.4, 5.5}, {4, 7.0, 3.2, 7.0},
        {5, 8.0, 4.0, 8.5}, {6, 10.0, 5.0, 10.0}, {8, 13.0, 6.5, 13.0}};
    const ScrewSize* best = &table[2];
    for (const auto& s : table)
        if (std::abs(s.d - m) < std::abs(best->d - m)) best = &s;
    return *best;
}

// Schwalbenschwanz-Profil (Trapez in der Ebene aus Querrichtung und Normale), entlang run extrudiert
TopoDS_Shape dovetailPrism(const gp_Pnt& c, int axis, int run, int cross, double w, double h, double grow,
                           double runLo, double runHi)
{
    const double t = std::tan(15.0 * M_PI / 180.0);
    auto at = [&](double s, double n) {
        double xyz[3];
        xyz[axis] = coord(c, axis) + n;
        xyz[cross] = coord(c, cross) + s;
        xyz[run] = runLo;
        return gp_Pnt(xyz[0], xyz[1], xyz[2]);
    };
    const double b = w / 2 + grow, top = w / 2 + grow + (h + grow) * t;
    BRepBuilderAPI_MakePolygon poly(at(-b, 0), at(b, 0), at(top, h + grow), at(-top, h + grow), true);
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(poly.Wire(), true).Face();
    gp_Vec dir(axisDir(run));
    return BRepPrimAPI_MakePrism(face, dir * (runHi - runLo)).Shape();
}

} // namespace

std::vector<SplitCut> autoCuts(const TopoDS_Shape& body, double bedX, double bedY, double maxZ,
                               const SplitPlan& plan, double reserve)
{
    std::vector<SplitCut> cuts;
    if (body.IsNull()) return cuts;
    const Box3 b = bbox(body);
    const double extra = (plan.connector == SplitConnector::Peg || plan.connector == SplitConnector::Dovetail) ? plan.length : 0.0;
    const double room[3] = {bedX - 2 * reserve, bedY - 2 * reserve, maxZ - 1.0};
    for (int a = 0; a < 3; ++a) {
        const double ext = b.ext(a);
        if (ext <= room[a]) continue;
        const double usable = std::max(10.0, room[a] - extra);
        const int n = static_cast<int>(std::ceil(ext / usable));
        for (int i = 1; i < n; ++i) cuts.push_back({a, b.lo[a] + ext * i / n});
    }
    return cuts;
}

SplitResult splitBody(const TopoDS_Shape& body, const SplitPlan& plan)
{
    SplitResult res;
    if (body.IsNull()) { res.error = "Kein Koerper vorhanden"; return res; }
    try {
        const Box3 all = bbox(body);
        std::vector<TopoDS_Shape> pieces = solidsOf(body);
        if (pieces.empty()) pieces.push_back(body);
        const double c = plan.clearance;
        const ScrewSize ss = screwSize(plan.diameter);
        double keep = plan.diameter / 2 + c + plan.margin;
        if (plan.connector == SplitConnector::Screw) keep = ss.nutAF / 2 / std::cos(M_PI / 6) + c + plan.margin;
        int pinCount = 0;
        int skipped = 0;

        for (const SplitCut& cut : plan.cuts) {
            const int axis = cut.axis;
            std::vector<TopoDS_Shape> next;
            for (const TopoDS_Shape& piece : pieces) {
                const Box3 pb = bbox(piece);
                if (cut.pos <= pb.lo[axis] + 0.01 || cut.pos >= pb.hi[axis] - 0.01) { next.push_back(piece); continue; }
                TopoDS_Shape low = boolCommon(piece, halfSpace(all, axis, cut.pos, true));
                TopoDS_Shape high = boolCommon(piece, halfSpace(all, axis, cut.pos, false));

                if (plan.connector != SplitConnector::None) {
                    for (const TopoDS_Face& f : sectionFaces(piece, axis, cut.pos, all)) {
                        if (plan.connector == SplitConnector::Dovetail) {
                            int ua, va;
                            inPlane(axis, ua, va);
                            const Box3 fb = bbox(f);
                            const int run = fb.ext(ua) >= fb.ext(va) ? ua : va, cross = run == ua ? va : ua;
                            if (fb.ext(cross) < plan.diameter + 2 * plan.length * std::tan(15.0 * M_PI / 180.0) + 2 * plan.margin) { ++skipped; continue; }
                            gp_Pnt ctr;
                            faceArea(f, &ctr);
                            double xyz[3];
                            xyz[axis] = cut.pos; xyz[ua] = coord(ctr, ua); xyz[va] = coord(ctr, va);
                            const gp_Pnt p(xyz[0], xyz[1], xyz[2]);
                            const TopoDS_Shape tail = dovetailPrism(p, axis, run, cross, plan.diameter, plan.length, 0, fb.lo[run] - 1, fb.hi[run] + 1);
                            const TopoDS_Shape slot = dovetailPrism(p, axis, run, cross, plan.diameter, plan.length, c, fb.lo[run] - 1, fb.hi[run] + 1);
                            const TopoDS_Shape gain = boolCommon(high, tail);
                            high = boolCut(high, slot);
                            low = boolFuse(low, gain);
                            ++res.connectors;
                            continue;
                        }
                        const auto pts = connectorPoints(f, axis, cut.pos, keep, plan.cuts, plan.count);
                        if (pts.empty()) { ++skipped; continue; }
                        for (const gp_Pnt& p : pts) {
                            const double r = plan.diameter / 2;
                            if (plan.connector == SplitConnector::Pins) {
                                const double h = plan.length / 2 + 0.5;
                                const TopoDS_Shape hole = axialCylinder(p, axis, r + c, -h, h);
                                low = boolCut(low, hole);
                                high = boolCut(high, hole);
                                ++pinCount;
                            } else if (plan.connector == SplitConnector::Peg) {
                                const TopoDS_Shape plug = axialCylinder(p, axis, r, 0, plan.length);
                                const TopoDS_Shape gain = boolCommon(high, plug);
                                high = boolCut(high, axialCylinder(p, axis, r + c, 0, plan.length + 0.5));
                                low = boolFuse(low, gain);
                            } else if (plan.connector == SplitConnector::Screw) {
                                const double ls = plan.length / 2;
                                const double far = std::max({all.ext(0), all.ext(1), all.ext(2)}) + 20;
                                const double rs = ss.d / 2 + c;
                                // Naechste Aussenseite quer zur Schraube: dorthin oeffnen Mutter- und Kopftasche
                                int ua, va;
                                inPlane(axis, ua, va);
                                int sideAxis = ua;
                                double sideSign = -1, best = 1e300;
                                for (int a : {ua, va})
                                    for (double sg : {-1.0, 1.0}) {
                                        const double d = sg < 0 ? coord(p, a) - pb.lo[a] : pb.hi[a] - coord(p, a);
                                        if (d < best) { best = d; sideAxis = a; sideSign = sg; }
                                    }
                                const int across = sideAxis == ua ? va : ua;
                                auto sideSlot = [&](double width, double a0, double a1) {
                                    double lo3[3], hi3[3];
                                    lo3[axis] = coord(p, axis) + a0; hi3[axis] = coord(p, axis) + a1;
                                    lo3[across] = coord(p, across) - width / 2; hi3[across] = coord(p, across) + width / 2;
                                    if (sideSign < 0) { lo3[sideAxis] = pb.lo[sideAxis] - 10; hi3[sideAxis] = coord(p, sideAxis); }
                                    else { lo3[sideAxis] = coord(p, sideAxis); hi3[sideAxis] = pb.hi[sideAxis] + 10; }
                                    return BRepPrimAPI_MakeBox(gp_Pnt(lo3[0], lo3[1], lo3[2]), gp_Pnt(hi3[0], hi3[1], hi3[2])).Shape();
                                };
                                // Unten (negative Seite): Schraubenloch und seitlich eingeschobene Mutter
                                const double nutT = ss.nutH + 2 * c;
                                low = boolCut(low, axialCylinder(p, axis, rs, -ls - 1.0, 0.5));
                                low = boolCut(low, axialHex(p, axis, ss.nutAF + 2 * c, -ls, -ls + nutT));
                                low = boolCut(low, sideSlot(ss.nutAF + 2 * c, -ls, -ls + nutT));
                                // Oben: Schraubenloch und Kopf; kurzer Weg nach aussen = Senkung, sonst seitliches Fenster
                                high = boolCut(high, axialCylinder(p, axis, rs, -0.5, ls));
                                if (pb.hi[axis] - cut.pos <= ls + 30) {
                                    high = boolCut(high, axialCylinder(p, axis, ss.headD / 2 + c, ls, far));
                                } else {
                                    const double win = std::max(15.0, 2.5 * ss.headD); // Platz fuer Kopf und Winkelschluessel
                                    high = boolCut(high, axialCylinder(p, axis, ss.headD / 2 + c, ls, ls + win));
                                    high = boolCut(high, sideSlot(ss.headD + 2 * c, ls, ls + win));
                                }
                            }
                            ++res.connectors;
                        }
                    }
                }
                for (const auto& s : solidsOf(low)) next.push_back(s);
                for (const auto& s : solidsOf(high)) next.push_back(s);
            }
            pieces.swap(next);
        }
        if (skipped > 0)
            res.warnings.push_back(std::to_string(skipped) + " Schnittflaeche(n) zu klein fuer " + connectorName(plan.connector)
                                   + ": dort ohne Verbinder");

        // Explosionsrichtung: Anzahl der Schnitte je Achse unterhalb der Teilmitte
        struct Item { TopoDS_Shape s; int idx[3]; };
        std::vector<Item> items;
        for (const auto& s : pieces) {
            GProp_GProps p;
            BRepGProp::VolumeProperties(s, p);
            const gp_Pnt m = p.CentreOfMass();
            Item it{s, {0, 0, 0}};
            for (const auto& cu : plan.cuts)
                if (coord(m, cu.axis) > cu.pos) ++it.idx[cu.axis];
            items.push_back(it);
        }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            for (int k : {2, 1, 0}) if (a.idx[k] != b.idx[k]) return a.idx[k] < b.idx[k];
            return false;
        });
        int n = 1;
        for (const auto& it : items)
            res.parts.push_back({"Teil " + std::to_string(n++), it.s, gp_Vec(it.idx[0], it.idx[1], it.idx[2]), false});

        if (plan.connector == SplitConnector::Pins && plan.printPins && pinCount > 0) {
            // Stehende Stifte vor dem Bauteil (Explosion geht nach +X/+Y/+Z)
            std::vector<TopoDS_Shape> pins;
            const double pitch = plan.diameter + 5;
            const int perRow = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(pinCount)))));
            for (int i = 0; i < pinCount; ++i) {
                const gp_Pnt base(all.lo[0] + (i % perRow) * pitch, all.lo[1] - 15 - (i / perRow) * pitch, all.lo[2]);
                pins.push_back(BRepPrimAPI_MakeCylinder(gp_Ax2(base, gp::DZ()), plan.diameter / 2, plan.length).Shape());
            }
            res.parts.push_back({"Passstifte (" + std::to_string(pinCount) + " Stück)", compound(pins), gp_Vec(0, 0, 0), true});
        }
        res.ok = true;
    } catch (const std::exception& e) {
        res.error = e.what();
    } catch (...) {
        res.error = "Teilen fehlgeschlagen";
    }
    return res;
}

std::string splitToText(const SplitPlan& p)
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(12) << "cuts=";
    for (size_t i = 0; i < p.cuts.size(); ++i) o << (i ? "/" : "") << p.cuts[i].axis << ':' << p.cuts[i].pos;
    o << ";conn=" << static_cast<int>(p.connector) << ";d=" << p.diameter << ";l=" << p.length << ";c=" << p.clearance
      << ";m=" << p.margin << ";n=" << p.count << ";pins=" << (p.printPins ? 1 : 0);
    return o.str();
}

bool splitFromText(const std::string& text, SplitPlan& p)
{
    auto num = [](const std::string& s) {
        std::istringstream in(s);
        in.imbue(std::locale::classic());
        double v = 0;
        if (!(in >> v)) throw std::invalid_argument("Zahl erwartet: " + s);
        return v;
    };
    try {
        SplitPlan out;
        std::stringstream ss(text);
        std::string item;
        while (std::getline(ss, item, ';')) {
            const auto eq = item.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = item.substr(0, eq), v = item.substr(eq + 1);
            if (k == "cuts") {
                std::stringstream cs(v);
                std::string c;
                while (std::getline(cs, c, '/')) {
                    const auto colon = c.find(':');
                    if (colon == std::string::npos) continue;
                    SplitCut cut;
                    cut.axis = std::clamp(static_cast<int>(num(c.substr(0, colon))), 0, 2);
                    cut.pos = num(c.substr(colon + 1));
                    out.cuts.push_back(cut);
                }
            } else if (k == "conn") {
                out.connector = static_cast<SplitConnector>(std::clamp(static_cast<int>(num(v)), 0, 4));
            } else if (k == "d") out.diameter = num(v);
            else if (k == "l") out.length = num(v);
            else if (k == "c") out.clearance = num(v);
            else if (k == "m") out.margin = num(v);
            else if (k == "n") out.count = static_cast<int>(num(v));
            else if (k == "pins") out.printPins = v == "1";
        }
        p = out;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace fw
