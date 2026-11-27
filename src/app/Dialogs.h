#pragma once

#include "core/Features.h"
#include "slicer/PrinterProfile.h"
#include "slicer/Settings.h"

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

// Druckeinstellungen: alle Einstellungen nach Gruppen, Profile verwalten,
// Hoehen-Ereignisse bearbeiten.
class PrintSettingsDialog : public QDialog {
    Q_OBJECT
public:
    PrintSettingsDialog(const fw::PrinterProfile& p, QWidget* parent = nullptr);
    fw::PrinterProfile profile() const;

private:
    void buildPages();
    void loadValues(const fw::PrinterProfile& p);
    void storeValues(fw::PrinterProfile& p) const;
    void updateExpertVisibility();
    void reloadProfileList(const QString& select);
    void saveProfile(bool askName);

    fw::PrinterProfile m_profile;
    QComboBox* m_profileCombo = nullptr;
    QCheckBox* m_expert = nullptr;
    class QListWidget* m_groups = nullptr;
    class QStackedWidget* m_pages = nullptr;
    QTableWidget* m_events = nullptr;
    struct Row {
        const fw::SettingDef* def;
        QWidget* editor;
        class QFormLayout* form;
    };
    std::vector<Row> m_rows;
};

// Ein Hoehen-Ereignis anlegen (z. B. aus der Schichtvorschau)
class HeightEventDialog : public QDialog {
    Q_OBJECT
public:
    HeightEventDialog(double z, QWidget* parent = nullptr);
    fw::HeightEvent event() const;
private:
    QDoubleSpinBox* m_z;
    QComboBox* m_type;
    QDoubleSpinBox* m_value;
    class QPlainTextEdit* m_text;
};

// Verwaltung der Profildateien
namespace profiles {
QString directory();
QStringList userProfileNames();
bool load(const QString& name, fw::PrinterProfile& out);
bool save(const fw::PrinterProfile& p);
bool remove(const QString& name);
bool isBuiltin(const QString& name);
}
