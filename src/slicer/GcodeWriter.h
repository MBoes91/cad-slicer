#pragma once

#include "slicer/PrinterProfile.h"

#include <sstream>
#include <string>

namespace fw {

// Schreibt Marlin-G-Code (absolute Koordinaten, absolutes E).
class GcodeWriter {
public:
    explicit GcodeWriter(const PrinterProfile& p);

    void comment(const std::string& c);
    void raw(const std::string& line);
    void startGcode();
    void endGcode();
    void setFan(int percent);
    void moveZ(double z);
    void travel(double x, double y);
    void extrudeTo(double x, double y, double speedMmS, double layerHeight);
    void retract();
    void unretract();
    void heightEvent(const HeightEvent& e);

    // Ersetzt {nozzle_temp}, {bed_temp}, {bed_x}, {bed_y}
    std::string expand(const std::string& tmpl) const;

    double filamentUsedMm() const { return m_e; }
    double estimatedSeconds() const { return m_seconds; }
    std::string str() const { return m_out.str(); }

    double x() const { return m_x; }
    double y() const { return m_y; }
    double z() const { return m_z; }

private:
    const PrinterProfile& m_p;
    std::ostringstream m_out;
    double m_x = 0, m_y = 0, m_z = 0, m_e = 0;
    double m_seconds = 0;
    bool m_retracted = false;
};

} // namespace fw
