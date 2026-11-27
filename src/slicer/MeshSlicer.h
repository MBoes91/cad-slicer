#pragma once

#include <TopoDS_Shape.hxx>

#include <array>
#include <vector>

namespace fw {

struct Vec2 { double x, y; };
using Polygon2 = std::vector<Vec2>;

struct Triangle { std::array<std::array<double, 3>, 3> v; };

// Trianguliert einen B-Rep-Koerper.
std::vector<Triangle> triangulate(const TopoDS_Shape& shape, double deflection = 0.01);

// Schneidet das Dreiecksnetz in Hoehe z und liefert die geschlossenen
// Konturen (Orientierung beliebig; Auswertung per Even-Odd-Regel).
std::vector<Polygon2> sliceAt(const std::vector<Triangle>& tris, double z);

} // namespace fw
