#include "app/SketchCanvas.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

#include "app/PropertyPanel.h"
#include "core/Expression.h"
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <map>

using namespace fw;

namespace {

const QColor kUnderColor(31, 95, 191);    // noch nicht voll bestimmt
const QColor kFullColor(25, 25, 30);      // voll bestimmt
const QColor kConstructionColor(230, 130, 30);
const QColor kSelectColor(220, 40, 40);
const QColor kHoverColor(60, 170, 250);
const QColor kDimColor(30, 110, 60);

double distToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = len2 > 0 ? ((p - a).x() * ab.x() + (p - a).y() * ab.y()) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

double angleOf(const SkPoint& c, const SkPoint& p) { return std::atan2(p.y - c.y, p.x - c.x); }

QString fmt(double v)
{
    QString s = QString::number(v, 'f', 3);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) s.chop(1);
    return s;
}

bool isRoundEntity(const SkEntity& e) { return e.type == SkType::Circle || e.type == SkType::Arc; }

QString glyph(ConType t)
{
    switch (t) {
    case ConType::Horizontal: return "H";
    case ConType::Vertical: return "V";
    case ConType::Parallel: return QString::fromUtf8("∥");
    case ConType::Perpendicular: return QString::fromUtf8("⊥");
    case ConType::Equal: return "=";
    case ConType::Tangent: return "T";
    case ConType::Fix: return "F";
    case ConType::Midpoint: return "M";
    case ConType::PointOnEntity: return QString::fromUtf8("•");
    default: return {};
    }
}

} // namespace

SketchCanvas::SketchCanvas(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);
    setAutoFillBackground(false);
}

void SketchCanvas::begin(const Sketch& sketch)
{
    m_geo = sketch.geometry();
    m_tool = sketch.isEmpty() ? Tool::Line : Tool::Select;
    cancelOperation();
    m_selected = {};
    m_lastSolve = m_geo.solve();
    fit();
    QTimer::singleShot(0, this, [this] { fit(); }); // nach dem Layout nochmals einpassen
    updateStatus();
    emit toolChanged();
}

void SketchCanvas::setTool(Tool t)
{
    cancelOperation();
    m_tool = t;
    m_selected = {};
    updateStatus();
    emit toolChanged();
    update();
}

void SketchCanvas::cancelOperation()
{
    m_pending.clear();
    m_picked.clear();
    m_dragPoint = -1;
    m_dragLabel = -1;
}

// --- Koordinaten ------------------------------------------------------------------

QPointF SketchCanvas::toScreen(double x, double y) const
{
    return {(x - m_center.x()) * m_scale + width() / 2.0, height() / 2.0 - (y - m_center.y()) * m_scale};
}

QPointF SketchCanvas::toSketch(const QPointF& s) const
{
    return {(s.x() - width() / 2.0) / m_scale + m_center.x(), (height() / 2.0 - s.y()) / m_scale + m_center.y()};
}

void SketchCanvas::fit()
{
    double x0 = -10, y0 = -10, x1 = 10, y1 = 10;
    bool any = false;
    for (const auto& e : m_geo.entities) {
        auto grow = [&](double x, double y) {
            if (!any) { x0 = x1 = x; y0 = y1 = y; any = true; }
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        };
        for (int p : {e.p0, e.p1, e.p2})
            if (p >= 0) grow(m_geo.points[p].x, m_geo.points[p].y);
        if (e.type == SkType::Circle) {
            const auto& c = m_geo.points[e.p0];
            grow(c.x - e.r, c.y - e.r);
            grow(c.x + e.r, c.y + e.r);
        }
    }
    for (const auto& c : m_geo.constraints) {
        if (!isDimension(c.type) || !any) continue;
        const QPointF a = constraintAnchor(c);
        x0 = std::min(x0, a.x() + c.labelX); x1 = std::max(x1, a.x() + c.labelX);
        y0 = std::min(y0, a.y() + c.labelY); y1 = std::max(y1, a.y() + c.labelY);
    }
    for (const auto& r : m_reference)
        for (const QPointF& q : r.pts) {
            if (!any) { x0 = x1 = q.x(); y0 = y1 = q.y(); any = true; }
            x0 = std::min(x0, q.x()); x1 = std::max(x1, q.x()); y0 = std::min(y0, q.y()); y1 = std::max(y1, q.y());
        }
    if (any) { x0 = std::min(x0, 0.0); y0 = std::min(y0, 0.0); x1 = std::max(x1, 0.0); y1 = std::max(y1, 0.0); }
    const double w = std::max(x1 - x0, 1.0), h = std::max(y1 - y0, 1.0);
    m_center = {(x0 + x1) / 2, (y0 + y1) / 2};
    if (width() > 50 && height() > 50)
        m_scale = std::min((width() - 100) / w, (height() - 100) / h);
    m_scale = std::clamp(m_scale, 0.05, 2000.0);
    update();
}

void SketchCanvas::resizeEvent(QResizeEvent*) { update(); }

// --- Treffertest ----------------------------------------------------------------------

int SketchCanvas::pointAt(const QPointF& screen) const
{
    int best = -1;
    double bestD = 7.0;
    for (size_t i = 0; i < m_geo.points.size(); ++i) {
        const QPointF s = toScreen(m_geo.points[i].x, m_geo.points[i].y);
        const double d = std::hypot(s.x() - screen.x(), s.y() - screen.y());
        if (d < bestD) { bestD = d; best = static_cast<int>(i); }
    }
    return best;
}

SketchCanvas::Hit SketchCanvas::hitTest(const QPointF& screen, bool pointsFirst) const
{
    if (pointsFirst) {
        const int p = pointAt(screen);
        if (p >= 0) return {Hit::Point, p};
    }
    for (size_t i = 0; i < m_geo.constraints.size(); ++i)
        if (isDimension(m_geo.constraints[i].type) && dimensionTextRect(static_cast<int>(i)).adjusted(-3, -3, 3, 3).contains(screen))
            return {Hit::Constraint, static_cast<int>(i)};

    int best = -1;
    double bestD = 6.0;
    for (size_t i = 0; i < m_geo.entities.size(); ++i) {
        const auto& e = m_geo.entities[i];
        const auto& P = m_geo.points;
        double d = 1e9;
        if (e.type == SkType::Line) {
            d = distToSegment(screen, toScreen(P[e.p0].x, P[e.p0].y), toScreen(P[e.p1].x, P[e.p1].y));
        } else {
            const QPointF c = toScreen(P[e.p0].x, P[e.p0].y);
            const double r = m_geo.radius(static_cast<int>(i)) * m_scale;
            d = std::abs(std::hypot(screen.x() - c.x(), screen.y() - c.y()) - r);
            if (e.type == SkType::Arc) {
                const QPointF sp = toSketch(screen);
                const double a = std::atan2(sp.y() - P[e.p0].y, sp.x() - P[e.p0].x);
                double a0 = angleOf(P[e.p0], P[e.p1]), a1 = angleOf(P[e.p0], P[e.p2]);
                auto norm = [](double v) { while (v < 0) v += 2 * M_PI; while (v >= 2 * M_PI) v -= 2 * M_PI; return v; };
                if (norm(a - a0) > norm(a1 - a0)) d = 1e9;
            }
        }
        if (d < bestD) { bestD = d; best = static_cast<int>(i); }
    }
    if (best >= 0) return {Hit::Entity, best};
    if (!pointsFirst) {
        const int p = pointAt(screen);
        if (p >= 0) return {Hit::Point, p};
    }
    return {};
}

int SketchCanvas::pickOrCreatePoint(const QPointF& screen)
{
    const int existing = pointAt(screen);
    if (existing >= 0) return existing;
    const QPointF s = toSketch(screen);
    const int p = m_geo.addPoint(s.x(), s.y());
    // Auf vorhandener Geometrie abgesetzt -> "Punkt auf Objekt"
    const Hit h = hitTest(screen, false);
    if (h.kind == Hit::Entity) {
        SkConstraint c{ConType::PointOnEntity, p, h.index};
        if (m_geo.canAdd(c)) m_geo.addConstraint(c);
    }
    return p;
}

// --- Abhaengigkeiten -------------------------------------------------------------------

void SketchCanvas::addAutoHV(int line)
{
    const auto& e = m_geo.entities[static_cast<size_t>(line)];
    const auto& a = m_geo.points[e.p0];
    const auto& b = m_geo.points[e.p1];
    const double dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
    const double tol = std::tan(4.0 * M_PI / 180.0);
    SkConstraint c;
    if (dy <= dx * tol) c = {ConType::Horizontal, line};
    else if (dx <= dy * tol) c = {ConType::Vertical, line};
    else return;
    if (m_geo.canAdd(c)) m_geo.addConstraint(c);
}

bool SketchCanvas::tryAdd(SkConstraint c, bool allowDriven)
{
    if (m_geo.canAdd(c)) {
        m_geo.addConstraint(c);
        resolve();
        if (m_lastSolve.status != SolveStatus::Ok) {
            m_geo.constraints.pop_back();
            resolve();
            m_message = QString::fromUtf8("Abhängigkeit lässt sich nicht erfüllen und wurde nicht eingefügt.");
            updateStatus();
            return false;
        }
        return true;
    }
    if (allowDriven) {
        const auto answer = QMessageBox::question(this, QString::fromUtf8("Bemaßung"),
            QString::fromUtf8("Diese Bemaßung würde die Skizze überbestimmen.\n"
                              "Soll sie als getriebene Bemaßung (nur zur Anzeige) eingefügt werden?"));
        if (answer == QMessageBox::Yes) {
            c.driving = false;
            c.value = m_geo.measure(c);
            m_geo.addConstraint(c);
            update();
            return true;
        }
        return false;
    }
    m_message = QString::fromUtf8("Nicht möglich: Diese Abhängigkeit würde die Skizze überbestimmen.");
    updateStatus();
    return false;
}

void SketchCanvas::resolve()
{
    m_lastSolve = m_geo.solve();
    updateStatus();
    update();
}

void SketchCanvas::handleConstraintTool(const Hit& h)
{
    if (h.kind == Hit::None || h.kind == Hit::Constraint) return;
    auto isLine = [this](const Hit& x) { return x.kind == Hit::Entity && m_geo.entities[static_cast<size_t>(x.index)].type == SkType::Line; };
    auto isRound = [this](const Hit& x) { return x.kind == Hit::Entity && isRoundEntity(m_geo.entities[static_cast<size_t>(x.index)]); };

    switch (m_tool) {
    case Tool::Horizontal:
    case Tool::Vertical:
        if (isLine(h)) tryAdd({m_tool == Tool::Horizontal ? ConType::Horizontal : ConType::Vertical, h.index});
        return;
    case Tool::Fix:
        if (h.kind == Hit::Point && h.index > 0) {
            SkConstraint c{ConType::Fix, h.index};
            c.value = m_geo.points[h.index].x;
            c.value2 = m_geo.points[h.index].y;
            tryAdd(c);
        }
        return;
    default: break;
    }

    m_picked.push_back(h);
    if (m_picked.size() < 2) { m_selected = h; update(); return; }
    const Hit a = m_picked[0], b = m_picked[1];
    m_picked.clear();
    m_selected = {};

    switch (m_tool) {
    case Tool::Coincident:
        if (a.kind == Hit::Point && b.kind == Hit::Point && a.index != b.index) tryAdd({ConType::Coincident, a.index, b.index});
        else if (a.kind == Hit::Point && b.kind == Hit::Entity) tryAdd({ConType::PointOnEntity, a.index, b.index});
        else if (b.kind == Hit::Point && a.kind == Hit::Entity) tryAdd({ConType::PointOnEntity, b.index, a.index});
        break;
    case Tool::Parallel:
    case Tool::Perpendicular:
        if (isLine(a) && isLine(b) && a.index != b.index)
            tryAdd({m_tool == Tool::Parallel ? ConType::Parallel : ConType::Perpendicular, a.index, b.index});
        break;
    case Tool::Equal:
        if (a.index != b.index && ((isLine(a) && isLine(b)) || (isRound(a) && isRound(b))))
            tryAdd({ConType::Equal, a.index, b.index});
        break;
    case Tool::Tangent:
        if (isLine(a) && isRound(b)) tryAdd({ConType::Tangent, a.index, b.index});
        else if (isRound(a) && isLine(b)) tryAdd({ConType::Tangent, b.index, a.index});
        else if (isRound(a) && isRound(b) && a.index != b.index) tryAdd({ConType::Tangent, a.index, b.index});
        break;
    case Tool::Midpoint:
        if (a.kind == Hit::Point && isLine(b)) tryAdd({ConType::Midpoint, a.index, b.index});
        else if (b.kind == Hit::Point && isLine(a)) tryAdd({ConType::Midpoint, b.index, a.index});
        break;
    default: break;
    }
    updateStatus();
}

void SketchCanvas::handleDimensionTool(const Hit& h, const QPointF& screen)
{
    const QPointF sp = toSketch(screen);
    auto finish = [this, sp](SkConstraint c) {
        const double measured = m_geo.measure(c);
        double entered = 0;
        std::string expr;
        if (!askValue(QString::fromUtf8("Bemaßung"), QString::fromUtf8(constraintName(c.type)),
                      ValueEdit::format(std::abs(measured)), entered, expr)) return;
        c.ref.expr = expr;
        c.value = (c.type == ConType::DistanceX || c.type == ConType::DistanceY || c.type == ConType::Angle)
            ? (measured < 0 ? -entered : entered) : entered;
        // Masszahl dort, wo geklickt wurde
        c.labelX = 0; c.labelY = 0;
        m_geo.constraints.push_back(c);
        const QPointF anchor = constraintAnchor(c);
        m_geo.constraints.pop_back();
        c.labelX = sp.x() - anchor.x();
        c.labelY = sp.y() - anchor.y();
        tryAdd(c, true);
    };

    if (m_picked.empty()) {
        if (h.kind == Hit::Entity && isRoundEntity(m_geo.entities[static_cast<size_t>(h.index)])) {
            const bool arc = m_geo.entities[static_cast<size_t>(h.index)].type == SkType::Arc;
            finish({arc ? ConType::Radius : ConType::Diameter, h.index});
            return;
        }
        if (h.kind == Hit::Entity || h.kind == Hit::Point) {
            m_picked.push_back(h);
            m_selected = h;
            updateStatus();
            update();
        }
        return;
    }

    const Hit a = m_picked[0];
    // Zweiter Klick
    if (a.kind == Hit::Entity && h.kind == Hit::Entity && h.index != a.index
        && m_geo.entities[static_cast<size_t>(h.index)].type == SkType::Line) {
        m_picked.clear(); m_selected = {};
        finish({ConType::Angle, a.index, h.index});
        return;
    }
    if (a.kind == Hit::Point && h.kind == Hit::Point && h.index != a.index) {
        m_picked.push_back(h);
        m_message = QString::fromUtf8("Klicken, um die Bemaßung zu platzieren (über/unter = horizontal, links/rechts = vertikal)");
        updateStatus();
        return;
    }
    if (a.kind == Hit::Entity && h.kind == Hit::None) {
        const auto& e = m_geo.entities[static_cast<size_t>(a.index)];
        m_picked.clear(); m_selected = {};
        finish({ConType::Distance, e.p0, e.p1});
        return;
    }
    if (m_picked.size() == 2 && h.kind == Hit::None) {
        const int p = m_picked[0].index, q = m_picked[1].index;
        m_picked.clear(); m_selected = {};
        const auto& P = m_geo.points;
        const double mx = (P[p].x + P[q].x) / 2, my = (P[p].y + P[q].y) / 2;
        const double ox = std::abs(sp.x() - mx), oy = std::abs(sp.y() - my);
        const double dx = std::abs(P[q].x - P[p].x), dy = std::abs(P[q].y - P[p].y);
        ConType t = ConType::Distance;
        if (dx > 1e-9 && (oy > ox || dy < 1e-9)) t = ConType::DistanceX;
        else if (dy > 1e-9) t = ConType::DistanceY;
        finish({t, p, q});
        return;
    }
    m_picked.clear();
    m_selected = {};
    update();
}

// --- Darstellung ---------------------------------------------------------------------------

QPointF SketchCanvas::constraintAnchor(const SkConstraint& c) const
{
    const auto& P = m_geo.points;
    auto mid = [&](int e) {
        const auto& en = m_geo.entities[static_cast<size_t>(e)];
        if (en.type == SkType::Line) return QPointF((P[en.p0].x + P[en.p1].x) / 2, (P[en.p0].y + P[en.p1].y) / 2);
        return QPointF(P[en.p0].x, P[en.p0].y);
    };
    switch (c.type) {
    case ConType::Distance: case ConType::DistanceX: case ConType::DistanceY: case ConType::Coincident:
        return {(P[c.a].x + P[c.b].x) / 2, (P[c.a].y + P[c.b].y) / 2};
    case ConType::Fix: case ConType::PointOnEntity: case ConType::Midpoint:
        return {P[c.a].x, P[c.a].y};
    case ConType::Angle: case ConType::Parallel: case ConType::Perpendicular: case ConType::Equal: case ConType::Tangent:
        return (mid(c.a) + mid(c.b)) / 2;
    default:
        return mid(c.a);
    }
}

bool SketchCanvas::askValue(const QString& title, const QString& label, const QString& initial, double& value, std::string& expr)
{
    QString text = initial;
    for (;;) {
        bool ok = false;
        text = QInputDialog::getText(this, title,
            label + QString::fromUtf8(":\n(Zahl oder Gleichung, z. B. d0 * 2)"), QLineEdit::Normal, text, &ok);
        if (!ok) return false;
        const std::string st = text.trimmed().toStdString();
        if (fw::isPlainNumber(st)) {
            value = std::abs(fw::evaluate(st, {}));
            expr.clear();
            return true;
        }
        QString err;
        if (m_eval && m_eval(text.trimmed(), value, err)) {
            value = std::abs(value);
            expr = st;
            return true;
        }
        QMessageBox::warning(this, title, QString::fromUtf8("Die Eingabe ist ungültig: %1").arg(err));
    }
}

QString SketchCanvas::dimensionText(const SkConstraint& c) const
{
    const double v = c.driving ? c.value : m_geo.measure(c);
    QString t;
    switch (c.type) {
    case ConType::Diameter: t = QString::fromUtf8("⌀") + fmt(std::abs(v)); break;
    case ConType::Radius: t = "R" + fmt(std::abs(v)); break;
    case ConType::Angle: t = fmt(std::abs(v)) + QString::fromUtf8("°"); break;
    default: t = fmt(std::abs(v)); break;
    }
    if (!c.ref.expr.empty()) t = "fx:" + t; // durch Gleichung bestimmt, wie in Inventor
    return c.driving ? t : "(" + t + ")";
}

QRectF SketchCanvas::dimensionTextRect(int index, QPainter*) const
{
    const auto& c = m_geo.constraints[static_cast<size_t>(index)];
    const QPointF a = constraintAnchor(c);
    const QPointF s = toScreen(a.x() + c.labelX, a.y() + c.labelY);
    QFont f = font();
    f.setPointSizeF(10);
    const QFontMetricsF fm(f);
    const QString text = dimensionText(c);
    const double w = fm.horizontalAdvance(text) + 8, h = fm.height() + 2;
    return {s.x() - w / 2, s.y() - h / 2, w, h};
}

void SketchCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(250, 251, 253));

    // Raster
    double step = 1.0;
    for (double s : {1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0}) {
        step = s;
        if (s * m_scale >= 18) break;
    }
    const QPointF tl = toSketch({0, 0}), br = toSketch({static_cast<double>(width()), static_cast<double>(height())});
    p.setPen(QPen(QColor(232, 235, 240), 1));
    for (double x = std::floor(tl.x() / step) * step; x <= br.x(); x += step) {
        const double sx = toScreen(x, 0).x();
        p.drawLine(QPointF(sx, 0), QPointF(sx, height()));
    }
    for (double y = std::floor(br.y() / step) * step; y <= tl.y(); y += step) {
        const double sy = toScreen(0, y).y();
        p.drawLine(QPointF(0, sy), QPointF(width(), sy));
    }
    const QPointF o = toScreen(0, 0);
    p.setPen(QPen(QColor(220, 120, 120), 1, Qt::DashDotLine));
    p.drawLine(QPointF(0, o.y()), QPointF(width(), o.y()));
    p.setPen(QPen(QColor(110, 180, 110), 1, Qt::DashDotLine));
    p.drawLine(QPointF(o.x(), 0), QPointF(o.x(), height()));

    // Bauteilkanten als Bezug
    for (const auto& r : m_reference) {
        p.setPen(r.inPlane ? QPen(QColor(95, 105, 120), 1.6) : QPen(QColor(190, 196, 205), 1.0));
        QPolygonF poly;
        poly.reserve(r.pts.size());
        for (const QPointF& q : r.pts) poly << toScreen(q.x(), q.y());
        p.drawPolyline(poly);
    }

    const bool full = m_lastSolve.dof == 0;
    const auto& P = m_geo.points;

    // Geometrie
    for (size_t i = 0; i < m_geo.entities.size(); ++i) {
        const auto& e = m_geo.entities[i];
        QColor col = e.construction ? kConstructionColor : (full ? kFullColor : kUnderColor);
        double width = e.construction ? 1.2 : 2.0;
        const bool sel = (m_selected.kind == Hit::Entity && m_selected.index == static_cast<int>(i))
            || std::any_of(m_picked.begin(), m_picked.end(), [i](const Hit& h) { return h.kind == Hit::Entity && h.index == static_cast<int>(i); });
        if (sel) col = kSelectColor;
        else if (m_hover.kind == Hit::Entity && m_hover.index == static_cast<int>(i)) { col = kHoverColor; width += 1; }
        p.setPen(QPen(col, width, e.construction ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        if (e.type == SkType::Line) {
            p.drawLine(toScreen(P[e.p0].x, P[e.p0].y), toScreen(P[e.p1].x, P[e.p1].y));
        } else {
            const QPointF c = toScreen(P[e.p0].x, P[e.p0].y);
            const double r = m_geo.radius(static_cast<int>(i)) * m_scale;
            if (e.type == SkType::Circle) {
                p.drawEllipse(c, r, r);
            } else {
                const double a0 = angleOf(P[e.p0], P[e.p1]) * 180 / M_PI;
                double span = (angleOf(P[e.p0], P[e.p2]) - angleOf(P[e.p0], P[e.p1])) * 180 / M_PI;
                while (span <= 0) span += 360;
                p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), static_cast<int>(a0 * 16), static_cast<int>(span * 16));
            }
        }
    }

    // Punkte
    for (size_t i = 0; i < P.size(); ++i) {
        const QPointF s = toScreen(P[i].x, P[i].y);
        QColor col = i == 0 ? QColor(240, 180, 0) : (full ? kFullColor : kUnderColor);
        const bool sel = (m_selected.kind == Hit::Point && m_selected.index == static_cast<int>(i))
            || std::any_of(m_picked.begin(), m_picked.end(), [i](const Hit& h) { return h.kind == Hit::Point && h.index == static_cast<int>(i); });
        if (sel) col = kSelectColor;
        else if (m_hover.kind == Hit::Point && m_hover.index == static_cast<int>(i)) col = kHoverColor;
        const double r = (i == 0 || sel || (m_hover.kind == Hit::Point && m_hover.index == static_cast<int>(i))) ? 4.5 : 2.5;
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawRect(QRectF(s.x() - r, s.y() - r, 2 * r, 2 * r));
    }

    // Abhaengigkeits-Symbole
    QFont small = font();
    small.setPointSizeF(8);
    small.setBold(true);
    p.setFont(small);
    if (m_showConstraints) {
        std::map<std::pair<int, int>, int> stack;
        for (const auto& c : m_geo.constraints) {
            if (isDimension(c.type)) continue;
            const QPointF a = toScreen(constraintAnchor(c).x(), constraintAnchor(c).y());
            if (c.type == ConType::Coincident) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(240, 180, 0));
                p.drawEllipse(a, 3.5, 3.5);
                continue;
            }
            const auto key = std::make_pair(static_cast<int>(a.x()) / 6, static_cast<int>(a.y()) / 6);
            const int n = stack[key]++;
            const QRectF box(a.x() + 6 + n * 16, a.y() + 6, 14, 14);
            p.setPen(QPen(QColor(120, 130, 145), 1));
            p.setBrush(QColor(255, 255, 255, 230));
            p.drawRoundedRect(box, 2, 2);
            p.setPen(QColor(60, 70, 90));
            p.drawText(box, Qt::AlignCenter, glyph(c.type));
        }
    }

    // Bemassungen
    QFont dimFont = font();
    dimFont.setPointSizeF(10);
    for (size_t i = 0; i < m_geo.constraints.size(); ++i) {
        const auto& c = m_geo.constraints[i];
        if (!isDimension(c.type)) continue;
        const bool sel = (m_selected.kind == Hit::Constraint && m_selected.index == static_cast<int>(i))
            || (m_hover.kind == Hit::Constraint && m_hover.index == static_cast<int>(i));
        const QColor col = sel ? kSelectColor : kDimColor;
        p.setPen(QPen(col, 1));
        const QPointF anchor = constraintAnchor(c);
        const QPointF label(anchor.x() + c.labelX, anchor.y() + c.labelY);
        const QPointF L = toScreen(label.x(), label.y());
        switch (c.type) {
        case ConType::DistanceX: {
            const QPointF a = toScreen(P[c.a].x, P[c.a].y), b = toScreen(P[c.b].x, P[c.b].y);
            p.drawLine(a, QPointF(a.x(), L.y()));
            p.drawLine(b, QPointF(b.x(), L.y()));
            p.drawLine(QPointF(a.x(), L.y()), QPointF(b.x(), L.y()));
            break;
        }
        case ConType::DistanceY: {
            const QPointF a = toScreen(P[c.a].x, P[c.a].y), b = toScreen(P[c.b].x, P[c.b].y);
            p.drawLine(a, QPointF(L.x(), a.y()));
            p.drawLine(b, QPointF(L.x(), b.y()));
            p.drawLine(QPointF(L.x(), a.y()), QPointF(L.x(), b.y()));
            break;
        }
        case ConType::Distance: {
            const double dx = P[c.b].x - P[c.a].x, dy = P[c.b].y - P[c.a].y;
            const double len = std::hypot(dx, dy);
            if (len < 1e-9) break;
            const double nx = -dy / len, ny = dx / len;
            const double off = c.labelX * nx + c.labelY * ny;
            const QPointF a = toScreen(P[c.a].x, P[c.a].y), b = toScreen(P[c.b].x, P[c.b].y);
            const QPointF a2 = toScreen(P[c.a].x + nx * off, P[c.a].y + ny * off);
            const QPointF b2 = toScreen(P[c.b].x + nx * off, P[c.b].y + ny * off);
            p.drawLine(a, a2);
            p.drawLine(b, b2);
            p.drawLine(a2, b2);
            break;
        }
        case ConType::Radius:
        case ConType::Diameter: {
            const auto& e = m_geo.entities[static_cast<size_t>(c.a)];
            const QPointF ctr = toScreen(P[e.p0].x, P[e.p0].y);
            p.drawLine(ctr, L);
            break;
        }
        default: break;
        }
        const QRectF r = dimensionTextRect(static_cast<int>(i));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(250, 251, 253, 235));
        p.drawRect(r);
        p.setFont(dimFont);
        p.setPen(col);
        p.drawText(r, Qt::AlignCenter, dimensionText(c));
    }

    // Vorschau des laufenden Werkzeugs
    p.setPen(QPen(kHoverColor, 1.5, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    if (!m_pending.empty()) {
        const auto& a = P[m_pending.front()];
        const QPointF as = toScreen(a.x, a.y);
        switch (m_tool) {
        case Tool::Line: {
            const auto& last = P[m_pending.back()];
            p.drawLine(toScreen(last.x, last.y), m_mouse);
            break;
        }
        case Tool::Rectangle:
            p.drawRect(QRectF(as, m_mouse).normalized());
            break;
        case Tool::Circle: {
            const double r = std::hypot(m_mouse.x() - as.x(), m_mouse.y() - as.y());
            p.drawEllipse(as, r, r);
            break;
        }
        case Tool::Arc: {
            if (m_pending.size() == 1) {
                p.drawLine(as, m_mouse);
            } else {
                const auto& s = P[m_pending[1]];
                const double r = std::hypot(s.x - a.x, s.y - a.y) * m_scale;
                const QPointF m = toSketch(m_mouse);
                const double a0 = angleOf(a, s) * 180 / M_PI;
                double span = (std::atan2(m.y() - a.y, m.x() - a.x) - angleOf(a, s)) * 180 / M_PI;
                while (span <= 0) span += 360;
                p.drawArc(QRectF(as.x() - r, as.y() - r, 2 * r, 2 * r), static_cast<int>(a0 * 16), static_cast<int>(span * 16));
            }
            break;
        }
        default: break;
        }
    }

    // Koordinatenanzeige
    const QPointF m = toSketch(m_mouse);
    p.setFont(font());
    p.setPen(QColor(90, 100, 115));
    p.drawText(QRectF(8, height() - 24, 300, 20), Qt::AlignLeft | Qt::AlignVCenter,
               QString("X %1  Y %2 mm").arg(m.x(), 0, 'f', 2).arg(m.y(), 0, 'f', 2));
}

// --- Maus ------------------------------------------------------------------------------------

void SketchCanvas::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    const QPointF pos = e->position();
    m_mouse = pos;
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = pos;
        return;
    }
    if (e->button() == Qt::RightButton) {
        // Laufende Eingabe (z. B. Linienzug) beendet der Rechtsklick wie "OK",
        // sonst erscheint das Markierungsmenue
        if (!m_pending.empty() || !m_picked.empty()) {
            cancelOperation();
            m_message.clear();
            updateStatus();
            update();
        } else {
            emit contextMenuAt(e->globalPosition().toPoint());
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    m_message.clear();

    const Hit h = hitTest(pos);
    switch (m_tool) {
    case Tool::Select:
        m_selected = h;
        if (h.kind == Hit::Point && h.index > 0) m_dragPoint = h.index;
        if (h.kind == Hit::Constraint) m_dragLabel = h.index;
        break;
    case Tool::Line: {
        const bool existing = pointAt(pos) >= 0;
        const int p = pickOrCreatePoint(pos);
        if (m_pending.empty()) { m_pending.push_back(p); break; }
        if (p == m_pending.back()) break;
        const int line = m_geo.addLine(m_pending.back(), p);
        addAutoHV(line);
        resolve();
        if (existing) m_pending.clear(); // an vorhandenen Punkt angeschlossen: Linienzug beenden
        else m_pending = {p};
        break;
    }
    case Tool::Rectangle: {
        const int p = pickOrCreatePoint(pos);
        if (m_pending.empty()) { m_pending.push_back(p); break; }
        const auto a = m_geo.points[m_pending[0]];
        const auto c = m_geo.points[p];
        if (std::abs(a.x - c.x) < 1e-6 || std::abs(a.y - c.y) < 1e-6) break;
        const int p1 = m_geo.addPoint(c.x, a.y), p3 = m_geo.addPoint(a.x, c.y);
        const int l0 = m_geo.addLine(m_pending[0], p1), l1 = m_geo.addLine(p1, p);
        const int l2 = m_geo.addLine(p, p3), l3 = m_geo.addLine(p3, m_pending[0]);
        for (auto [l, t] : {std::pair{l0, ConType::Horizontal}, {l2, ConType::Horizontal}, {l1, ConType::Vertical}, {l3, ConType::Vertical}})
            if (m_geo.canAdd({t, l})) m_geo.addConstraint({t, l});
        m_pending.clear();
        resolve();
        break;
    }
    case Tool::Circle: {
        if (m_pending.empty()) { m_pending.push_back(pickOrCreatePoint(pos)); break; }
        const auto& c = m_geo.points[m_pending[0]];
        const QPointF s = toSketch(pos);
        const double r = std::hypot(s.x() - c.x, s.y() - c.y);
        if (r > 1e-6) m_geo.addCircle(m_pending[0], r);
        m_pending.clear();
        resolve();
        break;
    }
    case Tool::Arc: {
        if (m_pending.size() < 2) { m_pending.push_back(pickOrCreatePoint(pos)); break; }
        const auto c = m_geo.points[m_pending[0]];
        const auto s = m_geo.points[m_pending[1]];
        int endPoint = pointAt(pos);
        if (endPoint < 0) {
            const QPointF m = toSketch(pos);
            const double r = std::hypot(s.x - c.x, s.y - c.y);
            const double ang = std::atan2(m.y() - c.y, m.x() - c.x);
            endPoint = m_geo.addPoint(c.x + r * std::cos(ang), c.y + r * std::sin(ang));
        }
        if (endPoint != m_pending[1]) m_geo.addArc(m_pending[0], m_pending[1], endPoint);
        m_pending.clear();
        resolve();
        break;
    }
    case Tool::Dimension:
        handleDimensionTool(h, pos);
        break;
    default:
        handleConstraintTool(h);
        break;
    }
    updateStatus();
    update();
}

void SketchCanvas::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (m_panning) {
        const QPointF d = pos - m_panStart;
        m_center -= QPointF(d.x() / m_scale, -d.y() / m_scale);
        m_panStart = pos;
    } else if (m_dragPoint > 0) {
        const QPointF s = toSketch(pos);
        m_lastSolve = m_geo.drag(m_dragPoint, s.x(), s.y());
        updateStatus();
    } else if (m_dragLabel >= 0) {
        auto& c = m_geo.constraints[static_cast<size_t>(m_dragLabel)];
        const QPointF a = constraintAnchor(c);
        const QPointF s = toSketch(pos);
        c.labelX = s.x() - a.x();
        c.labelY = s.y() - a.y();
    } else {
        m_hover = hitTest(pos);
    }
    m_mouse = pos;
    update();
}

void SketchCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton) m_panning = false;
    if (e->button() == Qt::LeftButton) {
        if (m_dragPoint > 0) resolve();
        m_dragPoint = -1;
        m_dragLabel = -1;
    }
}

void SketchCanvas::mouseDoubleClickEvent(QMouseEvent* e)
{
    const Hit h = hitTest(e->position());
    if (h.kind == Hit::Constraint) editDimension(h.index);
}

void SketchCanvas::editDimension(int index)
{
    auto& c = m_geo.constraints[static_cast<size_t>(index)];
    if (!c.driving) {
        m_message = QString::fromUtf8("Getriebene Bemaßungen zeigen nur den Wert an.");
        updateStatus();
        return;
    }
    double v = 0;
    std::string expr;
    const QString label = QString::fromUtf8(constraintName(c.type)) +
        (c.ref.param.empty() ? QString() : QString(" (%1)").arg(QString::fromStdString(c.ref.param)));
    if (!askValue(QString::fromUtf8("Bemaßung bearbeiten"), label,
                  c.ref.expr.empty() ? ValueEdit::format(std::abs(c.value)) : QString::fromStdString(c.ref.expr), v, expr))
        return;
    const double old = c.value;
    const std::string oldExpr = c.ref.expr;
    c.value = c.value < 0 ? -v : v;
    c.ref.expr = expr;
    resolve();
    if (m_lastSolve.status != SolveStatus::Ok) {
        m_geo.constraints[static_cast<size_t>(index)].value = old;
        m_geo.constraints[static_cast<size_t>(index)].ref.expr = oldExpr;
        resolve();
        m_message = QString::fromUtf8("Dieser Wert lässt sich nicht erreichen.");
        updateStatus();
    }
}

void SketchCanvas::wheelEvent(QWheelEvent* e)
{
    const QPointF before = toSketch(e->position());
    const double factor = std::pow(1.0015, e->angleDelta().y());
    m_scale = std::clamp(m_scale * factor, 0.05, 2000.0);
    const QPointF after = toSketch(e->position());
    m_center += before - after;
    update();
}

void SketchCanvas::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        if (!m_pending.empty() || !m_picked.empty()) cancelOperation();
        else setTool(Tool::Select);
        update();
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        return;
    case Qt::Key_L: setTool(Tool::Line); return;
    case Qt::Key_C: setTool(Tool::Circle); return;
    case Qt::Key_A: setTool(Tool::Arc); return;
    case Qt::Key_R: setTool(Tool::Rectangle); return;
    case Qt::Key_D: setTool(Tool::Dimension); return;
    default: QWidget::keyPressEvent(e);
    }
}

void SketchCanvas::deleteSelection()
{
    switch (m_selected.kind) {
    case Hit::Entity:
        m_geo.removeEntity(m_selected.index);
        break;
    case Hit::Constraint:
        m_geo.removeConstraint(m_selected.index);
        break;
    case Hit::Point: {
        if (m_selected.index == 0) return;
        for (int i = static_cast<int>(m_geo.entities.size()) - 1; i >= 0; --i) {
            const auto& en = m_geo.entities[static_cast<size_t>(i)];
            if (en.p0 == m_selected.index || en.p1 == m_selected.index || en.p2 == m_selected.index) m_geo.removeEntity(i);
        }
        m_geo.purgeUnusedPoints();
        break;
    }
    case Hit::None:
        return;
    }
    m_selected = {};
    m_hover = {};
    resolve();
}

void SketchCanvas::toggleConstruction()
{
    if (m_selected.kind != Hit::Entity) {
        m_message = QString::fromUtf8("Zuerst mit „Auswählen“ eine Linie, einen Kreis oder Bogen anklicken.");
        updateStatus();
        return;
    }
    auto& e = m_geo.entities[static_cast<size_t>(m_selected.index)];
    e.construction = !e.construction;
    update();
}

void SketchCanvas::updateStatus()
{
    QString hint;
    switch (m_tool) {
    case Tool::Select: hint = "Auswählen: Punkte ziehen, Maße doppelklicken, Entf löscht"; break;
    case Tool::Line: hint = m_pending.empty() ? "Linie: Startpunkt klicken" : "Linie: nächsten Punkt klicken (Rechtsklick beendet)"; break;
    case Tool::Rectangle: hint = m_pending.empty() ? "Rechteck: erste Ecke klicken" : "Rechteck: gegenüberliegende Ecke klicken"; break;
    case Tool::Circle: hint = m_pending.empty() ? "Kreis: Mittelpunkt klicken" : "Kreis: Radius festlegen"; break;
    case Tool::Arc: hint = m_pending.empty() ? "Bogen: Mittelpunkt klicken" : (m_pending.size() == 1 ? "Bogen: Startpunkt klicken" : "Bogen: Endpunkt klicken (gegen den Uhrzeigersinn)"); break;
    case Tool::Dimension: hint = m_picked.empty() ? "Bemaßung: Linie, Kreis/Bogen oder Punkt wählen" : "Bemaßung: zweites Objekt wählen oder Maß platzieren"; break;
    default:
        hint = QString::fromUtf8("%1: Objekte nacheinander wählen").arg(QString::fromUtf8(constraintName(
            m_tool == Tool::Coincident ? ConType::Coincident : m_tool == Tool::Horizontal ? ConType::Horizontal
            : m_tool == Tool::Vertical ? ConType::Vertical : m_tool == Tool::Parallel ? ConType::Parallel
            : m_tool == Tool::Perpendicular ? ConType::Perpendicular : m_tool == Tool::Tangent ? ConType::Tangent
            : m_tool == Tool::Equal ? ConType::Equal : m_tool == Tool::Fix ? ConType::Fix : ConType::Midpoint)));
        break;
    }
    QString state;
    if (m_lastSolve.status == SolveStatus::Conflict) state = QString::fromUtf8("⚠ Abhängigkeiten widersprechen sich");
    else if (m_lastSolve.dof == 0) state = QString::fromUtf8("Vollständig bestimmt");
    else state = QString::fromUtf8("Noch %1 Freiheitsgrad(e) offen").arg(m_lastSolve.dof);
    QString text = state + "   ·   " + QString::fromUtf8(hint.toUtf8());
    if (!m_message.isEmpty()) text += "   ·   " + m_message;
    emit statusChanged(text);
}
