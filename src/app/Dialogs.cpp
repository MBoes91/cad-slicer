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
SketchEditDialog::SketchEditDialog(const fw::Sketch& sketch, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QString("Skizze bearbeiten: %1").arg(QString::fromStdString(sketch.name())));
    resize(620, 360);
    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({"Element", "X / Mitte X", "Y / Mitte Y", "Breite / Radius", "Höhe"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (const auto& r : sketch.rectangles()) addRow("Rechteck", r.x, r.y, r.width, r.height);
    for (const auto& c : sketch.circles()) addRow("Kreis", c.cx, c.cy, c.radius, 0);

    auto* addRect = new QPushButton("Rechteck hinzufügen");
    auto* addCircle = new QPushButton("Kreis hinzufügen");
    auto* remove = new QPushButton("Entfernen");
    connect(addRect, &QPushButton::clicked, this, [this] { addRow("Rechteck", 0, 0, 20, 10); });
    connect(addCircle, &QPushButton::clicked, this, [this] { addRow("Kreis", 0, 0, 5, 0); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (m_table->currentRow() >= 0) m_table->removeRow(m_table->currentRow());
    });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(addRect);
    buttons->addWidget(addCircle);
    buttons->addWidget(remove);
    buttons->addStretch();

    auto* lay = new QVBoxLayout(this);
    lay->addWidget(m_table);
    lay->addLayout(buttons);
    lay->addWidget(okCancel(this));
}

void SketchEditDialog::addRow(const QString& type, double a, double b, double c, double d)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto* item = new QTableWidgetItem(type);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, 0, item);
    const bool circle = type == "Kreis";
    const double vals[4] = {a, b, c, d};
    for (int col = 1; col <= 4; ++col) {
        auto* box = mmBox(-1000, 1000, vals[col - 1]);
        if (col >= 3) box->setMinimum(0.01);
        if (circle && col == 4) box->setEnabled(false);
        m_table->setCellWidget(row, col, box);
    }
}

void SketchEditDialog::applyTo(fw::Sketch& sketch) const
{
    sketch.rectangles().clear();
    sketch.circles().clear();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto v = [&](int col) { return static_cast<QDoubleSpinBox*>(m_table->cellWidget(row, col))->value(); };
        if (m_table->item(row, 0)->text() == "Kreis")
            sketch.circles().push_back({v(1), v(2), v(3)});
        else
            sketch.rectangles().push_back({v(1), v(2), v(3), v(4)});
    }
}

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
PrintSettingsDialog::PrintSettingsDialog(const fw::PrinterProfile& p, QWidget* parent)
    : QDialog(parent), m_base(p)
{
    setWindowTitle(QString("Druckeinstellungen: %1").arg(QString::fromStdString(p.name)));
    auto spin = [](int min, int max, int v, const QString& suffix = {}) {
        auto* s = new QSpinBox;
        s->setRange(min, max);
        s->setValue(v);
        s->setSuffix(suffix);
        return s;
    };
    m_layer = mmBox(0.05, 0.32, p.layerHeight);
    m_firstLayer = mmBox(0.1, 0.4, p.firstLayerHeight);
    m_perimeters = spin(1, 10, p.perimeters);
    m_top = spin(0, 20, p.topLayers);
    m_bottom = spin(0, 20, p.bottomLayers);
    m_infill = mmBox(0, 100, p.infillDensity * 100, " %", 0);
    m_speed = mmBox(5, 150, p.printSpeed, " mm/s", 0);
    m_retract = mmBox(0, 10, p.retractLength);
    m_nozzleTemp = spin(150, 260, p.nozzleTemp, QString::fromUtf8(" °C"));
    m_bedTemp = spin(0, 100, p.bedTemp, QString::fromUtf8(" °C"));

    auto* form = new QFormLayout;
    form->addRow("Schichthöhe:", m_layer);
    form->addRow("Erste Schicht:", m_firstLayer);
    form->addRow("Wandlinien:", m_perimeters);
    form->addRow("Deckschichten:", m_top);
    form->addRow("Bodenschichten:", m_bottom);
    form->addRow("Fülldichte:", m_infill);
    form->addRow("Druckgeschwindigkeit:", m_speed);
    form->addRow("Rückzug:", m_retract);
    form->addRow("Duesentemperatur:", m_nozzleTemp);
    form->addRow("Betttemperatur:", m_bedTemp);
    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(okCancel(this));
}

fw::PrinterProfile PrintSettingsDialog::profile() const
{
    fw::PrinterProfile p = m_base;
    p.layerHeight = m_layer->value();
    p.firstLayerHeight = m_firstLayer->value();
    p.perimeters = m_perimeters->value();
    p.topLayers = m_top->value();
    p.bottomLayers = m_bottom->value();
    p.infillDensity = m_infill->value() / 100.0;
    p.printSpeed = m_speed->value();
    p.retractLength = m_retract->value();
    p.nozzleTemp = m_nozzleTemp->value();
    p.bedTemp = m_bedTemp->value();
    return p;
}
