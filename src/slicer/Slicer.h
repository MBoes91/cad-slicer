#pragma once

#include "slicer/MeshSlicer.h"
#include "slicer/PrinterProfile.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace fw {

enum class PathType { Skirt, OuterWall, InnerWall, Solid, Infill, Brim };

struct ToolPath {
    PathType type;
    Polygon2 points;
    bool closed;
};

struct SlicedLayer {
    double z;
    double height;
    std::vector<ToolPath> paths;
    std::vector<HeightEvent> events; // vor dieser Schicht ausgefuehrt
};

struct SliceResult {
    bool ok = false;
    std::string error;
    std::string gcode;
    std::vector<SlicedLayer> layers;   // fuer die Schichtvorschau
    std::vector<std::string> warnings;
    double filamentMm = 0;
    double filamentGrams = 0;
    double filamentCost = 0;
    double estimatedSeconds = 0;
    double sizeX = 0, sizeY = 0, sizeZ = 0;
};

// Slicer: Koerper -> Schichten -> Wand/Fuellung -> Marlin-G-Code.
// Das Teil wird mittig auf das Druckbett gestellt (tiefster Punkt auf Z=0).
SliceResult slice(const TopoDS_Shape& body, const PrinterProfile& profile);

} // namespace fw
