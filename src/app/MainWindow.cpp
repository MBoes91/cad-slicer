#include "app/MainWindow.h"

#include "app/Dialogs.h"
#include "app/OcctView.h"
#include "app/ToolpathPreview.h"
#include "slicer/Settings.h"

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_Drawer.hxx>

#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QCloseEvent>
#include <QFileInfo>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QtConcurrent/QtConcurrentRun>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Einfache, selbst gezeichnete Symbole (keine fremden Grafiken).
enum class Icon { Pause, Sketch, Extrude, Revolve, Hole, Export, Home, Fit, Top, Front, Right, Slice, Save, Settings, Preview, Rect, Circle };

QIcon makeIcon(Icon kind)
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor blue(40, 110, 200), dark(50, 60, 75), orange(235, 140, 30), green(40, 160, 90);
    p.setPen(QPen(dark, 3));
    switch (kind) {
    case Icon::Sketch:
        p.setPen(QPen(blue, 3, Qt::DashLine));
        p.drawRect(10, 18, 44, 30);
        p.setPen(QPen(orange, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(38, 44, 56, 10);
        break;
    case Icon::Extrude: {
        p.setBrush(QColor(150, 185, 230));
        QPolygon front({{10, 30}, {40, 30}, {40, 56}, {10, 56}});
        QPolygon top({{10, 30}, {24, 16}, {54, 16}, {40, 30}});
        QPolygon side({{40, 30}, {54, 16}, {54, 42}, {40, 56}});
        p.drawPolygon(front); p.drawPolygon(top); p.drawPolygon(side);
        p.setPen(QPen(orange, 4)); p.drawLine(32, 12, 32, 2);
        break;
    }
    case Icon::Revolve:
        p.setBrush(QColor(150, 185, 230));
        p.drawEllipse(12, 34, 40, 16);
        p.drawRect(12, 18, 40, 24);
        p.drawEllipse(12, 10, 40, 16);
        p.setPen(QPen(orange, 3, Qt::DashLine)); p.drawLine(32, 2, 32, 62);
        break;
    case Icon::Hole:
        p.setBrush(QColor(150, 185, 230)); p.drawRect(8, 14, 48, 40);
        p.setBrush(Qt::white); p.drawEllipse(22, 24, 20, 20);
        break;
    case Icon::Export:
        p.setBrush(QColor(150, 185, 230));
        p.drawPolygon(QPolygon({{8, 50}, {32, 10}, {56, 50}}));
        p.setPen(QPen(green, 5, Qt::SolidLine, Qt::RoundCap)); p.drawLine(46, 30, 46, 58); p.drawLine(38, 50, 46, 58); p.drawLine(54, 50, 46, 58);
        break;
    case Icon::Home:
        p.setBrush(QColor(150, 185, 230));
        p.drawPolygon(QPolygon({{8, 32}, {32, 10}, {56, 32}})); p.drawRect(16, 32, 32, 24);
        break;
    case Icon::Fit:
        p.drawRect(8, 8, 48, 48); p.setBrush(QColor(150, 185, 230)); p.drawRect(20, 20, 24, 24);
        break;
    case Icon::Top: case Icon::Front: case Icon::Right:
        p.setBrush(QColor(220, 225, 232)); p.drawRect(10, 10, 44, 44);
        p.setPen(dark); p.setFont(QFont("Sans", 22, QFont::Bold));
        p.drawText(QRect(10, 10, 44, 44), Qt::AlignCenter, kind == Icon::Top ? "O" : kind == Icon::Front ? "V" : "R");
        break;
    case Icon::Slice:
        p.setBrush(QColor(150, 185, 230));
        for (int i = 0; i < 5; ++i) p.drawRect(12, 12 + i * 9, 40, 6);
        p.setPen(QPen(orange, 4)); p.drawLine(4, 58, 60, 4);
        break;
    case Icon::Save:
        p.setBrush(QColor(150, 185, 230)); p.drawRect(10, 8, 44, 48);
        p.setBrush(Qt::white); p.drawRect(18, 8, 28, 16); p.drawRect(18, 34, 28, 22);
        p.setFont(QFont("Sans", 9, QFont::Bold)); p.drawText(QRect(18, 34, 28, 22), Qt::AlignCenter, "G");
        break;
    case Icon::Settings:
        p.setBrush(QColor(150, 185, 230));
        for (int i = 0; i < 8; ++i) { p.save(); p.translate(32, 32); p.rotate(i * 45); p.drawRect(-5, -28, 10, 12); p.restore(); }
        p.drawEllipse(12, 12, 40, 40); p.setBrush(Qt::white); p.drawEllipse(24, 24, 16, 16);
        break;
    case Icon::Preview:
        for (int i = 0; i < 6; ++i) { p.setPen(QPen(i % 2 ? green : QColor(220, 60, 50), 4)); p.drawLine(10, 14 + i * 8, 54, 14 + i * 8); }
        break;
    case Icon::Pause:
        p.setBrush(QColor(150, 185, 230));
        for (int i = 0; i < 4; ++i) p.drawRect(8, 34 + i * 7, 48, 5);
        p.setPen(Qt::NoPen); p.setBrush(orange);
        p.drawRect(20, 6, 9, 22); p.drawRect(35, 6, 9, 22);
        break;
    case Icon::Rect:
        p.setPen(QPen(blue, 4)); p.drawRect(10, 16, 44, 32);
        break;
    case Icon::Circle:
        p.setPen(QPen(blue, 4)); p.drawEllipse(12, 12, 40, 40);
        break;
    }
    return QIcon(pm);
}

QWidget* makePanel(const QString& title, const QList<QAction*>& actions)
{
    auto* panel = new QWidget;
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(4, 2, 4, 0);
    v->setSpacing(0);
    auto* row = new QHBoxLayout;
    row->setSpacing(2);
    for (QAction* a : actions) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(QSize(32, 32));
        b->setAutoRaise(true);
        b->setMinimumWidth(64);
        row->addWidget(b);
    }
    v->addLayout(row);
    auto* label = new QLabel(title);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("color: #5a6270; font-size: 11px; border-top: 1px solid #d0d5dc; padding-top: 1px;");
    v->addWidget(label);
    return panel;
}

QWidget* makeRibbonPage(const QList<QWidget*>& panels)
{
    auto* page = new QWidget;
    auto* h = new QHBoxLayout(page);
    h->setContentsMargins(2, 2, 2, 2);
    h->setSpacing(6);
    for (int i = 0; i < panels.size(); ++i) {
        if (i > 0) {
            auto* sep = new QFrame;
            sep->setFrameShape(QFrame::VLine);
            sep->setStyleSheet("color: #d0d5dc;");
            h->addWidget(sep);
        }
        h->addWidget(panels[i]);
    }
    h->addStretch();
    return page;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    resize(1400, 900);

    m_view = new OcctView(this);
    setCentralWidget(m_view);

    m_sliceWatcher = new QFutureWatcher<fw::SliceResult>(this);
    connect(m_sliceWatcher, &QFutureWatcher<fw::SliceResult>::finished, this, [this] { sliceFinished(); });
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setMaximumWidth(220);
    m_progressBar->setFormat("Slicen … %p %");
    m_progressBar->hide();
    m_cancelButton = new QPushButton("Abbrechen");
    m_cancelButton->hide();
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { m_cancelSlice = true; });
    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_cancelButton);
    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, [this] {
        m_progressBar->setValue(static_cast<int>(m_sliceProgress.load() * 100));
    });

    buildRibbon();
    buildBrowser();
    buildFileMenu();

    statusBar()->showMessage(QString::fromUtf8(
        "Mausrad: Zoom  ·  Mittlere Maustaste: Verschieben  ·  Umschalt+Mitte oder Rechts: Drehen  ·  F6: Ausgangsansicht"));

    // Zuletzt verwendetes Druckprofil wiederherstellen
    QSettings settings;
    const QByteArray stored = settings.value("druckprofil").toByteArray();
    if (!stored.isEmpty()) {
        fw::PrinterProfile p;
        if (fw::deserializeProfile(stored.toStdString(), p)) m_profile = p;
    }
    m_profileLabel->setText(QString::fromStdString(m_profile.name));

    loadDemoPart();
}

void MainWindow::buildRibbon()
{
    auto act = [this](const QString& text, Icon icon, const QKeySequence& key, auto slot) {
        auto* a = new QAction(makeIcon(icon), text, this);
        if (!key.isEmpty()) {
            a->setShortcut(key);
            a->setToolTip(QString("%1 (%2)").arg(text, key.toString()));
        }
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        return a;
    };

    // --- 3D-Modell
    auto* aSketch = act("2D-Skizze\nerstellen", Icon::Sketch, QKeySequence("S"), [this] { cmdNewSketch(); });
    auto* aExtrude = act("Extrusion", Icon::Extrude, QKeySequence("E"), [this] { cmdExtrude(); });
    auto* aRevolve = act("Drehung", Icon::Revolve, QKeySequence("R"), [this] { cmdRevolve(); });
    auto* aHole = act("Bohrung", Icon::Hole, QKeySequence("H"), [this] {
        QMessageBox::information(this, "Bohrung",
            QString::fromUtf8("Das Bohrungs-Feature folgt im nächsten Schritt.\n"
                              "Bis dahin: Kreis in eine Skizze zeichnen und mit \"Differenz\" extrudieren."));
    });
    auto* aStl = act("STL\nexportieren", Icon::Export, {}, [this] { cmdExportStl(); });

    // --- Ansicht
    auto* aHome = act("Ausgangs-\nansicht", Icon::Home, QKeySequence(Qt::Key_F6), [this] { m_view->homeView(); });
    auto* aFit = act("Alles\nzeigen", Icon::Fit, QKeySequence(Qt::Key_Home), [this] { m_view->fitAll(); });
    auto* aTop = act("Oben", Icon::Top, {}, [this] { m_view->setStandardView(V3d_Zpos); });
    auto* aFront = act("Vorne", Icon::Front, {}, [this] { m_view->setStandardView(V3d_Yneg); });
    auto* aRight = act("Rechts", Icon::Right, {}, [this] { m_view->setStandardView(V3d_Xpos); });

    // --- Drucken
    auto* aSlice = act("Slicen", Icon::Slice, QKeySequence("Ctrl+R"), [this] { cmdSlice(); });
    m_previewAction = act("Druck-\nvorschau", Icon::Preview, {}, [this] { setPrintPreview(!m_printPreview); });
    m_previewAction->setCheckable(true);
    auto* aSave = act("G-Code\nspeichern", Icon::Save, QKeySequence("Ctrl+G"), [this] { cmdSaveGcode(); });
    auto* aSettings = act("Druckein-\nstellungen", Icon::Settings, {}, [this] { cmdPrintSettings(); });

    auto* aEvent = act("Ereignis", Icon::Pause, {}, [this] { cmdAddEventAtLayer(); });
    aEvent->setToolTip(QString::fromUtf8("Pause, Filamentwechsel, Temperatur o. Ä. ab der angezeigten Schicht einfügen"));

    m_layerSlider = new QSlider(Qt::Horizontal);
    m_layerSlider->setMinimumWidth(220);
    m_layerSlider->setEnabled(false);
    m_layerLabel = new QLabel("Schicht: –");
    connect(m_layerSlider, &QSlider::valueChanged, this, [this] { updatePreview(); });
    auto* layerPanel = new QWidget;
    auto* lv = new QVBoxLayout(layerPanel);
    auto* sliderRow = new QHBoxLayout;
    auto* sliderCol = new QVBoxLayout;
    sliderCol->addWidget(m_layerLabel);
    sliderCol->addWidget(m_layerSlider);
    sliderRow->addLayout(sliderCol);
    auto* eventButton = new QToolButton;
    eventButton->setDefaultAction(aEvent);
    eventButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    eventButton->setIconSize(QSize(32, 32));
    eventButton->setAutoRaise(true);
    sliderRow->addWidget(eventButton);
    lv->addLayout(sliderRow);
    auto* lt = new QLabel("Schichtvorschau");
    lt->setAlignment(Qt::AlignCenter);
    lt->setStyleSheet("color: #5a6270; font-size: 11px; border-top: 1px solid #d0d5dc;");
    lv->addWidget(lt);

    m_ribbon = new QTabWidget;
    m_ribbon->setDocumentMode(true);
    m_ribbon->addTab(makeRibbonPage({makePanel("Skizze", {aSketch}),
                                     makePanel("Erstellen", {aExtrude, aRevolve}),
                                     makePanel("Ändern", {aHole}),
                                     makePanel("Exportieren", {aStl})}),
                     "3D-Modell");
    m_ribbon->addTab(makeRibbonPage({makePanel("Navigieren", {aHome, aFit}),
                                     makePanel("Ansichten", {aTop, aFront, aRight})}),
                     "Ansicht");
    m_profileLabel = new QLabel;
    m_profileLabel->setWordWrap(true);
    m_profileLabel->setMaximumWidth(170);
    m_profileLabel->setStyleSheet("font-size: 11px;");
    auto* printerPanel = makePanel(QString::fromUtf8("Creality Ender 3 V2"), {aSettings, aSlice});
    static_cast<QHBoxLayout*>(static_cast<QVBoxLayout*>(printerPanel->layout())->itemAt(0)->layout())->insertWidget(0, m_profileLabel);
    m_ribbon->addTab(makeRibbonPage({printerPanel,
                                     makePanel("Ergebnis", {m_previewAction, aSave}), layerPanel}),
                     "3D-Druck");
    m_ribbon->setFixedHeight(118);
    m_ribbon->setStyleSheet("QTabWidget::pane { background: #f4f6f9; border-bottom: 1px solid #c8cdd5; }");

    auto* tb = new QToolBar("Multifunktionsleiste");
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->addWidget(m_ribbon);
    addToolBar(Qt::TopToolBarArea, tb);
}

void MainWindow::buildFileMenu()
{
    auto* fileButton = new QToolButton;
    fileButton->setText("Datei");
    fileButton->setPopupMode(QToolButton::InstantPopup);
    fileButton->setStyleSheet("QToolButton { background: #2f6fbf; color: white; padding: 3px 14px; font-weight: bold; border: none; }"
                              "QToolButton::menu-indicator { image: none; }");
    auto* menu = new QMenu(fileButton);
    auto add = [this, menu](const QString& text, const QKeySequence& key, auto slot) {
        QAction* a = menu->addAction(text, this, slot);
        a->setShortcut(key);
        addAction(a);
        return a;
    };
    add("Neu", QKeySequence::New, [this] { cmdNew(); });
    add(QString::fromUtf8("Öffnen …"), QKeySequence::Open, [this] { cmdOpen(); });
    m_recentMenu = menu->addMenu(QString::fromUtf8("Zuletzt geöffnet"));
    menu->addSeparator();
    add("Speichern", QKeySequence::Save, [this] { cmdSave(); });
    add("Speichern unter …", QKeySequence("Ctrl+Shift+S"), [this] { cmdSaveAs(); });
    menu->addSeparator();
    add("STL exportieren …", {}, [this] { cmdExportStl(); });
    add("G-Code speichern …", {}, [this] { cmdSaveGcode(); });
    menu->addSeparator();
    add("Beenden", QKeySequence("Ctrl+Q"), [this] { close(); });
    fileButton->setMenu(menu);
    updateRecentMenu();

    m_undoAction = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), QString::fromUtf8("Rückgängig"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setToolTip(QString::fromUtf8("Rückgängig (Strg+Z)"));
    connect(m_undoAction, &QAction::triggered, this, [this] { cmdUndo(); });
    addAction(m_undoAction);
    m_redoAction = new QAction(style()->standardIcon(QStyle::SP_ArrowForward), "Wiederholen", this);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Y")});
    m_redoAction->setToolTip("Wiederholen (Strg+Y)");
    connect(m_redoAction, &QAction::triggered, this, [this] { cmdRedo(); });
    addAction(m_redoAction);

    auto* corner = new QWidget;
    auto* h = new QHBoxLayout(corner);
    h->setContentsMargins(0, 0, 8, 0);
    h->setSpacing(2);
    h->addWidget(fileButton);
    for (QAction* a : {m_undoAction, m_redoAction}) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        h->addWidget(b);
    }
    m_ribbon->setCornerWidget(corner, Qt::TopLeftCorner);
    updateTitle();
}

void MainWindow::buildBrowser()
{
    m_browser = new QTreeWidget;
    m_browser->setHeaderHidden(true);
    m_browser->setContextMenuPolicy(Qt::CustomContextMenu);
    m_browser->setMinimumWidth(240);

    connect(m_browser, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (auto f = featureOf(item)) cmdEditFeature(f);
    });
    connect(m_browser, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto f = featureOf(m_browser->itemAt(pos));
        if (!f) return;
        QMenu menu;
        menu.addAction(f->type() == fw::FeatureType::Sketch ? "Skizze bearbeiten" : "Feature bearbeiten",
                       this, [this, f] { cmdEditFeature(f); });
        menu.addAction(f->isSuppressed() ? QString::fromUtf8("Unterdrückung aufheben") : QString::fromUtf8("Unterdrücken"),
                       this, [this, f] { cmdToggleSuppress(f); });
        menu.addSeparator();
        menu.addAction(QString::fromUtf8("Löschen"), this, [this, f] { cmdDeleteFeature(f); });
        menu.exec(m_browser->viewport()->mapToGlobal(pos));
    });

    auto* dock = new QDockWidget("Modell", this);
    dock->setObjectName("Modellbrowser");
    dock->setWidget(m_browser);
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::loadDemoPart()
{
    // Beispielteil: Platte mit Bohrung und aufgesetztem Zapfen
    auto sk1 = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk1->rectangles().push_back({0, 0, 60, 40});
    sk1->circles().push_back({15, 20, 6});
    m_doc.addFeature(sk1);
    m_doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk1, 8.0));

    auto sk2 = std::make_shared<fw::Sketch>("Skizze2", fw::SketchPlane::XY, 8.0);
    sk2->circles().push_back({42, 20, 10});
    m_doc.addFeature(sk2);
    m_doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion2", sk2, 15.0));

    resetHistory();
    QTimer::singleShot(150, this, [this] { rebuild(true); });
}

std::vector<fw::SketchPtr> MainWindow::sketches() const
{
    std::vector<fw::SketchPtr> out;
    for (const auto& f : m_doc.features())
        if (auto s = std::dynamic_pointer_cast<fw::Sketch>(f)) out.push_back(s);
    return out;
}

fw::FeaturePtr MainWindow::featureOf(QTreeWidgetItem* item) const
{
    auto it = m_itemFeature.find(item);
    return it == m_itemFeature.end() ? nullptr : it->second;
}

void MainWindow::rebuild(bool fit)
{
    // Verlauf fuer Rueckgaengig: Zustand vor der Aenderung merken
    const std::string text = m_doc.toText();
    if (text != m_lastText) {
        if (!m_restoring) {
            m_undo.push_back(m_lastText);
            if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
            m_redo.clear();
        }
        m_lastText = text;
        m_modified = true;
    }
    ++m_generation;
    updateTitle();

    const bool ok = m_doc.recompute();
    m_slice = fw::SliceResult{}; // Slicing ist nach Modellaenderung veraltet
    if (m_printPreview) setPrintPreview(false);

    const auto& ctx = m_view->context();
    if (ctx.IsNull()) return;

    if (!m_bodyAis.IsNull()) ctx->Remove(m_bodyAis, false);
    for (auto& s : m_sketchAis) ctx->Remove(s, false);
    m_sketchAis.clear();
    m_bodyAis.Nullify();

    if (!m_doc.body().IsNull()) {
        m_bodyAis = new AIS_Shape(m_doc.body());
        m_bodyAis->SetColor(Quantity_Color(0.72, 0.76, 0.82, Quantity_TOC_sRGB));
        m_bodyAis->SetMaterial(Graphic3d_NameOfMaterial_Steel);
        m_bodyAis->Attributes()->SetFaceBoundaryDraw(true);
        m_bodyAis->Attributes()->SetFaceBoundaryAspect(
            new Prs3d_LineAspect(Quantity_NOC_GRAY15, Aspect_TOL_SOLID, 1.0));
        ctx->Display(m_bodyAis, AIS_Shaded, 0, false);
    }

    // Skizzen nur anzeigen, solange sie von keinem Feature verbraucht werden (wie in Inventor)
    for (const auto& s : sketches()) {
        if (m_doc.isUsed(s) || s->isEmpty()) continue;
        Handle(AIS_Shape) a = new AIS_Shape(s->wireframe());
        a->SetColor(Quantity_NOC_ORANGE);
        a->SetWidth(2.0);
        ctx->Display(a, AIS_WireFrame, -1, false);
        m_sketchAis.push_back(a);
    }

    refreshBrowser();
    if (fit) m_view->fitAll();
    else m_view->redraw();

    if (!ok) {
        statusBar()->showMessage(QString::fromUtf8("Fehler in %1: %2")
            .arg(QString::fromStdString(m_doc.errorFeature()->name()), QString::fromStdString(m_doc.errorMessage())), 8000);
    }
}

void MainWindow::refreshBrowser()
{
    m_browser->clear();
    m_itemFeature.clear();

    auto* root = new QTreeWidgetItem(m_browser, {QString::fromStdString(m_doc.name())});
    root->setIcon(0, makeIcon(Icon::Extrude));
    auto* origin = new QTreeWidgetItem(root, {"Ursprung"});
    for (const char* n : {"YZ-Ebene", "XZ-Ebene", "XY-Ebene", "X-Achse", "Y-Achse", "Z-Achse", "Mittelpunkt"})
        new QTreeWidgetItem(origin, {n});

    // Skizzen werden unter dem Feature einsortiert, das sie verbraucht
    std::map<fw::Sketch*, QTreeWidgetItem*> placed;
    auto sketchItem = [&](const fw::SketchPtr& s, QTreeWidgetItem* parent) {
        auto* it = new QTreeWidgetItem(parent, {QString::fromStdString(s->name())});
        it->setIcon(0, makeIcon(Icon::Sketch));
        m_itemFeature[it] = s;
        placed[s.get()] = it;
        return it;
    };

    for (const auto& f : m_doc.features()) {
        QString label = QString::fromStdString(f->name());
        Icon icon = Icon::Sketch;
        fw::SketchPtr used;
        if (auto e = std::dynamic_pointer_cast<fw::ExtrudeFeature>(f)) { icon = Icon::Extrude; used = e->sketch(); }
        else if (auto r = std::dynamic_pointer_cast<fw::RevolveFeature>(f)) { icon = Icon::Revolve; used = r->sketch(); }
        else if (auto s = std::dynamic_pointer_cast<fw::Sketch>(f)) {
            if (!m_doc.isUsed(s)) sketchItem(s, root);
            continue;
        }

        auto* it = new QTreeWidgetItem(root, {label});
        it->setIcon(0, makeIcon(icon));
        m_itemFeature[it] = f;
        if (used && !placed.count(used.get())) sketchItem(used, it);

        if (f->isSuppressed()) {
            QFont font = it->font(0);
            font.setStrikeOut(true);
            it->setFont(0, font);
            it->setForeground(0, Qt::gray);
        }
        if (m_doc.errorFeature() == f) {
            it->setForeground(0, Qt::red);
            it->setToolTip(0, QString::fromStdString(m_doc.errorMessage()));
        }
    }
    root->setExpanded(true);
}

void MainWindow::cmdNewSketch()
{
    NewSketchDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    auto s = std::make_shared<fw::Sketch>(m_doc.nextName("Skizze"), dlg.plane(), dlg.offset());
    SketchEditDialog edit(*s, this);
    if (edit.exec() != QDialog::Accepted) return;
    edit.applyTo(*s);
    m_doc.addFeature(s);
    rebuild();
}

void MainWindow::cmdExtrude()
{
    auto sk = sketches();
    if (sk.empty()) {
        QMessageBox::information(this, "Extrusion", "Bitte zuerst eine 2D-Skizze erstellen.");
        return;
    }
    ExtrudeDialog dlg(sk, this);
    if (dlg.exec() != QDialog::Accepted || !dlg.sketch()) return;
    m_doc.addFeature(std::make_shared<fw::ExtrudeFeature>(m_doc.nextName("Extrusion"), dlg.sketch(),
                                                          dlg.distance(), dlg.operation(), dlg.symmetric()));
    rebuild();
}

void MainWindow::cmdRevolve()
{
    auto sk = sketches();
    if (sk.empty()) {
        QMessageBox::information(this, "Drehung", "Bitte zuerst eine 2D-Skizze erstellen.");
        return;
    }
    RevolveDialog dlg(sk, this);
    if (dlg.exec() != QDialog::Accepted || !dlg.sketch()) return;
    m_doc.addFeature(std::make_shared<fw::RevolveFeature>(m_doc.nextName("Drehung"), dlg.sketch(),
                                                          dlg.axis(), dlg.angle(), dlg.operation()));
    rebuild();
}

void MainWindow::cmdEditFeature(const fw::FeaturePtr& f)
{
    if (auto s = std::dynamic_pointer_cast<fw::Sketch>(f)) {
        SketchEditDialog dlg(*s, this);
        if (dlg.exec() == QDialog::Accepted) { dlg.applyTo(*s); rebuild(); }
    } else if (auto e = std::dynamic_pointer_cast<fw::ExtrudeFeature>(f)) {
        ExtrudeDialog dlg({e->sketch()}, this);
        dlg.setValues(*e);
        if (dlg.exec() == QDialog::Accepted) {
            e->setDistance(dlg.distance());
            e->setOperation(dlg.operation());
            e->setSymmetric(dlg.symmetric());
            rebuild();
        }
    } else if (auto r = std::dynamic_pointer_cast<fw::RevolveFeature>(f)) {
        RevolveDialog dlg({r->sketch()}, this);
        dlg.setValues(*r);
        if (dlg.exec() == QDialog::Accepted) {
            r->setAxis(dlg.axis());
            r->setAngle(dlg.angle());
            r->setOperation(dlg.operation());
            rebuild();
        }
    }
}

void MainWindow::cmdDeleteFeature(const fw::FeaturePtr& f)
{
    if (!m_doc.removeFeature(f)) {
        QMessageBox::warning(this, QString::fromUtf8("Löschen"),
            QString::fromUtf8("Die Skizze wird noch von einem Feature verwendet. Bitte zuerst das Feature löschen."));
        return;
    }
    rebuild();
}

void MainWindow::cmdToggleSuppress(const fw::FeaturePtr& f)
{
    f->setSuppressed(!f->isSuppressed());
    rebuild();
}

void MainWindow::cmdExportStl()
{
    if (m_doc.body().IsNull()) return;
    const QString path = QFileDialog::getSaveFileName(this, "STL exportieren",
        QString::fromStdString(m_doc.name()) + ".stl", "STL-Dateien (*.stl)");
    if (path.isEmpty()) return;
    if (!m_doc.exportStl(path.toStdString()))
        QMessageBox::warning(this, "STL exportieren", "Die Datei konnte nicht geschrieben werden.");
}

void MainWindow::cmdSlice()
{
    startSlice({});
}

void MainWindow::startSlice(std::function<void()> then)
{
    if (m_sliceWatcher->isRunning()) return;
    if (m_doc.body().IsNull()) {
        QMessageBox::information(this, "Slicen", "Es ist noch kein Körper vorhanden.");
        return;
    }
    m_afterSlice = std::move(then);
    m_cancelSlice = false;
    m_sliceProgress = 0.0;
    m_sliceGeneration = m_generation;
    m_progressBar->setValue(0);
    m_progressBar->show();
    m_cancelButton->show();
    m_progressTimer->start(100);

    const TopoDS_Shape body = m_doc.body();
    const fw::PrinterProfile profile = m_profile;
    auto* progress = &m_sliceProgress;
    auto* cancel = &m_cancelSlice;
    m_sliceWatcher->setFuture(QtConcurrent::run([body, profile, progress, cancel] {
        return fw::slice(body, profile, [progress, cancel](double f) {
            progress->store(f);
            return !cancel->load();
        });
    }));
}

void MainWindow::sliceFinished()
{
    m_progressTimer->stop();
    m_progressBar->hide();
    m_cancelButton->hide();
    fw::SliceResult result = m_sliceWatcher->result();
    auto then = std::move(m_afterSlice);
    m_afterSlice = {};

    if (result.cancelled) {
        statusBar()->showMessage("Slicen abgebrochen", 4000);
        return;
    }
    if (m_sliceGeneration != m_generation) {
        statusBar()->showMessage(QString::fromUtf8("Modell wurde während des Slicens geändert, bitte erneut slicen"), 6000);
        return;
    }
    m_slice = std::move(result);
    if (!m_slice.ok) {
        QMessageBox::warning(this, "Slicen", QString::fromStdString(m_slice.error));
        return;
    }
    const int minutes = static_cast<int>(m_slice.estimatedSeconds / 60.0 + 0.5);
    statusBar()->showMessage(QString::fromUtf8("Geslict: %1 Schichten · ca. %2 h %3 min · %4 m Filament · %5 g · %6 €")
        .arg(m_slice.layers.size())
        .arg(minutes / 60).arg(minutes % 60)
        .arg(m_slice.filamentMm / 1000.0, 0, 'f', 2)
        .arg(m_slice.filamentGrams, 0, 'f', 1)
        .arg(m_slice.filamentCost, 0, 'f', 2));
    if (!m_slice.warnings.empty()) {
        QStringList w;
        for (const auto& s : m_slice.warnings) w << QString::fromUtf8(s.c_str());
        QMessageBox::information(this, "Slicen", w.join("\n"));
    }
    m_ribbon->setCurrentIndex(2);
    if (m_printPreview) setPrintPreview(false);
    setPrintPreview(true);
    if (then) then();
}

void MainWindow::cmdSaveGcode()
{
    if (!m_slice.ok) {
        startSlice([this] { if (m_slice.ok) cmdSaveGcode(); });
        return;
    }
    QString base = m_filePath.isEmpty() ? QString::fromStdString(m_doc.name()) : QFileInfo(m_filePath).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(this, "G-Code speichern", base + ".gcode", "G-Code (*.gcode)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "G-Code speichern", "Die Datei konnte nicht geschrieben werden.");
        return;
    }
    file.write(QByteArray::fromStdString(m_slice.gcode));
    statusBar()->showMessage(QString("G-Code gespeichert: %1").arg(path), 6000);
}

void MainWindow::cmdPrintSettings()
{
    PrintSettingsDialog dlg(m_profile, this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_profile = dlg.profile();
    QSettings().setValue("druckprofil", QByteArray::fromStdString(fw::serializeProfile(m_profile)));
    m_profileLabel->setText(QString::fromStdString(m_profile.name));
    const bool wasPreview = m_printPreview;
    m_slice = fw::SliceResult{};
    if (m_printPreview) setPrintPreview(false);
    if (wasPreview) startSlice({});
}

void MainWindow::setPrintPreview(bool on)
{
    const auto& ctx = m_view->context();
    if (on && !m_slice.ok) {
        m_previewAction->setChecked(false);
        startSlice({});
        return;
    }
    m_printPreview = on;
    m_previewAction->setChecked(on);
    m_layerSlider->setEnabled(on);

    if (on) {
        if (m_bedAis.IsNull()) {
            m_bedAis = new AIS_Shape(BRepPrimAPI_MakeBox(gp_Pnt(0, 0, -1.0), m_profile.bedX, m_profile.bedY, 1.0).Shape());
            m_bedAis->SetColor(Quantity_Color(0.20, 0.22, 0.25, Quantity_TOC_sRGB));
        }
        if (!m_bodyAis.IsNull()) ctx->Erase(m_bodyAis, false);
        for (auto& s : m_sketchAis) ctx->Erase(s, false);
        ctx->Display(m_bedAis, AIS_Shaded, -1, false);
        m_layerSlider->blockSignals(true);
        m_layerSlider->setRange(1, static_cast<int>(m_slice.layers.size()));
        m_layerSlider->setValue(static_cast<int>(m_slice.layers.size()));
        m_layerSlider->blockSignals(false);
        updatePreview();
        m_view->fitAll();
    } else {
        if (!m_previewAis.IsNull()) ctx->Remove(m_previewAis, false);
        m_previewAis.Nullify();
        if (!m_bedAis.IsNull()) ctx->Erase(m_bedAis, false);
        if (!m_bodyAis.IsNull()) ctx->Display(m_bodyAis, AIS_Shaded, 0, false);
        for (auto& s : m_sketchAis) ctx->Display(s, AIS_WireFrame, -1, false);
        m_layerLabel->setText("Schicht: –");
        m_view->fitAll();
    }
}

void MainWindow::cmdAddEventAtLayer()
{
    if (!m_slice.ok) {
        startSlice([this] { if (m_slice.ok) cmdAddEventAtLayer(); });
        return;
    }
    if (!m_printPreview) setPrintPreview(true);
    const int layer = m_layerSlider->value();
    const auto& L = m_slice.layers[static_cast<size_t>(layer - 1)];
    HeightEventDialog dlg(L.z - L.height, this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_profile.events.push_back(dlg.event());
    QSettings().setValue("druckprofil", QByteArray::fromStdString(fw::serializeProfile(m_profile)));
    startSlice([this, layer] { m_layerSlider->setValue(layer); });
}

void MainWindow::updatePreview()
{
    if (!m_printPreview || !m_slice.ok) return;
    const auto& ctx = m_view->context();
    const int layer = m_layerSlider->value();
    if (!m_previewAis.IsNull()) ctx->Remove(m_previewAis, false);
    m_previewAis = new ToolpathPreview(m_slice, layer - 1);
    ctx->Display(m_previewAis, 0, -1, false);
    const auto& L = m_slice.layers[static_cast<size_t>(layer - 1)];
    QString text = QString("Schicht: %1 / %2  (Z = %3 mm)").arg(layer).arg(m_slice.layers.size()).arg(L.z, 0, 'f', 2);
    for (const auto& e : L.events)
        text += QString::fromUtf8("  ·  ⚑ ") + QString::fromUtf8(fw::heightEventName(e.type));
    m_layerLabel->setText(text);
    m_view->redraw();
}

// --- Datei ---------------------------------------------------------------------

void MainWindow::resetHistory()
{
    m_undo.clear();
    m_redo.clear();
    m_lastText = m_doc.toText();
    m_modified = false;
    updateTitle();
}

void MainWindow::updateTitle()
{
    const QString name = m_filePath.isEmpty() ? QString::fromStdString(m_doc.name()) : QFileInfo(m_filePath).fileName();
    setWindowTitle(QString("%1%2 – FormWerk").arg(name, m_modified ? "*" : ""));
    if (m_undoAction) m_undoAction->setEnabled(!m_undo.empty());
    if (m_redoAction) m_redoAction->setEnabled(!m_redo.empty());
}

void MainWindow::cmdUndo()
{
    if (m_undo.empty()) return;
    m_redo.push_back(m_lastText);
    const std::string text = m_undo.back();
    m_undo.pop_back();
    restoreText(text);
}

void MainWindow::cmdRedo()
{
    if (m_redo.empty()) return;
    m_undo.push_back(m_lastText);
    const std::string text = m_redo.back();
    m_redo.pop_back();
    restoreText(text);
}

void MainWindow::restoreText(const std::string& text)
{
    m_doc.fromText(text);
    m_restoring = true;
    rebuild();
    m_restoring = false;
}

bool MainWindow::maybeSave()
{
    if (!m_modified) return true;
    const auto answer = QMessageBox::question(this, "FormWerk",
        QString::fromUtf8("Änderungen an \"%1\" speichern?").arg(QString::fromStdString(m_doc.name())),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return cmdSave();
    return true;
}

void MainWindow::cmdNew()
{
    if (!maybeSave()) return;
    m_doc.clear("Bauteil1");
    m_filePath.clear();
    m_slice = fw::SliceResult{};
    resetHistory();
    rebuild(true);
    m_modified = false;
    updateTitle();
}

void MainWindow::cmdOpen()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("Bauteil öffnen"),
        QSettings().value("letzterOrdner").toString(), "FormWerk-Bauteil (*.fwpart)");
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    QFile f(path);
    std::string err;
    fw::PartDocument tmp;
    if (!f.open(QIODevice::ReadOnly) || !tmp.fromText(f.readAll().toStdString(), &err)) {
        QMessageBox::warning(this, QString::fromUtf8("Bauteil öffnen"),
            QString::fromUtf8("Die Datei konnte nicht gelesen werden.\n%1").arg(QString::fromStdString(err)));
        return false;
    }
    m_doc.fromText(tmp.toText());
    m_filePath = path;
    m_slice = fw::SliceResult{};
    resetHistory();
    rebuild(true);
    m_modified = false;
    updateTitle();
    addRecent(path);
    return true;
}

bool MainWindow::cmdSave()
{
    if (m_filePath.isEmpty()) return cmdSaveAs();
    return writeFile(m_filePath);
}

bool MainWindow::cmdSaveAs()
{
    const QString start = m_filePath.isEmpty()
        ? QSettings().value("letzterOrdner").toString() + "/" + QString::fromStdString(m_doc.name()) + ".fwpart"
        : m_filePath;
    QString path = QFileDialog::getSaveFileName(this, "Bauteil speichern unter", start, "FormWerk-Bauteil (*.fwpart)");
    if (path.isEmpty()) return false;
    if (!path.endsWith(".fwpart", Qt::CaseInsensitive)) path += ".fwpart";
    m_doc.setName(QFileInfo(path).completeBaseName().toStdString());
    if (!writeFile(path)) return false;
    m_filePath = path;
    m_lastText = m_doc.toText(); // Namensaenderung ist keine rueckgaengig machbare Aenderung
    refreshBrowser();
    updateTitle();
    return true;
}

bool MainWindow::writeFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Speichern", "Die Datei konnte nicht geschrieben werden.");
        return false;
    }
    f.write(QByteArray::fromStdString(m_doc.toText()));
    m_modified = false;
    updateTitle();
    addRecent(path);
    statusBar()->showMessage(QString("Gespeichert: %1").arg(path), 4000);
    return true;
}

void MainWindow::addRecent(const QString& path)
{
    QSettings s;
    QStringList list = s.value("zuletztGeoeffnet").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 8) list.removeLast();
    s.setValue("zuletztGeoeffnet", list);
    s.setValue("letzterOrdner", QFileInfo(path).absolutePath());
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList list = QSettings().value("zuletztGeoeffnet").toStringList();
    for (const QString& path : list) {
        m_recentMenu->addAction(QFileInfo(path).fileName(), this, [this, path] {
            if (maybeSave()) openFile(path);
        })->setToolTip(path);
    }
    m_recentMenu->setEnabled(!list.isEmpty());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_sliceWatcher->isRunning()) {
        m_cancelSlice = true;
        m_sliceWatcher->waitForFinished();
    }
    if (maybeSave()) e->accept();
    else e->ignore();
}
