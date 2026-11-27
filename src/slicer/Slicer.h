#pragma once

#include "slicer/MeshSlicer.h"
#include "slicer/PrinterProfile.h"
#include "core/PrintModifier.h"

#include <TopoDS_Shape.hxx>
#include <gp_Vec.hxx>

#include <functional>
#include <string>
#include <vector>

namespace fw {

enum class PathType { Skirt, OuterWall, InnerWall, Solid, Infill, Brim, Support };

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
    bool cancelled = false;
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
// progress(0..1) wird regelmaessig aufgerufen; gibt sie false zurueck, wird abgebrochen.
using ProgressFn = std::function<bool(double)>;
SliceResult slice(const TopoDS_Shape& body, const PrinterProfile& profile, const ProgressFn& progress = {},
                  const std::vector<PrintModifier>& modifiers = {});

// Verschiebung, mit der der Slicer das Teil auf das Bett stellt (mittig, Unterseite auf Z=0).
gp_Vec bedPlacement(const TopoDS_Shape& body, const PrinterProfile& profile);

} // namespace fw
