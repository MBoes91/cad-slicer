#include "app/Dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

QDoubleSpinBox* mmBox(double min, double max, double value, const QString& suffix = " mm", int decimals = 2)
{
    auto* b = new QDoubleSpinBox;
    b->setRange(min, max);
    b->setDecimals(decimals);
    b->setValue(value);
    b->setSuffix(suffix);
    b->setKeyboardTracking(false);
    return b;
}

QDialogButtonBox* okCancel(QDialog* d)
{
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("OK");
    bb->button(QDialogButtonBox::Cancel)->setText("Abbrechen");
    QObject::connect(bb, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return bb;
}

QComboBox* sketchCombo(const std::vector<fw::SketchPtr>& sketches)
{
    auto* c = new QComboBox;
    for (const auto& s : sketches) c->addItem(QString::fromStdString(s->name()));
    if (!sketches.empty()) c->setCurrentIndex(static_cast<int>(sketches.size()) - 1);
    return c;
}

QComboBox* opCombo()
{
    auto* c = new QComboBox;
    c->addItem("Verbinden");
    c->addItem("Differenz");
    c->addItem("Schnittmenge");
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
NewSketchDialog::NewSketchDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Neue 2D-Skizze");
    m_plane = new QComboBox;
    m_plane->addItems({"XY-Ebene (oben)", "XZ-Ebene (vorne)", "YZ-Ebene (rechts)"});
    m_offset = mmBox(-1000, 1000, 0);
    auto* form = new QFormLayout;
    form->addRow("Ebene:", m_plane);
    form->addRow("Abstand:", m_offset);
    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(okCancel(this));
}

fw::SketchPlane NewSketchDialog::plane() const
{
    return static_cast<fw::SketchPlane>(m_plane->currentIndex());
}

double NewSketchDialog::offset() const { return m_offset->value(); }

// ---------------------------------------------------------------------------
ExtrudeDialog::ExtrudeDialog(const std::vector<fw::SketchPtr>& sketches, QWidget* parent)
    : QDialog(parent), m_sketches(sketches)
{
    setWindowTitle("Extrusion");
    m_sketch = sketchCombo(sketches);
    m_distance = mmBox(-1000, 1000, 10);
    m_op = opCombo();
    m_symmetric = new QCheckBox("Symmetrisch zur Skizzenebene");
    auto* form = new QFormLayout;
    form->addRow("Profil (Skizze):", m_sketch);
    form->addRow("Abstand:", m_distance);
    form->addRow("Operation:", m_op);
    form->addRow("", m_symmetric);
    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(okCancel(this));
}

void ExtrudeDialog::setValues(const fw::ExtrudeFeature& f)
{
    for (size_t i = 0; i < m_sketches.size(); ++i)
        if (m_sketches[i] == f.sketch()) m_sketch->setCurrentIndex(static_cast<int>(i));
    m_distance->setValue(f.distance());
    m_op->setCurrentIndex(static_cast<int>(f.operation()));
    m_symmetric->setChecked(f.symmetric());
}

fw::SketchPtr ExtrudeDialog::sketch() const
{
    const int i = m_sketch->currentIndex();
    return i >= 0 ? m_sketches[static_cast<size_t>(i)] : nullptr;
}
double ExtrudeDialog::distance() const { return m_distance->value(); }
fw::BooleanOp ExtrudeDialog::operation() const { return static_cast<fw::BooleanOp>(m_op->currentIndex()); }
bool ExtrudeDialog::symmetric() const { return m_symmetric->isChecked(); }

// ---------------------------------------------------------------------------
RevolveDialog::RevolveDialog(const std::vector<fw::SketchPtr>& sketches, QWidget* parent)
    : QDialog(parent), m_sketches(sketches)
{
    setWindowTitle("Drehung");
    m_sketch = sketchCombo(sketches);
    m_axis = new QComboBox;
    m_axis->addItems({"Skizzen-X-Achse", "Skizzen-Y-Achse"});
    m_axis->setCurrentIndex(1);
    m_angle = mmBox(0.1, 360, 360, QString::fromUtf8(" °"), 1);
    m_op = opCombo();
    auto* form = new QFormLayout;
    form->addRow("Profil (Skizze):", m_sketch);
    form->addRow("Achse:", m_axis);
    form->addRow("Winkel:", m_angle);
    form->addRow("Operation:", m_op);
    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(okCancel(this));
}

void RevolveDialog::setValues(const fw::RevolveFeature& f)
{
    for (size_t i = 0; i < m_sketches.size(); ++i)
        if (m_sketches[i] == f.sketch()) m_sketch->setCurrentIndex(static_cast<int>(i));
    m_axis->setCurrentIndex(static_cast<int>(f.axis()));
    m_angle->setValue(f.angle());
    m_op->setCurrentIndex(static_cast<int>(f.operation()));
}

fw::SketchPtr RevolveDialog::sketch() const
{
    const int i = m_sketch->currentIndex();
    return i >= 0 ? m_sketches[static_cast<size_t>(i)] : nullptr;
}
fw::RevolveAxis RevolveDialog::axis() const { return static_cast<fw::RevolveAxis>(m_axis->currentIndex()); }
double RevolveDialog::angle() const { return m_angle->value(); }
fw::BooleanOp RevolveDialog::operation() const { return static_cast<fw::BooleanOp>(m_op->currentIndex()); }

// ---------------------------------------------------------------------------
namespace profiles {

QString directory()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/profiles");
    dir.mkpath(".");
    return dir.absolutePath();
}

static QString fileFor(const QString& name)
{
    QString safe = name;
    safe.replace(QRegularExpression("[^A-Za-z0-9 _,.()\\-äöüÄÖÜß]"), "_");
    return directory() + "/" + safe + ".fwprofile";
}

bool isBuiltin(const QString& name)
{
    for (const auto& p : fw::builtinProfiles())
        if (QString::fromStdString(p.name) == name) return true;
    return false;
}

QStringList userProfileNames()
{
    QStringList names;
    for (const QFileInfo& fi : QDir(directory()).entryInfoList({"*.fwprofile"}, QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        fw::PrinterProfile p;
        if (fw::deserializeProfile(f.readAll().toStdString(), p)) names << QString::fromStdString(p.name);
    }
    return names;
}

bool load(const QString& name, fw::PrinterProfile& out)
{
    for (const auto& p : fw::builtinProfiles())
        if (QString::fromStdString(p.name) == name) { out = p; return true; }
    QFile f(fileFor(name));
    if (!f.open(QIODevice::ReadOnly)) return false;
    fw::PrinterProfile p;
    if (!fw::deserializeProfile(f.readAll().toStdString(), p)) return false;
    out = p;
    return true;
}

bool save(const fw::PrinterProfile& p)
{
    QFile f(fileFor(QString::fromStdString(p.name)));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QByteArray::fromStdString(fw::serializeProfile(p)));
    return true;
}

bool remove(const QString& name) { return QFile::remove(fileFor(name)); }

} // namespace profiles

// ---------------------------------------------------------------------------
namespace {

const char* kEventGroup = "Höhen-Ereignisse";

QComboBox* eventTypeCombo()
{
    auto* c = new QComboBox;
    for (int t = 0; t <= static_cast<int>(fw::HeightEventType::CustomGcode); ++t)
        c->addItem(QString::fromUtf8(fw::heightEventName(static_cast<fw::HeightEventType>(t))));
    return c;
}

QString eventValueHint(fw::HeightEventType t)
{
    switch (t) {
    case fw::HeightEventType::NozzleTemp:
    case fw::HeightEventType::BedTemp: return QString::fromUtf8("Wert = Temperatur in °C");
    case fw::HeightEventType::FanPercent:
    case fw::HeightEventType::SpeedPercent:
    case fw::HeightEventType::FlowPercent: return "Wert = Prozent";
    case fw::HeightEventType::Pause: return "Text = Meldung am Display (optional)";
    case fw::HeightEventType::CustomGcode: return "Text = G-Code-Zeilen";
    case fw::HeightEventType::FilamentChange: return "Bedienung am Display des Druckers";
    }
    return {};
}

void addEventRow(QTableWidget* t, const fw::HeightEvent& e)
{
    const int row = t->rowCount();
    t->insertRow(row);
    auto* z = mmBox(0, 1000, e.z);
    auto* type = eventTypeCombo();
    type->setCurrentIndex(static_cast<int>(e.type));
    auto* value = mmBox(0, 300, e.value, "", 0);
    t->setCellWidget(row, 0, z);
    t->setCellWidget(row, 1, type);
    t->setCellWidget(row, 2, value);
    auto* text = new QTableWidgetItem(QString::fromStdString(e.gcode).replace("\n", " | "));
    text->setToolTip("Mehrere G-Code-Zeilen mit | trennen");
    t->setItem(row, 3, text);
}

} // namespace

PrintSettingsDialog::PrintSettingsDialog(const fw::PrinterProfile& p, QWidget* parent)
    : QDialog(parent), m_profile(p)
{
    setWindowTitle("Druckeinstellungen");
    resize(820, 620);

    // Profilzeile
    m_profileCombo = new QComboBox;
    m_profileCombo->setMinimumWidth(320);
    auto* bSave = new QPushButton("Speichern");
    auto* bSaveAs = new QPushButton("Speichern unter …");
    auto* bDelete = new QPushButton("Löschen");
    auto* bImport = new QPushButton("Importieren …");
    auto* bExport = new QPushButton("Exportieren …");
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel("Profil:"));
    top->addWidget(m_profileCombo, 1);
    top->addWidget(bSave);
    top->addWidget(bSaveAs);
    top->addWidget(bDelete);
    auto* top2 = new QHBoxLayout;
    m_expert = new QCheckBox("Experteneinstellungen anzeigen");
    top2->addWidget(m_expert);
    top2->addStretch();
    top2->addWidget(bImport);
    top2->addWidget(bExport);

    m_groups = new QListWidget;
    m_groups->setFixedWidth(190);
    m_pages = new QStackedWidget;
    buildPages();
    connect(m_groups, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_groups->setCurrentRow(0);

    auto* mid = new QHBoxLayout;
    mid->addWidget(m_groups);
    mid->addWidget(m_pages, 1);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(top);
    lay->addLayout(top2);
    lay->addLayout(mid, 1);
    lay->addWidget(okCancel(this));

    loadValues(m_profile);
    reloadProfileList(QString::fromStdString(m_profile.name));
    updateExpertVisibility();

    connect(m_expert, &QCheckBox::toggled, this, [this] { updateExpertVisibility(); });
    connect(m_profileCombo, &QComboBox::activated, this, [this](int) {
        fw::PrinterProfile p;
        if (profiles::load(m_profileCombo->currentText(), p)) {
            m_profile = p;
            loadValues(p);
        }
    });
    connect(bSave, &QPushButton::clicked, this, [this] { saveProfile(false); });
    connect(bSaveAs, &QPushButton::clicked, this, [this] { saveProfile(true); });
    connect(bDelete, &QPushButton::clicked, this, [this] {
        const QString name = m_profileCombo->currentText();
        if (profiles::isBuiltin(name)) {
            QMessageBox::information(this, "Profil löschen", "Mitgelieferte Profile können nicht gelöscht werden.");
            return;
        }
        if (QMessageBox::question(this, "Profil löschen", QString("Profil \"%1\" löschen?").arg(name)) != QMessageBox::Yes)
            return;
        profiles::remove(name);
        reloadProfileList(QString::fromStdString(fw::builtinProfiles().front().name));
    });
    connect(bImport, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Profil importieren", {}, "FormWerk-Profil (*.fwprofile)");
        if (path.isEmpty()) return;
        QFile f(path);
        fw::PrinterProfile p;
        std::string err;
        if (!f.open(QIODevice::ReadOnly) || !fw::deserializeProfile(f.readAll().toStdString(), p, &err)) {
            QMessageBox::warning(this, "Profil importieren", QString::fromStdString(err.empty() ? "Datei nicht lesbar" : err));
            return;
        }
        m_profile = p;
        loadValues(p);
        profiles::save(p);
        reloadProfileList(QString::fromStdString(p.name));
    });
    connect(bExport, &QPushButton::clicked, this, [this] {
        fw::PrinterProfile p = m_profile;
        storeValues(p);
        const QString path = QFileDialog::getSaveFileName(this, "Profil exportieren",
            QString::fromStdString(p.name) + ".fwprofile", "FormWerk-Profil (*.fwprofile)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QByteArray::fromStdString(fw::serializeProfile(p)));
    });
}

void PrintSettingsDialog::buildPages()
{
    for (const auto& group : fw::settingGroups()) {
        const QString title = QString::fromUtf8(group.c_str());
        m_groups->addItem(title);
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        form->setLabelAlignment(Qt::AlignRight);
        auto* heading = new QLabel(QString("<b>%1</b>").arg(title));
        form->addRow(heading);
        for (const auto& def : fw::settingDefs()) {
            if (group != def.group) continue;
            QWidget* editor = nullptr;
            const QString unit = QString::fromUtf8(def.unit);
            switch (def.kind) {
            case fw::SettingKind::Double: {
                const int decimals = def.displayScale > 1 ? 0 : (def.max <= 10 ? 2 : 1);
                editor = mmBox(def.min * def.displayScale, def.max * def.displayScale, 0,
                               unit.isEmpty() ? QString() : " " + unit, decimals);
                break;
            }
            case fw::SettingKind::Int: {
                auto* s = new QSpinBox;
                s->setRange(static_cast<int>(def.min), static_cast<int>(def.max));
                if (!unit.isEmpty()) s->setSuffix(" " + unit);
                editor = s;
                break;
            }
            case fw::SettingKind::Bool: editor = new QCheckBox; break;
            case fw::SettingKind::Choice: {
                auto* c = new QComboBox;
                for (const char* ch : def.choices) c->addItem(QString::fromUtf8(ch));
                editor = c;
                break;
            }
            case fw::SettingKind::Text: {
                auto* t = new QPlainTextEdit;
                t->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
                t->setMinimumHeight(150);
                editor = t;
                break;
            }
            }
            editor->setToolTip(QString::fromUtf8(def.tooltip));
            auto* label = new QLabel(QString::fromUtf8(def.label) + ":");
            label->setToolTip(QString::fromUtf8(def.tooltip));
            form->addRow(label, editor);
            m_rows.push_back({&def, editor, form});
        }
        m_pages->addWidget(page);
    }

    // Hoehen-Ereignisse
    m_groups->addItem(QString::fromUtf8(kEventGroup));
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    auto* info = new QLabel(QString::fromUtf8(
        "<b>Höhen-Ereignisse</b><br>Werden vor der ersten Schicht ausgeführt, die ganz über der angegebenen Höhe liegt. "
        "Beispiele: Pause zum Einlegen von Muttern, Filamentwechsel für Farbwechsel, Temperaturturm."));
    info->setWordWrap(true);
    v->addWidget(info);
    m_events = new QTableWidget(0, 4);
    m_events->setHorizontalHeaderLabels({"Höhe Z", "Ereignis", "Wert", "Text / G-Code"});
    m_events->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_events->setColumnWidth(0, 100);
    m_events->setColumnWidth(1, 190);
    v->addWidget(m_events, 1);
    auto* hint = new QLabel;
    hint->setStyleSheet("color: #5a6270;");
    v->addWidget(hint);
    auto* add = new QPushButton("Ereignis hinzufügen");
    auto* del = new QPushButton("Entfernen");
    auto* hb = new QHBoxLayout;
    hb->addWidget(add);
    hb->addWidget(del);
    hb->addStretch();
    v->addLayout(hb);
    connect(add, &QPushButton::clicked, this, [this] { addEventRow(m_events, fw::HeightEvent{}); });
    connect(del, &QPushButton::clicked, this, [this] {
        if (m_events->currentRow() >= 0) m_events->removeRow(m_events->currentRow());
    });
    connect(m_events, &QTableWidget::currentCellChanged, this, [this, hint](int row) {
        if (row < 0) { hint->clear(); return; }
        auto* type = static_cast<QComboBox*>(m_events->cellWidget(row, 1));
        hint->setText(eventValueHint(static_cast<fw::HeightEventType>(type->currentIndex())));
    });
    m_pages->addWidget(page);
}

void PrintSettingsDialog::loadValues(const fw::PrinterProfile& p)
{
    for (const auto& r : m_rows) {
        const auto& d = *r.def;
        switch (d.kind) {
        case fw::SettingKind::Double: static_cast<QDoubleSpinBox*>(r.editor)->setValue(p.*(d.d) * d.displayScale); break;
        case fw::SettingKind::Int: static_cast<QSpinBox*>(r.editor)->setValue(p.*(d.i)); break;
        case fw::SettingKind::Bool: static_cast<QCheckBox*>(r.editor)->setChecked(p.*(d.b)); break;
        case fw::SettingKind::Choice: static_cast<QComboBox*>(r.editor)->setCurrentIndex(p.*(d.i)); break;
        case fw::SettingKind::Text: static_cast<QPlainTextEdit*>(r.editor)->setPlainText(QString::fromStdString(p.*(d.s))); break;
        }
    }
    m_events->setRowCount(0);
    for (const auto& e : p.events) addEventRow(m_events, e);
}

void PrintSettingsDialog::storeValues(fw::PrinterProfile& p) const
{
    for (const auto& r : m_rows) {
        const auto& d = *r.def;
        switch (d.kind) {
        case fw::SettingKind::Double: p.*(d.d) = static_cast<QDoubleSpinBox*>(r.editor)->value() / d.displayScale; break;
        case fw::SettingKind::Int: p.*(d.i) = static_cast<QSpinBox*>(r.editor)->value(); break;
        case fw::SettingKind::Bool: p.*(d.b) = static_cast<QCheckBox*>(r.editor)->isChecked(); break;
        case fw::SettingKind::Choice: p.*(d.i) = static_cast<QComboBox*>(r.editor)->currentIndex(); break;
        case fw::SettingKind::Text: p.*(d.s) = static_cast<QPlainTextEdit*>(r.editor)->toPlainText().toStdString(); break;
        }
    }
    p.events.clear();
    for (int row = 0; row < m_events->rowCount(); ++row) {
        fw::HeightEvent e;
        e.z = static_cast<QDoubleSpinBox*>(m_events->cellWidget(row, 0))->value();
        e.type = static_cast<fw::HeightEventType>(static_cast<QComboBox*>(m_events->cellWidget(row, 1))->currentIndex());
        e.value = static_cast<QDoubleSpinBox*>(m_events->cellWidget(row, 2))->value();
        QString text = m_events->item(row, 3) ? m_events->item(row, 3)->text() : QString();
        e.gcode = text.replace(" | ", "\n").replace("|", "\n").toStdString();
        p.events.push_back(e);
    }
}

void PrintSettingsDialog::updateExpertVisibility()
{
    const bool expert = m_expert->isChecked();
    for (const auto& r : m_rows)
        r.form->setRowVisible(r.editor, expert || !r.def->expert);
    // Gruppen ohne sichtbare Einstellungen ausblenden
    const auto groups = fw::settingGroups();
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
        bool any = false;
        for (const auto& r : m_rows)
            any = any || (groups[static_cast<size_t>(i)] == r.def->group && (expert || !r.def->expert));
        m_groups->item(i)->setHidden(!any);
    }
}

void PrintSettingsDialog::reloadProfileList(const QString& select)
{
    m_profileCombo->clear();
    for (const auto& p : fw::builtinProfiles()) m_profileCombo->addItem(QString::fromStdString(p.name));
    m_profileCombo->insertSeparator(m_profileCombo->count());
    for (const auto& n : profiles::userProfileNames()) m_profileCombo->addItem(n);
    int idx = m_profileCombo->findText(select);
    if (idx < 0) {
        m_profileCombo->addItem(select + " (nicht gespeichert)");
        idx = m_profileCombo->count() - 1;
    }
    m_profileCombo->setCurrentIndex(idx);
}

void PrintSettingsDialog::saveProfile(bool askName)
{
    fw::PrinterProfile p = m_profile;
    storeValues(p);
    QString name = QString::fromStdString(p.name);
    if (askName || profiles::isBuiltin(name)) {
        bool ok = false;
        const QString suggestion = profiles::isBuiltin(name) ? name + " (eigenes)" : name;
        name = QInputDialog::getText(this, "Profil speichern", "Name des Profils:", QLineEdit::Normal, suggestion, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (profiles::isBuiltin(name)) {
            QMessageBox::information(this, "Profil speichern", "Bitte einen anderen Namen wählen als die mitgelieferten Profile.");
            return;
        }
    }
    p.name = name.toStdString();
    if (!profiles::save(p)) {
        QMessageBox::warning(this, "Profil speichern", "Das Profil konnte nicht gespeichert werden.");
        return;
    }
    m_profile = p;
    reloadProfileList(name);
}

fw::PrinterProfile PrintSettingsDialog::profile() const
{
    fw::PrinterProfile p = m_profile;
    storeValues(p);
    return p;
}

// ---------------------------------------------------------------------------
HeightEventDialog::HeightEventDialog(double z, QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Höhen-Ereignis einfügen");
    m_z = mmBox(0, 1000, z);
    m_type = eventTypeCombo();
    m_value = mmBox(0, 300, 0, "", 0);
    m_text = new QPlainTextEdit;
    m_text->setMaximumHeight(90);
    auto* hint = new QLabel;
    hint->setStyleSheet("color: #5a6270;");
    auto updateHint = [this, hint] {
        const auto t = static_cast<fw::HeightEventType>(m_type->currentIndex());
        hint->setText(eventValueHint(t));
        m_value->setEnabled(t != fw::HeightEventType::Pause && t != fw::HeightEventType::FilamentChange
                            && t != fw::HeightEventType::CustomGcode);
        m_text->setEnabled(t == fw::HeightEventType::Pause || t == fw::HeightEventType::CustomGcode);
    };
    connect(m_type, &QComboBox::currentIndexChanged, this, updateHint);
    updateHint();

    auto* form = new QFormLayout;
    form->addRow("Ab Höhe Z:", m_z);
    form->addRow("Ereignis:", m_type);
    form->addRow("Wert:", m_value);
    form->addRow("Text / G-Code:", m_text);
    form->addRow("", hint);
    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(okCancel(this));
}

fw::HeightEvent HeightEventDialog::event() const
{
    fw::HeightEvent e;
    e.z = m_z->value();
    e.type = static_cast<fw::HeightEventType>(m_type->currentIndex());
    e.value = m_value->value();
    e.gcode = m_text->toPlainText().toStdString();
    return e;
}

// ---------------------------------------------------------------------------
ModifierDialog::ModifierDialog(const fw::PrintModifier& m, QWidget* parent) : QDialog(parent), m_base(m)
{
    setWindowTitle(QString::fromUtf8("Druck-Modifikator: %1").arg(QString::fromStdString(m.name)));
    m_role = new QComboBox;
    for (auto r : {fw::PrintModifier::Role::InfillZone, fw::PrintModifier::Role::SupportEnforcer, fw::PrintModifier::Role::SupportBlocker})
        m_role->addItem(QString::fromUtf8(fw::modifierRoleName(r)));
    m_role->setCurrentIndex(static_cast<int>(m.role));
    m_shape = new QComboBox;
    m_shape->addItems({"Quader", "Zylinder"});
    m_shape->setCurrentIndex(static_cast<int>(m.shape));
    m_cx = mmBox(-1000, 1000, m.cx);
    m_cy = mmBox(-1000, 1000, m.cy);
    m_cz = mmBox(-1000, 1000, m.cz);
    m_sx = mmBox(0.1, 1000, m.sx);
    m_sy = mmBox(0.1, 1000, m.sy);
    m_sz = mmBox(0.1, 1000, m.sz);
    m_density = mmBox(0, 100, m.infillDensity * 100, " %", 0);
    m_pattern = new QComboBox;
    m_pattern->addItems({"Linien", "Gitter"});
    m_pattern->setCurrentIndex(m.infillPattern);

    auto* form = new QFormLayout;
    form->addRow("Art:", m_role);
    form->addRow("Form:", m_shape);
    auto* pos = new QHBoxLayout;
    pos->addWidget(m_cx); pos->addWidget(m_cy); pos->addWidget(m_cz);
    form->addRow("Mitte X / Y / Z:", pos);
    auto* size = new QHBoxLayout;
    size->addWidget(m_sx); size->addWidget(m_sy); size->addWidget(m_sz);
    auto* sizeLabel = new QLabel;
    form->addRow(sizeLabel, size);
    form->addRow("Fülldichte:", m_density);
    form->addRow("Füllmuster:", m_pattern);
    auto* hint = new QLabel;
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #5a6270;");
    form->addRow(hint);

    auto update = [=] {
        const bool zone = m_role->currentIndex() == static_cast<int>(fw::PrintModifier::Role::InfillZone);
        const bool cyl = m_shape->currentIndex() == 1;
        form->setRowVisible(m_density, zone);
        form->setRowVisible(m_pattern, zone);
        m_sy->setVisible(!cyl);
        sizeLabel->setText(cyl ? QString::fromUtf8("Durchmesser / Höhe:") : QString::fromUtf8("Größe X / Y / Z:"));
        switch (static_cast<fw::PrintModifier::Role>(m_role->currentIndex())) {
        case fw::PrintModifier::Role::InfillZone:
            hint->setText(QString::fromUtf8("Innerhalb des Bereichs wird mit eigener Dichte gefüllt, z. B. 80 % rund um Schraubenlöcher."));
            break;
        case fw::PrintModifier::Role::SupportEnforcer:
            hint->setText(QString::fromUtf8("Unter allen nach unten zeigenden Flächen im Bereich entstehen Stützen, auch wenn Stützen sonst aus sind."));
            break;
        case fw::PrintModifier::Role::SupportBlocker:
            hint->setText(QString::fromUtf8("Im Bereich werden keine Stützen erzeugt."));
            break;
        }
    };
    connect(m_role, &QComboBox::currentIndexChanged, this, update);
    connect(m_shape, &QComboBox::currentIndexChanged, this, update);
    update();

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(okCancel(this));
}

fw::PrintModifier ModifierDialog::modifier() const
{
    fw::PrintModifier m = m_base;
    m.role = static_cast<fw::PrintModifier::Role>(m_role->currentIndex());
    m.shape = static_cast<fw::PrintModifier::Shape>(m_shape->currentIndex());
    m.cx = m_cx->value(); m.cy = m_cy->value(); m.cz = m_cz->value();
    m.sx = m_sx->value(); m.sy = m.shape == fw::PrintModifier::Shape::Cylinder ? m_sx->value() : m_sy->value(); m.sz = m_sz->value();
    m.infillDensity = m_density->value() / 100.0;
    m.infillPattern = m_pattern->currentIndex();
    return m;
}
