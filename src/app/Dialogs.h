#pragma once

#include "core/PartDocument.h"
#include "slicer/PrinterProfile.h"
#include "slicer/Settings.h"
#include "core/PrintModifier.h"

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

// Parameter (fx): Modellparameter mit Gleichungen und Benutzerparameter
class ParameterDialog : public QDialog {
    Q_OBJECT
public:
    ParameterDialog(const std::vector<fw::ParameterInfo>& params, QWidget* parent = nullptr);
    // Gleichungen der Modellparameter (Name -> Gleichung oder Zahl)
    std::vector<std::pair<std::string, std::string>> modelExpressions() const;
    std::vector<fw::UserParameter> userParameters() const;

private:
    void addUserRow(const fw::UserParameter& u);
    bool validate();
    QTableWidget* m_table;
    int m_userHeader = 0;
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

// Druck-Modifikator anlegen/bearbeiten
class ModifierDialog : public QDialog {
    Q_OBJECT
public:
    ModifierDialog(const fw::PrintModifier& m, QWidget* parent = nullptr);
    fw::PrintModifier modifier() const;
private:
    fw::PrintModifier m_base;
    QComboBox* m_role;
    QComboBox* m_shape;
    QDoubleSpinBox *m_cx, *m_cy, *m_cz, *m_sx, *m_sy, *m_sz, *m_density;
    QComboBox* m_pattern;
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
