#include "slicer/GcodeWriter.h"

#include <cmath>
#include <algorithm>
#include <iomanip>
#include <locale>

namespace fw {

GcodeWriter::GcodeWriter(const PrinterProfile& p) : m_p(p)
{
    m_out.imbue(std::locale::classic());
    m_out << std::fixed;
}

void GcodeWriter::comment(const std::string& c) { m_out << ';' << c << '\n'; }
void GcodeWriter::raw(const std::string& line) { m_out << line << '\n'; }

std::string GcodeWriter::expand(const std::string& tmpl) const
{
    std::string out = tmpl;
    auto replaceAll = [&out](const std::string& key, const std::string& value) {
        for (size_t pos = out.find(key); pos != std::string::npos; pos = out.find(key, pos + value.size()))
            out.replace(pos, key.size(), value);
    };
    auto num = [](double v) {
        std::ostringstream s;
        s.imbue(std::locale::classic());
        s << v;
        return s.str();
    };
    replaceAll("{nozzle_temp}", std::to_string(m_p.nozzleTempFirstLayer));
    replaceAll("{bed_temp}", std::to_string(m_p.bedTempFirstLayer));
    replaceAll("{bed_x}", num(m_p.bedX));
    replaceAll("{bed_y}", num(m_p.bedY));
    return out;
}

void GcodeWriter::startGcode()
{
    m_out << ";FLAVOR:Marlin\n"
          << ";Drucker: " << m_p.name << "\n"
          << "M140 S" << m_p.bedTempFirstLayer << " ; Bett aufheizen\n"
          << "M104 S" << m_p.nozzleTempFirstLayer << " ; Duese aufheizen\n"
          << "M190 S" << m_p.bedTempFirstLayer << " ; auf Bett warten\n"
          << "M109 S" << m_p.nozzleTempFirstLayer << " ; auf Duese warten\n"
          << "M82 ; absolutes Extrudieren\n"
          << "G21 ; Millimeter\n"
          << "G90 ; absolute Koordinaten\n"
          << expand(m_p.startGcode) << '\n'
          << "G92 E0\n"
          << "M107\n";
    // Position nach dem Start-G-Code ist unbekannt: erste Fahrt immer mit Rueckzug-Logik
    m_x = -1000; m_y = -1000; m_z = 2.0; m_e = 0;
}

void GcodeWriter::endGcode()
{
    m_out << ";Ende\n" << expand(m_p.endGcode) << '\n';
}

void GcodeWriter::heightEvent(const HeightEvent& e)
{
    std::ostringstream z;
    z << std::fixed << std::setprecision(2) << e.z;
    m_out << ";EREIGNIS bei Z=" << z.str() << ": " << heightEventName(e.type) << '\n';
    const int v = static_cast<int>(std::lround(e.value));
    switch (e.type) {
    case HeightEventType::Pause:
    case HeightEventType::FilamentChange: {
        const double px = m_x, py = m_y, pz = m_z;
        const bool known = px > -999;
        retract();
        const double liftZ = std::min(m_z + m_p.pauseLiftZ, m_p.maxZ);
        m_out << std::setprecision(3) << "G0 Z" << liftZ << " F600\n"
              << "G0 X" << m_p.pauseParkX << " Y" << m_p.pauseParkY << " F6000\n";
        if (e.type == HeightEventType::FilamentChange) {
            m_out << "M600 ; Filamentwechsel (Bedienung am Display)\n";
        } else {
            const std::string msg = e.gcode.empty() ? "Pause - Klick zum Fortsetzen" : e.gcode;
            m_out << "M400\nM117 " << msg << "\nM0 " << msg << '\n';
        }
        m_out << "M109 S" << m_p.nozzleTemp << " ; Temperatur sicherstellen\n"
              << "G92 E" << std::setprecision(5) << m_e << " ; E-Zaehler wiederherstellen\n";
        if (known) m_out << std::setprecision(3) << "G0 X" << px << " Y" << py << " F6000\n";
        m_out << std::setprecision(3) << "G0 Z" << pz << " F600\n";
        break;
    }
    case HeightEventType::NozzleTemp: m_out << "M104 S" << v << '\n'; break;
    case HeightEventType::BedTemp: m_out << "M140 S" << v << '\n'; break;
    case HeightEventType::FanPercent: setFan(v); break;
    case HeightEventType::SpeedPercent: m_out << "M220 S" << v << '\n'; break;
    case HeightEventType::FlowPercent: m_out << "M221 S" << v << '\n'; break;
    case HeightEventType::CustomGcode: m_out << e.gcode << '\n'; break;
    }
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
    if (m_x > -999) m_seconds += d / m_p.travelSpeed;
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
    m_e += d * area / filamentArea * m_p.flowPercent / 100.0;
    m_out << "G1 F" << std::setprecision(0) << speedMmS * 60
          << std::setprecision(3) << " X" << x << " Y" << y
          << " E" << std::setprecision(5) << m_e << '\n';
    m_seconds += d / speedMmS;
    m_x = x; m_y = y;
}

} // namespace fw
