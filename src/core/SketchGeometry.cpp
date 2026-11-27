#include "core/SketchGeometry.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace fw {

const char* constraintName(ConType t)
{
    switch (t) {
    case ConType::Coincident: return "Koinzident";
    case ConType::Horizontal: return "Horizontal";
    case ConType::Vertical: return "Vertikal";
    case ConType::Parallel: return "Parallel";
    case ConType::Perpendicular: return "Senkrecht";
    case ConType::Equal: return "Gleich";
    case ConType::Fix: return "Fixiert";
    case ConType::Tangent: return "Tangential";
    case ConType::PointOnEntity: return "Punkt auf Objekt";
    case ConType::Midpoint: return "Mittelpunkt";
    case ConType::Distance: return "Abstand";
    case ConType::DistanceX: return "Horizontaler Abstand";
    case ConType::DistanceY: return "Vertikaler Abstand";
    case ConType::Radius: return "Radius";
    case ConType::Diameter: return "Durchmesser";
    case ConType::Angle: return "Winkel";
    }
    return "?";
}

bool isDimension(ConType t)
{
    return t == ConType::Distance || t == ConType::DistanceX || t == ConType::DistanceY
        || t == ConType::Radius || t == ConType::Diameter || t == ConType::Angle;
}

SketchGeometry::SketchGeometry()
{
    points.push_back({0, 0}); // Ursprung
}

int SketchGeometry::addPoint(double x, double y)
{
    points.push_back({x, y});
    return static_cast<int>(points.size()) - 1;
}

int SketchGeometry::addLine(int p0, int p1, bool construction)
{
    SkEntity e;
    e.type = SkType::Line;
    e.p0 = p0; e.p1 = p1;
    e.construction = construction;
    entities.push_back(e);
    return static_cast<int>(entities.size()) - 1;
}

int SketchGeometry::addCircle(int center, double r, bool construction)
{
    SkEntity e;
    e.type = SkType::Circle;
    e.p0 = center; e.r = r;
    e.construction = construction;
    entities.push_back(e);
    return static_cast<int>(entities.size()) - 1;
}

int SketchGeometry::addArc(int center, int start, int end, bool construction)
{
    SkEntity e;
    e.type = SkType::Arc;
    e.p0 = center; e.p1 = start; e.p2 = end;
    e.construction = construction;
    entities.push_back(e);
    return static_cast<int>(entities.size()) - 1;
}

int SketchGeometry::addConstraint(const SkConstraint& c)
{
    constraints.push_back(c);
    return static_cast<int>(constraints.size()) - 1;
}

double SketchGeometry::radius(int e) const
{
    const auto& en = entities[static_cast<size_t>(e)];
    if (en.type == SkType::Circle) return en.r;
    const auto& c = points[static_cast<size_t>(en.p0)];
    const auto& s = points[static_cast<size_t>(en.p1)];
    return std::hypot(s.x - c.x, s.y - c.y);
}

// --- Zustandsvektor: alle Punkte (x, y), danach Kreisradien -------------------
std::vector<double> SketchGeometry::state() const
{
    std::vector<double> x;
    x.reserve(points.size() * 2 + entities.size());
    for (const auto& p : points) { x.push_back(p.x); x.push_back(p.y); }
    for (const auto& e : entities) if (e.type == SkType::Circle) x.push_back(e.r);
    return x;
}

void SketchGeometry::setState(const std::vector<double>& x)
{
    size_t k = 0;
    for (auto& p : points) { p.x = x[k++]; p.y = x[k++]; }
    for (auto& e : entities) if (e.type == SkType::Circle) e.r = x[k++];
}

namespace {

struct View {
    const std::vector<double>& x;
    const std::vector<SkEntity>& ents;
    std::vector<int> radiusIndex; // Entitaet -> Index im Zustand (nur Kreise)

    double px(int p) const { return x[static_cast<size_t>(2 * p)]; }
    double py(int p) const { return x[static_cast<size_t>(2 * p + 1)]; }
    double r(int e) const
    {
        const auto& en = ents[static_cast<size_t>(e)];
        if (en.type == SkType::Circle) return x[static_cast<size_t>(radiusIndex[static_cast<size_t>(e)])];
        return std::hypot(px(en.p1) - px(en.p0), py(en.p1) - py(en.p0));
    }
    void dir(int line, double& dx, double& dy) const
    {
        const auto& en = ents[static_cast<size_t>(line)];
        dx = px(en.p1) - px(en.p0);
        dy = py(en.p1) - py(en.p0);
    }
};

bool isRound(const SkEntity& e) { return e.type == SkType::Circle || e.type == SkType::Arc; }

} // namespace

std::vector<double> SketchGeometry::residuals(const std::vector<double>& x, const std::vector<SkConstraint>& cons) const
{
    View v{x, entities, std::vector<int>(entities.size(), -1)};
    size_t k = points.size() * 2;
    for (size_t i = 0; i < entities.size(); ++i)
        if (entities[i].type == SkType::Circle) v.radiusIndex[i] = static_cast<int>(k++);

    std::vector<double> f;
    // Ursprung fest
    f.push_back(v.px(0) - 0.0);
    f.push_back(v.py(0) - 0.0);
    // Boegen: Start und Ende gleich weit von der Mitte
    for (const auto& e : entities)
        if (e.type == SkType::Arc)
            f.push_back(std::hypot(v.px(e.p2) - v.px(e.p0), v.py(e.p2) - v.py(e.p0))
                      - std::hypot(v.px(e.p1) - v.px(e.p0), v.py(e.p1) - v.py(e.p0)));

    for (const auto& c : cons) {
        if (!c.driving) continue;
        switch (c.type) {
        case ConType::Coincident:
            f.push_back(v.px(c.a) - v.px(c.b));
            f.push_back(v.py(c.a) - v.py(c.b));
            break;
        case ConType::Horizontal: {
            const auto& e = entities[static_cast<size_t>(c.a)];
            f.push_back(v.py(e.p1) - v.py(e.p0));
            break;
        }
        case ConType::Vertical: {
            const auto& e = entities[static_cast<size_t>(c.a)];
            f.push_back(v.px(e.p1) - v.px(e.p0));
            break;
        }
        case ConType::Parallel:
        case ConType::Perpendicular: {
            double ax, ay, bx, by;
            v.dir(c.a, ax, ay);
            v.dir(c.b, bx, by);
            const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
            if (la < 1e-12 || lb < 1e-12) { f.push_back(0); break; }
            f.push_back(c.type == ConType::Parallel ? (ax * by - ay * bx) / (la * lb) : (ax * bx + ay * by) / (la * lb));
            break;
        }
        case ConType::Equal: {
            const auto& ea = entities[static_cast<size_t>(c.a)];
            const auto& eb = entities[static_cast<size_t>(c.b)];
            if (isRound(ea) && isRound(eb)) {
                f.push_back(v.r(c.a) - v.r(c.b));
            } else {
                double ax, ay, bx, by;
                v.dir(c.a, ax, ay);
                v.dir(c.b, bx, by);
                f.push_back(std::hypot(ax, ay) - std::hypot(bx, by));
            }
            break;
        }
        case ConType::Fix:
            f.push_back(v.px(c.a) - c.value);
            f.push_back(v.py(c.a) - c.value2);
            break;
        case ConType::Tangent: {
            const auto& ea = entities[static_cast<size_t>(c.a)];
            const auto& eb = entities[static_cast<size_t>(c.b)];
            if (ea.type == SkType::Line) {
                double dx, dy;
                v.dir(c.a, dx, dy);
                const double l = std::hypot(dx, dy);
                const double cx = v.px(eb.p0) - v.px(ea.p0), cy = v.py(eb.p0) - v.py(ea.p0);
                const double dist = l < 1e-12 ? 0 : std::abs(dx * cy - dy * cx) / l;
                f.push_back(dist - v.r(c.b));
            } else {
                const double d = std::hypot(v.px(ea.p0) - v.px(eb.p0), v.py(ea.p0) - v.py(eb.p0));
                f.push_back(d - (v.r(c.a) + v.r(c.b)));
            }
            break;
        }
        case ConType::PointOnEntity: {
            const auto& e = entities[static_cast<size_t>(c.b)];
            if (e.type == SkType::Line) {
                double dx, dy;
                v.dir(c.b, dx, dy);
                const double l = std::hypot(dx, dy);
                const double qx = v.px(c.a) - v.px(e.p0), qy = v.py(c.a) - v.py(e.p0);
                f.push_back(l < 1e-12 ? 0 : (dx * qy - dy * qx) / l);
            } else {
                f.push_back(std::hypot(v.px(c.a) - v.px(e.p0), v.py(c.a) - v.py(e.p0)) - v.r(c.b));
            }
            break;
        }
        case ConType::Midpoint: {
            const auto& e = entities[static_cast<size_t>(c.b)];
            f.push_back(v.px(c.a) - (v.px(e.p0) + v.px(e.p1)) / 2);
            f.push_back(v.py(c.a) - (v.py(e.p0) + v.py(e.p1)) / 2);
            break;
        }
        case ConType::Distance:
            f.push_back(std::hypot(v.px(c.b) - v.px(c.a), v.py(c.b) - v.py(c.a)) - c.value);
            break;
        case ConType::DistanceX:
            f.push_back((v.px(c.b) - v.px(c.a)) - c.value);
            break;
        case ConType::DistanceY:
            f.push_back((v.py(c.b) - v.py(c.a)) - c.value);
            break;
        case ConType::Radius:
            f.push_back(v.r(c.a) - c.value);
            break;
        case ConType::Diameter:
            f.push_back(2 * v.r(c.a) - c.value);
            break;
        case ConType::Angle: {
            double ax, ay, bx, by;
            v.dir(c.a, ax, ay);
            v.dir(c.b, bx, by);
            double ang = std::atan2(ax * by - ay * bx, ax * bx + ay * by) - c.value * M_PI / 180.0;
            while (ang > M_PI) ang -= 2 * M_PI;
            while (ang < -M_PI) ang += 2 * M_PI;
            f.push_back(ang);
            break;
        }
        }
    }
    return f;
}

namespace {

using Matrix = std::vector<std::vector<double>>;

Matrix jacobian(const std::function<std::vector<double>(const std::vector<double>&)>& F, std::vector<double> x, size_t m)
{
    Matrix J(m, std::vector<double>(x.size(), 0.0));
    for (size_t j = 0; j < x.size(); ++j) {
        const double h = 1e-7 * std::max(1.0, std::abs(x[j]));
        const double old = x[j];
        x[j] = old + h;
        const auto fp = F(x);
        x[j] = old - h;
        const auto fm = F(x);
        x[j] = old;
        for (size_t i = 0; i < m; ++i) J[i][j] = (fp[i] - fm[i]) / (2 * h);
    }
    return J;
}

// Loest A x = b (A symmetrisch positiv definit) per Gauss mit Pivotsuche.
bool solveLinear(Matrix A, std::vector<double> b, std::vector<double>& x)
{
    const size_t n = b.size();
    for (size_t col = 0; col < n; ++col) {
        size_t piv = col;
        for (size_t r = col + 1; r < n; ++r)
            if (std::abs(A[r][col]) > std::abs(A[piv][col])) piv = r;
        if (std::abs(A[piv][col]) < 1e-300) return false;
        std::swap(A[piv], A[col]);
        std::swap(b[piv], b[col]);
        for (size_t r = col + 1; r < n; ++r) {
            const double f = A[r][col] / A[col][col];
            if (f == 0) continue;
            for (size_t c = col; c < n; ++c) A[r][c] -= f * A[col][c];
            b[r] -= f * b[col];
        }
    }
    x.assign(n, 0.0);
    for (size_t i = n; i-- > 0;) {
        double s = b[i];
        for (size_t c = i + 1; c < n; ++c) s -= A[i][c] * x[c];
        x[i] = s / A[i][i];
    }
    return true;
}

double norm2(const std::vector<double>& v)
{
    double s = 0;
    for (double d : v) s += d * d;
    return s;
}

int matrixRank(Matrix J)
{
    if (J.empty()) return 0;
    const size_t m = J.size(), n = J[0].size();
    double maxAbs = 0;
    for (const auto& r : J) for (double d : r) maxAbs = std::max(maxAbs, std::abs(d));
    const double tol = 1e-7 * std::max(1.0, maxAbs);
    int rank = 0;
    size_t row = 0;
    for (size_t col = 0; col < n && row < m; ++col) {
        size_t piv = row;
        for (size_t r = row + 1; r < m; ++r)
            if (std::abs(J[r][col]) > std::abs(J[piv][col])) piv = r;
        if (std::abs(J[piv][col]) < tol) continue;
        std::swap(J[piv], J[row]);
        for (size_t r = row + 1; r < m; ++r) {
            const double f = J[r][col] / J[row][col];
            for (size_t c = col; c < n; ++c) J[r][c] -= f * J[row][c];
        }
        ++row;
        ++rank;
    }
    return rank;
}

} // namespace

SolveResult SketchGeometry::solveImpl(int dragPoint, double dragX, double dragY)
{
    SolveResult res;
    std::vector<double> x = state();
    if (dragPoint > 0) {
        x[static_cast<size_t>(2 * dragPoint)] = dragX;
        x[static_cast<size_t>(2 * dragPoint + 1)] = dragY;
    }
    const auto F = [this](const std::vector<double>& s) { return residuals(s, constraints); };

    // Levenberg-Marquardt; die Daempfung haelt die Loesung nahe am Ausgangszustand
    std::vector<double> f = F(x);
    double err = norm2(f);
    double lambda = 1e-3;
    for (int iter = 0; iter < 200 && err > 1e-20; ++iter) {
        const Matrix J = jacobian(F, x, f.size());
        const size_t n = x.size();
        Matrix A(n, std::vector<double>(n, 0.0));
        std::vector<double> g(n, 0.0);
        for (size_t i = 0; i < f.size(); ++i)
            for (size_t a = 0; a < n; ++a) {
                if (J[i][a] == 0) continue;
                g[a] -= J[i][a] * f[i];
                for (size_t b = 0; b < n; ++b) A[a][b] += J[i][a] * J[i][b];
            }
        bool improved = false;
        for (int tries = 0; tries < 12; ++tries) {
            Matrix Ad = A;
            for (size_t a = 0; a < n; ++a) Ad[a][a] += lambda * (1.0 + A[a][a]);
            std::vector<double> delta;
            if (!solveLinear(Ad, g, delta)) { lambda *= 10; continue; }
            std::vector<double> xn = x;
            for (size_t a = 0; a < n; ++a) xn[a] += delta[a];
            const auto fn = F(xn);
            const double en = norm2(fn);
            if (en < err) {
                x = xn; f = fn; err = en;
                lambda = std::max(1e-12, lambda / 10);
                improved = true;
                break;
            }
            lambda *= 10;
        }
        if (!improved) break;
    }
    // Kreisradien muessen positiv bleiben
    setState(x);
    for (auto& e : entities) if (e.type == SkType::Circle) e.r = std::abs(e.r);

    res.residual = std::sqrt(err);
    res.status = res.residual < 1e-6 ? SolveStatus::Ok : SolveStatus::Conflict;
    res.dof = degreesOfFreedom();
    return res;
}

SolveResult SketchGeometry::solve() { return solveImpl(-1, 0, 0); }

SolveResult SketchGeometry::drag(int p, double x, double y) { return solveImpl(p, x, y); }

int SketchGeometry::rank(const std::vector<double>& x, const std::vector<SkConstraint>& cons) const
{
    const auto F = [this, &cons](const std::vector<double>& s) { return residuals(s, cons); };
    const auto f = F(x);
    return matrixRank(jacobian(F, x, f.size()));
}

int SketchGeometry::degreesOfFreedom() const
{
    const auto x = state();
    return static_cast<int>(x.size()) - rank(x, constraints);
}

bool SketchGeometry::canAdd(const SkConstraint& c) const
{
    if (!c.driving) return true;
    const auto x = state();
    std::vector<SkConstraint> with = constraints;
    with.push_back(c);
    const auto f0 = residuals(x, constraints);
    const auto f1 = residuals(x, with);
    // Jede neue Gleichung muss den Rang erhoehen (sonst ueberbestimmt/redundant)
    const int added = static_cast<int>(f1.size() - f0.size());
    return rank(x, with) - rank(x, constraints) == added;
}

double SketchGeometry::measure(const SkConstraint& c) const
{
    SkConstraint probe = c;
    probe.value = 0;
    probe.value2 = 0;
    probe.driving = true;
    const auto f = residuals(state(), {probe});
    // Residuum bei value = 0 ist genau der Messwert (bzw. dessen Negativ beim Winkel)
    const double r = f.back();
    if (c.type == ConType::Angle) return r * 180.0 / M_PI;
    return r;
}

void SketchGeometry::removeConstraint(int c)
{
    if (c >= 0 && c < static_cast<int>(constraints.size())) constraints.erase(constraints.begin() + c);
}

void SketchGeometry::removeEntity(int e)
{
    if (e < 0 || e >= static_cast<int>(entities.size())) return;
    // Abhaengigkeiten, die die Entitaet benutzen, entfernen und Indizes nachziehen
    auto usesEntity = [](const SkConstraint& c) {
        switch (c.type) {
        case ConType::Horizontal: case ConType::Vertical: case ConType::Radius: case ConType::Diameter:
            return std::make_pair(true, false);
        case ConType::Parallel: case ConType::Perpendicular: case ConType::Equal: case ConType::Tangent:
        case ConType::Angle:
            return std::make_pair(true, true);
        case ConType::PointOnEntity: case ConType::Midpoint:
            return std::make_pair(false, true);
        default:
            return std::make_pair(false, false);
        }
    };
    std::vector<SkConstraint> kept;
    for (auto c : constraints) {
        const auto [ua, ub] = usesEntity(c);
        if ((ua && c.a == e) || (ub && c.b == e)) continue;
        if (ua && c.a > e) --c.a;
        if (ub && c.b > e) --c.b;
        kept.push_back(c);
    }
    constraints = kept;
    entities.erase(entities.begin() + e);
    purgeUnusedPoints();
}

void SketchGeometry::purgeUnusedPoints()
{
    std::vector<int> used(points.size(), 0);
    used[0] = 1;
    for (const auto& e : entities) {
        for (int p : {e.p0, e.p1, e.p2}) if (p >= 0) used[static_cast<size_t>(p)] = 1;
    }
    // Punkte, die nur noch in Abhaengigkeiten vorkommen, werden mit entfernt
    std::vector<int> map(points.size(), -1);
    std::vector<SkPoint> np;
    for (size_t i = 0; i < points.size(); ++i)
        if (used[i]) { map[i] = static_cast<int>(np.size()); np.push_back(points[i]); }
    auto pointArgs = [](const SkConstraint& c) {
        switch (c.type) {
        case ConType::Coincident: case ConType::Distance: case ConType::DistanceX: case ConType::DistanceY:
            return std::make_pair(true, true);
        case ConType::Fix: case ConType::PointOnEntity: case ConType::Midpoint:
            return std::make_pair(true, false);
        default:
            return std::make_pair(false, false);
        }
    };
    std::vector<SkConstraint> kept;
    for (auto c : constraints) {
        const auto [pa, pb] = pointArgs(c);
        if (pa) { if (map[static_cast<size_t>(c.a)] < 0) continue; c.a = map[static_cast<size_t>(c.a)]; }
        if (pb) { if (map[static_cast<size_t>(c.b)] < 0) continue; c.b = map[static_cast<size_t>(c.b)]; }
        kept.push_back(c);
    }
    constraints = kept;
    for (auto& e : entities)
        for (int* p : {&e.p0, &e.p1, &e.p2}) if (*p >= 0) *p = map[static_cast<size_t>(*p)];
    points = np;
}

} // namespace fw
