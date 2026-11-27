#pragma once

#include "core/Features.h"
#include "slicer/PrinterProfile.h"

#include <QDialog>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QTableWidget;

// Neue Skizze: Ursprungsebene und Abstand waehlen
class NewSketchDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewSketchDialog(QWidget* parent = nullptr);
    fw::SketchPlane plane() const;
    double offset() const;
private:
    QComboBox* m_plane;
    QDoubleSpinBox* m_offset;
};

// Skizzenelemente mit ihren Bemassungen bearbeiten
class SketchEditDialog : public QDialog {
    Q_OBJECT
public:
    SketchEditDialog(const fw::Sketch& sketch, QWidget* parent = nullptr);
    void applyTo(fw::Sketch& sketch) const;
private:
    void addRow(const QString& type, double a, double b, double c, double d);
    QTableWidget* m_table;
};

// Extrusion anlegen/bearbeiten
class ExtrudeDialog : public QDialog {
    Q_OBJECT
public:
    ExtrudeDialog(const std::vector<fw::SketchPtr>& sketches, QWidget* parent = nullptr);
    void setValues(const fw::ExtrudeFeature& f);
    fw::SketchPtr sketch() const;
    double distance() const;
    fw::BooleanOp operation() const;
    bool symmetric() const;
private:
    std::vector<fw::SketchPtr> m_sketches;
    QComboBox* m_sketch;
    QDoubleSpinBox* m_distance;
    QComboBox* m_op;
    QCheckBox* m_symmetric;
};

// Drehung anlegen/bearbeiten
class RevolveDialog : public QDialog {
    Q_OBJECT
public:
    RevolveDialog(const std::vector<fw::SketchPtr>& sketches, QWidget* parent = nullptr);
    void setValues(const fw::RevolveFeature& f);
    fw::SketchPtr sketch() const;
    fw::RevolveAxis axis() const;
    double angle() const;
    fw::BooleanOp operation() const;
private:
    std::vector<fw::SketchPtr> m_sketches;
    QComboBox* m_sketch;
    QComboBox* m_axis;
    QDoubleSpinBox* m_angle;
    QComboBox* m_op;
};

// Druckeinstellungen fuer den Slicer
class PrintSettingsDialog : public QDialog {
    Q_OBJECT
public:
    PrintSettingsDialog(const fw::PrinterProfile& p, QWidget* parent = nullptr);
    fw::PrinterProfile profile() const;
private:
    fw::PrinterProfile m_base;
    QDoubleSpinBox *m_layer, *m_firstLayer, *m_infill, *m_speed, *m_retract;
    QSpinBox *m_perimeters, *m_top, *m_bottom, *m_nozzleTemp, *m_bedTemp;
};
