#include "slicer/GcodeWriter.h"

#include <cmath>
#include <iomanip>

namespace fw {

GcodeWriter::GcodeWriter(const PrinterProfile& p) : m_p(p)
{
    m_out << std::fixed;
}

void GcodeWriter::comment(const std::string& c) { m_out << ';' << c << '\n'; }
void GcodeWriter::raw(const std::string& line) { m_out << line << '\n'; }

void GcodeWriter::startGcode()
{
    m_out << ";FLAVOR:Marlin\n"
          << ";Drucker: " << m_p.name << "\n"
          << "M140 S" << m_p.bedTemp << " ; Bett aufheizen\n"
          << "M104 S" << m_p.nozzleTemp << " ; Duese aufheizen\n"
          << "M190 S" << m_p.bedTemp << " ; auf Bett warten\n"
          << "M109 S" << m_p.nozzleTemp << " ; auf Duese warten\n"
          << "M82 ; absolutes Extrudieren\n"
          << "G21 ; Millimeter\n"
          << "G90 ; absolute Koordinaten\n"
          << "G92 E0\n"
          << "G28 ; Referenzfahrt\n"
          << "G1 Z2.0 F3000\n"
          << "G1 X0.1 Y20 Z0.3 F5000.0 ; Reinigungslinie\n"
          << "G1 X0.1 Y200.0 Z0.3 F1500.0 E15\n"
          << "G1 X0.4 Y200.0 Z0.3 F5000.0\n"
          << "G1 X0.4 Y20 Z0.3 F1500.0 E30\n"
          << "G92 E0\n"
          << "G1 Z2.0 F3000\n"
          << "M107\n";
    m_x = 0.4; m_y = 20; m_z = 2.0; m_e = 0;
}

void GcodeWriter::endGcode()
{
    m_out << ";Ende\n"
          << "G91 ; relativ\n"
          << "G1 E-2 F2700\n"
          << "G1 E-2 Z0.2 F2400\n"
          << "G1 X5 Y5 F3000\n"
          << "G1 Z10\n"
          << "G90 ; absolut\n"
          << "G1 X0 Y" << m_p.bedY << " ; Teil praesentieren\n"
          << "M106 S0\n"
          << "M104 S0\n"
          << "M140 S0\n"
          << "M84 X Y E\n";
}

void GcodeWriter::setFan(int percent)
{
    const int pwm = static_cast<int>(std::lround(255.0 * percent / 100.0));
    if (pwm <= 0) m_out << "M107\n";
    else m_out << "M106 S" << pwm << '\n';
}

void GcodeWriter::moveZ(double z)
{
    m_out << std::setprecision(3) << "G0 Z" << z << " F600\n";
    m_seconds += std::abs(z - m_z) / 10.0;
    m_z = z;
}

void GcodeWriter::travel(double x, double y)
{
    const double d = std::hypot(x - m_x, y - m_y);
    if (d < 1e-4) return;
    const bool doRetract = d > m_p.retractMinTravel;
    if (doRetract) retract();
    if (doRetract && m_p.zHop > 0)
        m_out << std::setprecision(3) << "G0 Z" << m_z + m_p.zHop << '\n';
    m_out << std::setprecision(3) << "G0 F" << std::setprecision(0) << m_p.travelSpeed * 60
          << std::setprecision(3) << " X" << x << " Y" << y << '\n';
    if (doRetract && m_p.zHop > 0)
        m_out << std::setprecision(3) << "G0 Z" << m_z << '\n';
    m_seconds += d / m_p.travelSpeed;
    m_x = x; m_y = y;
}

void GcodeWriter::retract()
{
    if (m_retracted || m_p.retractLength <= 0) return;
    m_e -= m_p.retractLength;
    m_out << "G1 F" << std::setprecision(0) << m_p.retractSpeed * 60
          << " E" << std::setprecision(5) << m_e << '\n';
    m_seconds += m_p.retractLength / m_p.retractSpeed;
    m_retracted = true;
}

void GcodeWriter::unretract()
{
    if (!m_retracted) return;
    m_e += m_p.retractLength;
    m_out << "G1 F" << std::setprecision(0) << m_p.retractSpeed * 60
          << " E" << std::setprecision(5) << m_e << '\n';
    m_seconds += m_p.retractLength / m_p.retractSpeed;
    m_retracted = false;
}

void GcodeWriter::extrudeTo(double x, double y, double speedMmS, double layerHeight)
{
    unretract();
    const double d = std::hypot(x - m_x, y - m_y);
    if (d < 1e-4) return;
    const double filamentArea = M_PI * m_p.filamentDiameter * m_p.filamentDiameter / 4.0;
    // Querschnitt als Rechteck mit Halbkreisen (wie Slic3r/Prusa)
    const double w = m_p.lineWidth, h = layerHeight;
    const double area = (w - h) * h + M_PI * h * h / 4.0;
    m_e += d * area / filamentArea;
    m_out << "G1 F" << std::setprecision(0) << speedMmS * 60
          << std::setprecision(3) << " X" << x << " Y" << y
          << " E" << std::setprecision(5) << m_e << '\n';
    m_seconds += d / speedMmS;
    m_x = x; m_y = y;
}

} // namespace fw
