// Einfache Tests ohne Framework: Bauteil modellieren, slicen, G-Code pruefen.
#include "core/Expression.h"
#include "core/PartDocument.h"
#include "slicer/Settings.h"
#include "slicer/Slicer.h"

#include <BRepGProp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <GProp_GProps.hxx>

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static double volume(const TopoDS_Shape& s)
{
    GProp_GProps p;
    BRepGProp::VolumeProperties(s, p);
    return p.Mass();
}

static void testExtrudeWithHole()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 40, 30);
    sk->addCircle(20, 15, 5);
    doc.addFeature(sk);
    auto ex = std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 10.0);
    doc.addFeature(ex);
    CHECK(doc.recompute());
    const double expected = 40 * 30 * 10 - M_PI * 25 * 10;
    CHECK(std::abs(volume(doc.body()) - expected) < 1.0);

    // parametrisch aendern
    ex->setDistance(20.0);
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - 2 * expected) < 2.0);

    // Differenz-Extrusion von oben
    auto sk2 = std::make_shared<fw::Sketch>("Skizze2", fw::SketchPlane::XY, 20.0);
    sk2->addRectangle(0, 0, 10, 30);
    doc.addFeature(sk2);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion2", sk2, -5.0, fw::BooleanOp::Cut));
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - (2 * expected - 10 * 30 * 5)) < 2.0);
    CHECK(!doc.removeFeature(sk));
}

static void testRevolve()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XZ);
    sk->addRectangle(0, 0, 10, 20); // Zylinder r=10, h=20 um Skizzen-Y (Welt-Z)
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::RevolveFeature>("Drehung1", sk, fw::RevolveAxis::SketchY));
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - M_PI * 100 * 20) < 5.0);
}

static void testSlice()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 20, 20);
    sk->addCircle(10, 10, 4);
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 5.0));
    CHECK(doc.recompute());

    fw::PrinterProfile pp;
    fw::SliceResult r = fw::slice(doc.body(), pp);
    if (!r.ok) std::printf("Slicer-Fehler: %s\n", r.error.c_str());
    CHECK(r.ok);
    CHECK(r.layers.size() == 25);
    CHECK(r.gcode.find("M109 S205") != std::string::npos);
    CHECK(r.gcode.find("G28") != std::string::npos);
    CHECK(r.gcode.find(";LAYER:24") != std::string::npos);
    CHECK(r.filamentMm > 100 && r.filamentMm < 2000);

    // Alle Druckbewegungen innerhalb des Teils (mittig bei 110/110, +/-10 mm + Skirt)
    std::istringstream in(r.gcode);
    std::string line;
    bool inLayers = false;
    double minX = 1e9, maxX = -1e9;
    while (std::getline(in, line)) {
        if (line.rfind(";LAYER:", 0) == 0) inLayers = true;
        if (line.rfind(";Ende", 0) == 0) inLayers = false;
        if (!inLayers || line.rfind("G1 ", 0) != 0) continue;
        auto px = line.find(" X");
        if (px == std::string::npos) continue;
        double x = std::stod(line.substr(px + 2));
        minX = std::min(minX, x); maxX = std::max(maxX, x);
    }
    CHECK(minX > 110 - 10 - 7 && maxX < 110 + 10 + 7);

    // Zu grosses Teil wird abgelehnt
    {
        auto& g = sk->geometry();
        for (auto& c : g.constraints) if (c.type == fw::ConType::DistanceX) c.value = 300;
        CHECK(g.solve().status == fw::SolveStatus::Ok);
    }
    CHECK(doc.recompute());
    CHECK(!fw::slice(doc.body(), pp).ok);
}

static void testProfilesAndEvents()
{
    // Profil-Datei: Hin- und Rueckweg
    fw::PrinterProfile p = fw::builtinProfiles().at(3);
    p.infillDensity = 0.35;
    p.startGcode = "G28\nM117 Hallo ; Kommentar";
    p.events.push_back({2.0, fw::HeightEventType::FilamentChange, 0, ""});
    p.events.push_back({3.0, fw::HeightEventType::FanPercent, 50, ""});
    p.events.push_back({4.0, fw::HeightEventType::CustomGcode, 0, "M117 Test;x\nG4 S1"});
    p.events.push_back({99.0, fw::HeightEventType::Pause, 0, ""});
    fw::PrinterProfile q;
    CHECK(fw::deserializeProfile(fw::serializeProfile(p), q));
    CHECK(q.name == p.name && q.nozzleTemp == 235 && std::abs(q.infillDensity - 0.35) < 1e-9);
    CHECK(q.startGcode == p.startGcode);
    CHECK(q.events.size() == 4 && q.events[2].gcode == "M117 Test;x\nG4 S1");

    // Slicen mit Ereignissen, Brim und Gitter-Fuellung
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 20, 20);
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 5.0));
    CHECK(doc.recompute());
    q.brimWidth = 4.0;
    q.infillPattern = static_cast<int>(fw::InfillPattern::Grid);
    fw::SliceResult r = fw::slice(doc.body(), q);
    CHECK(r.ok);
    CHECK(r.warnings.size() == 1); // Pause bei 99 mm liegt ueber dem Teil
    // M600 vor der Schicht, die bei Z=2,0 beginnt (Schicht 10, Oberkante 2,2)
    const auto m600 = r.gcode.find("M600");
    CHECK(m600 != std::string::npos);
    CHECK(m600 > r.gcode.find(";LAYER:10\n") && m600 < r.gcode.find(";LAYER:11\n"));
    CHECK(r.gcode.find("M106 S128") != std::string::npos);
    CHECK(r.gcode.find("G4 S1") != std::string::npos);
    CHECK(r.gcode.find("M109 S240") != std::string::npos);  // erste Schicht PETG
    CHECK(r.gcode.find("M104 S235") != std::string::npos);  // danach Normaltemperatur
    CHECK(r.gcode.find("M117 Hallo") != std::string::npos);
    int brim = 0;
    for (const auto& tp : r.layers[0].paths) brim += tp.type == fw::PathType::Brim;
    CHECK(brim == 10);
    CHECK(r.filamentGrams > 0.5 && r.filamentCost > 0);
}

static void testSupports()
{
    // Saeule 10x10x20 mit auskragender Platte 40x10x2 oben
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(15, 0, 10, 10);
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 20.0));
    auto sk2 = std::make_shared<fw::Sketch>("Skizze2", fw::SketchPlane::XY, 18.0);
    sk2->addRectangle(0, 0, 40, 10);
    doc.addFeature(sk2);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion2", sk2, 4.0));
    CHECK(doc.recompute());

    fw::PrinterProfile p;
    auto countSupport = [](const fw::SliceResult& r, size_t layer) {
        int c = 0;
        for (const auto& tp : r.layers.at(layer).paths) c += tp.type == fw::PathType::Support;
        return c;
    };
    fw::SliceResult none = fw::slice(doc.body(), p);
    CHECK(none.ok && countSupport(none, 10) == 0);

    p.supportMode = static_cast<int>(fw::SupportMode::Everywhere);
    fw::SliceResult r = fw::slice(doc.body(), p);
    CHECK(r.ok);
    CHECK(countSupport(r, 0) > 0);
    CHECK(countSupport(r, 50) > 0);
    CHECK(countSupport(r, 88) > 0);
    CHECK(countSupport(r, 89) == 0); // Abstandsschicht unter der Platte (Platte ab Schicht 90)
    CHECK(countSupport(r, 95) == 0); // in der Platte selbst
    CHECK(r.gcode.find(";TYPE:SUPPORT") != std::string::npos);
    // Stuetzen nie innerhalb der Saeule (Bettmitte 110/110, Saeule x 105..115)
    for (const auto& tp : r.layers[50].paths)
        if (tp.type == fw::PathType::Support)
            for (const auto& v : tp.points) CHECK(v.x < 105.0 - 0.5 || v.x > 115.0 + 0.5);
}

static void testFileRoundtrip()
{
    fw::PartDocument doc("Halter");
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XZ, 2.5);
    sk->addRectangle(0, 0, 10.25, 20);
    sk->addCircle(5, 5, 1.5);
    doc.addFeature(sk);
    auto ex = std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, -7.5, fw::BooleanOp::Join, true);
    ex->setSuppressed(true);
    doc.addFeature(ex);
    doc.addFeature(std::make_shared<fw::RevolveFeature>("Drehung1", sk, fw::RevolveAxis::SketchX, 90.0));
    const std::string text = doc.toText();

    fw::PartDocument copy;
    std::string err;
    CHECK(copy.fromText(text, &err));
    CHECK(copy.toText() == text);
    CHECK(copy.name() == "Halter" && copy.features().size() == 3);
    auto e2 = std::dynamic_pointer_cast<fw::ExtrudeFeature>(copy.features()[1]);
    CHECK(e2 && e2->isSuppressed() && e2->symmetric() && e2->sketch() == copy.features()[0]);
    CHECK(!copy.fromText("format = 1\nextrude = E;sketch=Fehlt;distance=1;op=0\n", &err));
    CHECK(copy.features().size() == 3); // bei Fehler unveraendert
}

static void testCancel()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 20, 20);
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 5.0));
    CHECK(doc.recompute());
    int calls = 0;
    fw::SliceResult r = fw::slice(doc.body(), fw::PrinterProfile{}, [&calls](double) { return ++calls < 5; });
    CHECK(!r.ok && r.cancelled);
}

static void testSketchSolver()
{
    using namespace fw;
    // Rechteck: Masse aendern -> Geometrie folgt
    Sketch sk("Skizze1", SketchPlane::XY);
    RectangleRefs r = sk.addRectangle(0, 0, 20, 10);
    auto& g = sk.geometry();
    CHECK(g.solve().status == SolveStatus::Ok);
    CHECK(g.degreesOfFreedom() == 2); // nur noch Lage frei
    // linke untere Ecke auf den Ursprung -> vollstaendig bestimmt
    const int p0 = g.entities[r.lines[0]].p0;
    CHECK(g.canAdd({ConType::Coincident, 0, p0}));
    g.addConstraint({ConType::Coincident, 0, p0});
    SolveResult res = g.solve();
    CHECK(res.status == SolveStatus::Ok && res.dof == 0);
    g.constraints[r.widthDim].value = 35;
    CHECK(g.solve().status == SolveStatus::Ok);
    const int p2 = g.entities[r.lines[1]].p1;
    CHECK(std::abs(g.points[p2].x - 35) < 1e-6 && std::abs(g.points[p2].y - 10) < 1e-6);
    // Redundant: weitere Horizontal-Bedingung auf schon waagrechter Linie
    CHECK(!g.canAdd({ConType::Horizontal, r.lines[0]}));
    // Ziehen eines voll bestimmten Punktes aendert nichts
    g.drag(p2, 50, 50);
    CHECK(std::abs(g.points[p2].x - 35) < 1e-6);

    // Langloch: zwei Linien + zwei Boegen, Breite 10, Mittenabstand 30
    Sketch slot("Skizze2", SketchPlane::XY);
    auto& s = slot.geometry();
    const int c1 = s.addPoint(0, 0), c2 = s.addPoint(30, 0);
    const int a = s.addPoint(0, -5), b = s.addPoint(30, -5), c = s.addPoint(30, 5), d = s.addPoint(0, 5);
    const int l1 = s.addLine(a, b), l2 = s.addLine(c, d);
    s.addArc(c2, b, c);
    s.addArc(c1, d, a);
    s.addConstraint({ConType::Horizontal, l1});
    s.addConstraint({ConType::Horizontal, l2});
    CHECK(s.solve().status == SolveStatus::Ok);
    PartDocument doc;
    auto sp = std::make_shared<Sketch>(slot);
    doc.addFeature(sp);
    doc.addFeature(std::make_shared<ExtrudeFeature>("Extrusion1", sp, 2.0));
    CHECK(doc.recompute());
    const double expected = (30 * 10 + M_PI * 25) * 2;
    CHECK(std::abs(volume(doc.body()) - expected) < 0.5);

    // Datei-Rundweg mit Abhaengigkeiten
    const std::string text = doc.toText();
    PartDocument copy;
    CHECK(copy.fromText(text));
    CHECK(copy.toText() == text);
    CHECK(copy.recompute() && std::abs(volume(copy.body()) - expected) < 0.5);

    // Offene Kontur -> verstaendlicher Fehler
    Sketch open("Skizze3", SketchPlane::XY);
    auto& o = open.geometry();
    o.addLine(o.addPoint(0, 0), o.addPoint(10, 0));
    bool threw = false;
    try { open.profile(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

static void testModifiers()
{
    // Saeule mit auskragender Platte (wie testSupports), Bauteilkoordinaten: Saeule x 15..25, Platte x 0..40
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(15, 0, 10, 10);
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 20.0));
    auto sk2 = std::make_shared<fw::Sketch>("Skizze2", fw::SketchPlane::XY, 18.0);
    sk2->addRectangle(0, 0, 40, 10);
    doc.addFeature(sk2);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion2", sk2, 4.0));
    CHECK(doc.recompute());
    auto supportCount = [](const fw::SliceResult& r, size_t layer) {
        int c = 0;
        for (const auto& tp : r.layers.at(layer).paths) c += tp.type == fw::PathType::Support;
        return c;
    };
    auto maxSupportX = [](const fw::SliceResult& r, size_t layer) {
        double m = -1e9;
        for (const auto& tp : r.layers.at(layer).paths)
            if (tp.type == fw::PathType::Support) for (const auto& v : tp.points) m = std::max(m, v.x);
        return m;
    };
    fw::PrinterProfile p;

    // Stuetze nur per "erzwingen" links der Saeule (Stuetzmodus aus)
    fw::PrintModifier enf;
    enf.role = fw::PrintModifier::Role::SupportEnforcer;
    enf.cx = 7; enf.cy = 5; enf.cz = 10; enf.sx = 14; enf.sy = 10; enf.sz = 30;
    fw::SliceResult r1 = fw::slice(doc.body(), p, {}, {enf});
    CHECK(r1.ok && supportCount(r1, 50) > 0);
    CHECK(maxSupportX(r1, 50) < 110 - 5); // nur links der Saeule (Bettmitte 110 = Bauteil x 20)

    // Stuetzen ueberall, rechte Seite gesperrt
    p.supportMode = static_cast<int>(fw::SupportMode::Everywhere);
    fw::PrintModifier blk = enf;
    blk.role = fw::PrintModifier::Role::SupportBlocker;
    blk.cx = 33; blk.sx = 20;
    fw::SliceResult r2 = fw::slice(doc.body(), p, {}, {blk});
    CHECK(r2.ok && supportCount(r2, 50) > 0 && maxSupportX(r2, 50) < 110 - 5);

    // Infill-Zone mit 100 % im oberen Teil der Platte -> mehr Fuellung als ohne
    fw::PrinterProfile q;
    q.topLayers = 0; q.bottomLayers = 0;
    fw::PrintModifier zone;
    zone.cx = 20; zone.cy = 5; zone.cz = 10; zone.sx = 10; zone.sy = 10; zone.sz = 8;
    zone.infillDensity = 1.0;
    auto infillLen = [](const fw::SliceResult& r, size_t layer) {
        double len = 0;
        for (const auto& tp : r.layers.at(layer).paths)
            if (tp.type == fw::PathType::Infill)
                for (size_t k = 1; k < tp.points.size(); ++k)
                    len += std::hypot(tp.points[k].x - tp.points[k - 1].x, tp.points[k].y - tp.points[k - 1].y);
        return len;
    };
    fw::SliceResult a = fw::slice(doc.body(), q);
    fw::SliceResult b = fw::slice(doc.body(), q, {}, {zone});
    CHECK(a.ok && b.ok);
    CHECK(infillLen(b, 50) > 3 * infillLen(a, 50));
    CHECK(std::abs(infillLen(b, 80) - infillLen(a, 80)) < 1e-6); // ausserhalb der Zone unveraendert

    // Modifikatoren werden mit dem Bauteil gespeichert
    doc.modifiers() = {enf, zone};
    fw::PartDocument copy;
    CHECK(copy.fromText(doc.toText()));
    CHECK(copy.modifiers().size() == 2 && copy.modifiers()[1].infillDensity == 1.0 && copy.modifiers()[0].sz == 30);
}

void testCustomPlane()
{
    // Skizze auf der rechten Seitenflaeche eines Quaders (Normale +X), Zapfen nach aussen
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 20, 20);
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 10.0));
    auto side = std::make_shared<fw::Sketch>("Skizze2", gp_Ax3(gp_Pnt(20, 0, 0), gp::DX(), gp::DY()));
    side->addCircle(10, 5, 3);
    doc.addFeature(side);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion2", side, 5.0));
    CHECK(doc.recompute());
    const double expected = 20.0 * 20.0 * 10.0 + M_PI * 9.0 * 5.0;
    CHECK(std::abs(volume(doc.body()) - expected) < 1.0);

    // Speichern und Laden erhaelt die Ebene
    fw::PartDocument copy;
    CHECK(copy.fromText(doc.toText()));
    CHECK(copy.toText() == doc.toText());
    CHECK(copy.recompute());
    CHECK(std::abs(volume(copy.body()) - expected) < 1.0);
}

static void testModifyFeatures()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 40, 40);
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 10.0));
    auto hs = std::make_shared<fw::Sketch>("Skizze2", fw::SketchPlane::XY, 10.0);
    hs->geometry().points[static_cast<size_t>(hs->geometry().addPoint(10, 10))].center = true;
    doc.addFeature(hs);
    auto hole = std::make_shared<fw::HoleFeature>("Bohrung1", hs);
    hole->throughAll = true;
    doc.addFeature(hole);
    CHECK(doc.recompute());
    double v = 16000 - M_PI * 9 * 10;
    CHECK(std::abs(volume(doc.body()) - v) < 1.0);

    // Sackloch 5 mm tief mit Bohrspitze
    hole->throughAll = false;
    hole->depth = 5;
    CHECK(doc.recompute());
    const double cone = M_PI * 9 * (3 / std::tan(59 * M_PI / 180)) / 3;
    CHECK(std::abs(volume(doc.body()) - (16000 - M_PI * 9 * 5 - cone)) < 1.0);
    hole->throughAll = true;

    // Senkung
    hole->kind = fw::HoleFeature::Kind::Counterbore;
    hole->cbDiameter = 10; hole->cbDepth = 3;
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - (v - M_PI * (25 - 9) * 3)) < 1.0);
    hole->kind = fw::HoleFeature::Kind::Simple;

    auto pat = std::make_shared<fw::PatternFeature>("Rechteckige Anordnung1", fw::PatternFeature::Kind::Rectangular);
    pat->sources = {hole};
    pat->count1 = 2; pat->spacing1 = 20;
    pat->useDir2 = true; pat->count2 = 2; pat->spacing2 = 20;
    doc.addFeature(pat);
    CHECK(doc.recompute());
    v = 16000 - 4 * M_PI * 9 * 10;
    CHECK(std::abs(volume(doc.body()) - v) < 1.0);
    CHECK(!doc.removeFeature(hole));

    auto fil = std::make_shared<fw::FilletFeature>("Rundung1", 2.0);
    fil->edges = {gp_Pnt(40, 40, 5)};
    doc.addFeature(fil);
    auto cha = std::make_shared<fw::ChamferFeature>("Fase1", 1.0);
    cha->edges = {gp_Pnt(20, 0, 10)};
    doc.addFeature(cha);
    CHECK(doc.recompute());
    v -= (4 - M_PI) * 10 + 0.5 * 40;
    CHECK(std::abs(volume(doc.body()) - v) < 1.0);

    auto mir = std::make_shared<fw::MirrorFeature>("Spiegeln1");
    mir->plane = 2;
    doc.addFeature(mir);
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - 2 * v) < 2.0);

    // Parameter: Bohrungsdurchmesser per Gleichung
    doc.assignParameterNames();
    std::string diaParam;
    for (const auto& p : doc.parameters()) if (p.owner == "Bohrung1: Durchmesser") diaParam = p.name;
    CHECK(!diaParam.empty());
    CHECK(doc.setModelExpression(diaParam, "4 mm"));
    CHECK(doc.recompute());
    CHECK(std::abs(hole->diameter - 4) < 1e-9);

    // Speichern/Laden
    fw::PartDocument copy;
    std::string err;
    CHECK(copy.fromText(doc.toText(), &err));
    CHECK(copy.toText() == doc.toText());
    CHECK(copy.recompute());
    CHECK(std::abs(volume(copy.body()) - volume(doc.body())) < 0.1);

    // Runde Anordnung um Z: 6 Bohrungen
    fw::PartDocument d2;
    auto s1 = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    s1->addCircle(0, 0, 30);
    d2.addFeature(s1);
    d2.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", s1, 5.0));
    auto s2 = std::make_shared<fw::Sketch>("Skizze2", fw::SketchPlane::XY, 5.0);
    s2->geometry().points[static_cast<size_t>(s2->geometry().addPoint(20, 0))].center = true;
    d2.addFeature(s2);
    auto h2 = std::make_shared<fw::HoleFeature>("Bohrung1", s2);
    h2->throughAll = true; h2->diameter = 4;
    d2.addFeature(h2);
    auto cp = std::make_shared<fw::PatternFeature>("Runde Anordnung1", fw::PatternFeature::Kind::Circular);
    cp->sources = {h2};
    cp->count = 6;
    d2.addFeature(cp);
    CHECK(d2.recompute());
    CHECK(std::abs(volume(d2.body()) - (M_PI * 900 * 5 - 6 * M_PI * 4 * 5)) < 2.0);
}

static void testShell()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 40, 30);
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 20.0));
    auto sh = std::make_shared<fw::ShellFeature>("Wandung1", 2.0);
    sh->faces = {gp_Pnt(20, 15, 20)}; // Deckel offen
    doc.addFeature(sh);
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - (40 * 30 * 20 - 36 * 26 * 18)) < 1.0);
    // ohne offene Flaeche: geschlossener Hohlkoerper
    sh->faces.clear();
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - (40 * 30 * 20 - 36 * 26 * 16)) < 1.0);
    sh->faces = {gp_Pnt(20, 15, 20)};
    fw::PartDocument copy;
    CHECK(copy.fromText(doc.toText()));
    CHECK(copy.toText() == doc.toText());
    // Punkt auf einer Flaeche
    TopExp_Explorer ex(doc.body(), TopAbs_FACE);
    const TopoDS_Face f = TopoDS::Face(ex.Current());
    CHECK(fw::faceAt(doc.body(), fw::facePoint(f)).IsSame(f));
}

static void testImportExport()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 20, 10);
    doc.addFeature(sk);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 5.0));
    CHECK(doc.recompute());
    const std::string step = "/tmp/formwerk_test.step", stl = "/tmp/formwerk_test.stl";
    CHECK(doc.exportStep(step));
    CHECK(doc.exportStl(stl));
    for (const auto& path : {step, stl}) {
        fw::PartDocument d2;
        TopoDS_Shape s;
        try { s = fw::importShape(path); } catch (const std::exception& e) { std::printf("Import: %s\n", e.what()); }
        CHECK(!s.IsNull());
        d2.addFeature(std::make_shared<fw::ImportFeature>("Import1", s, path));
        CHECK(d2.recompute());
        CHECK(std::abs(volume(d2.body()) - 1000.0) < 1.0);
        fw::PartDocument d3;
        CHECK(d3.fromText(d2.toText(true)));
        CHECK(d2.toText(true).size() > d2.toText().size() + 1000);
        CHECK(d3.recompute());
        CHECK(std::abs(volume(d3.body()) - 1000.0) < 1.0);
    }
}

void testEndOfPart()
{
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 10, 10);
    doc.addFeature(sk);
    auto e1 = std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 10.0);
    doc.addFeature(e1);
    auto sk2 = std::make_shared<fw::Sketch>("Skizze2", fw::SketchPlane::XY, 10.0);
    sk2->addRectangle(0, 0, 10, 10);
    doc.addFeature(sk2);
    doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion2", sk2, 5.0));
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - 1500.0) < 1.0);

    // Bauteilende hinter Extrusion1: nur 1000 mm³
    doc.setEndOfPart(2);
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - 1000.0) < 1.0);
    CHECK(doc.isAfterEndOfPart(doc.features()[3]));

    // Neues Feature landet am Bauteilende, Bauteilende rutscht mit
    auto sk3 = std::make_shared<fw::Sketch>("Skizze3", fw::SketchPlane::XY);
    doc.addFeature(sk3);
    CHECK(doc.features()[2] == sk3);
    CHECK(doc.endOfPart() == 3);

    // Speichern/Laden, Umbenennen, Sichtbarkeit
    sk->setVisibility(fw::Sketch::Visibility::Shown);
    CHECK(!doc.rename(e1, "Skizze2"));
    CHECK(doc.rename(e1, "Grundplatte"));
    fw::PartDocument copy;
    CHECK(copy.fromText(doc.toText()));
    CHECK(copy.endOfPart() == 3);
    CHECK(copy.features()[1]->name() == "Grundplatte");
    CHECK(std::dynamic_pointer_cast<fw::Sketch>(copy.features()[0])->visibility() == fw::Sketch::Visibility::Shown);
    CHECK(copy.toText() == doc.toText());
    doc.setEndOfPart(-1);
    CHECK(doc.endOfPart() == 5);
}

void testParameters()
{
    // Ausdruecke
    auto none = fw::ParamLookup{};
    CHECK(std::abs(fw::evaluate("2 + 3 * 4", none) - 14.0) < 1e-12);
    CHECK(std::abs(fw::evaluate("12,5 mm", none) - 12.5) < 1e-12);
    CHECK(std::abs(fw::evaluate("1 cm + 2 mm", none) - 12.0) < 1e-12);
    CHECK(std::abs(fw::evaluate("(2 + 3) ^ 2", none) - 25.0) < 1e-12);
    CHECK(std::abs(fw::evaluate("sin(30)", none) - 0.5) < 1e-12);
    CHECK(std::abs(fw::evaluate("max(3; 7; 5)", none) - 7.0) < 1e-12);
    CHECK(std::abs(fw::evaluate("-PI", none) + M_PI) < 1e-12);
    bool threw = false;
    try { fw::evaluate("1 / 0", none); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    CHECK(fw::isPlainNumber("12,5 mm"));
    CHECK(fw::isPlainNumber("-3"));
    CHECK(!fw::isPlainNumber("d0 * 2"));
    CHECK(!fw::isPlainNumber("2 * 3"));
    CHECK(fw::isValidParamName("Breite_2"));
    CHECK(!fw::isValidParamName("2Breite"));
    CHECK(!fw::isValidParamName("mm"));

    // Rechteck 20 x 10 (d0, d1), Extrusion (d2); Breite per Benutzerparameter, Hoehe = d0 / 2
    fw::PartDocument doc;
    auto sk = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk->addRectangle(0, 0, 20, 10);
    doc.addFeature(sk);
    auto ex = std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk, 5.0);
    doc.addFeature(ex);
    CHECK(doc.recompute());
    auto params = doc.parameters();
    CHECK(params.size() == 3);
    CHECK(params[0].name == "d0" && params[1].name == "d1" && params[2].name == "d2");
    CHECK(std::abs(volume(doc.body()) - 1000.0) < 1.0);

    doc.userParameters().push_back({"Breite", "30 mm", "Gesamtbreite"});
    CHECK(doc.setModelExpression("d0", "Breite"));
    CHECK(doc.setModelExpression("d1", "d0 / 2"));
    CHECK(doc.setModelExpression("d2", "4"));
    CHECK(doc.recompute());
    CHECK(std::abs(volume(doc.body()) - 30.0 * 15.0 * 4.0) < 1.0);
    CHECK(std::abs(ex->distance() - 4.0) < 1e-9);

    // Speichern/Laden erhaelt Gleichungen
    fw::PartDocument copy;
    CHECK(copy.fromText(doc.toText()));
    CHECK(copy.toText() == doc.toText());
    copy.userParameters()[0].expr = "40";
    CHECK(copy.recompute());
    CHECK(std::abs(volume(copy.body()) - 40.0 * 20.0 * 4.0) < 1.0);

    // Zirkelbezug wird gemeldet
    CHECK(doc.setModelExpression("d0", "d1 * 2"));
    CHECK(!doc.recompute());
    CHECK(doc.errorMessage().find("Zirkelbezug") != std::string::npos);
}

int main()
{
    testParameters();
    testEndOfPart();
    testModifyFeatures();
    testShell();
    testImportExport();
    testCustomPlane();
    testModifiers();
    testSketchSolver();
    testFileRoundtrip();
    testCancel();
    testSupports();
    testProfilesAndEvents();
    testExtrudeWithHole();
    testRevolve();
    testSlice();
    if (g_failed) { std::printf("%d Pruefung(en) fehlgeschlagen\n", g_failed); return 1; }
    std::printf("Alle Tests bestanden\n");
    return 0;
}
