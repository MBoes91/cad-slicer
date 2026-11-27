#include "app/MainWindow.h"

#include "app/Dialogs.h"
#include "app/OcctView.h"
#include "app/PropertyPanel.h"
#include "app/SketchCanvas.h"
#include "app/ToolpathPreview.h"
#include "slicer/Settings.h"

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <TopoDS_Compound.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_Drawer.hxx>

#include <QApplication>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QComboBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
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
#include <QActionGroup>
#include <QStackedWidget>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
#include <QTabBar>
#include <QListWidget>
#include <QDateTime>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Einfache, selbst gezeichnete Symbole (keine fremden Grafiken).
enum class Icon { Line, Arc, Dim, Finish, Cancel, Construction, Select, Pause, Sketch, Extrude, Revolve, Hole, Export, Home, Fit, Top, Front, Right, Slice, Save, Settings, Preview, Rect, Circle,
                  Pan, Zoom, Orbit, PrevView, NewDoc, OpenDoc, Disk, Measure, Update, PrintEnv, Shaded, Edges, Wire, Persp, EndOfPart, Close };

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
    case Icon::Line:
        p.setPen(QPen(blue, 4, Qt::SolidLine, Qt::RoundCap)); p.drawLine(10, 50, 54, 14);
        p.setPen(Qt::NoPen); p.setBrush(dark); p.drawRect(6, 46, 8, 8); p.drawRect(50, 10, 8, 8);
        break;
    case Icon::Arc:
        p.setPen(QPen(blue, 4)); p.drawArc(QRect(10, 14, 44, 44), 0, 180 * 16);
        break;
    case Icon::Dim:
        p.setPen(QPen(green, 2)); p.drawLine(8, 40, 56, 40); p.drawLine(8, 30, 8, 50); p.drawLine(56, 30, 56, 50);
        p.setFont(QFont("Sans", 16, QFont::Bold)); p.drawText(QRect(8, 6, 48, 30), Qt::AlignCenter, "25");
        break;
    case Icon::Finish:
        p.setPen(QPen(green, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygon({{12, 34}, {26, 48}, {52, 16}}));
        break;
    case Icon::Cancel:
        p.setPen(QPen(QColor(210, 60, 50), 7, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(16, 16, 48, 48); p.drawLine(48, 16, 16, 48);
        break;
    case Icon::Construction:
        p.setPen(QPen(orange, 4, Qt::DashLine)); p.drawLine(8, 52, 56, 12);
        break;
    case Icon::Select:
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygon({{18, 8}, {18, 52}, {29, 41}, {37, 58}, {44, 55}, {36, 38}, {50, 38}}));
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
    case Icon::Pan: {
        p.setPen(QPen(dark, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(32, 6, 32, 58); p.drawLine(6, 32, 58, 32);
        p.setBrush(dark);
        p.drawPolygon(QPolygon({{32, 4}, {24, 14}, {40, 14}}));
        p.drawPolygon(QPolygon({{32, 60}, {24, 50}, {40, 50}}));
        p.drawPolygon(QPolygon({{4, 32}, {14, 24}, {14, 40}}));
        p.drawPolygon(QPolygon({{60, 32}, {50, 24}, {50, 40}}));
        break;
    }
    case Icon::Zoom:
        p.setPen(QPen(dark, 5)); p.setBrush(QColor(200, 220, 245)); p.drawEllipse(8, 8, 34, 34);
        p.setPen(QPen(dark, 8, Qt::SolidLine, Qt::RoundCap)); p.drawLine(40, 40, 56, 56);
        break;
    case Icon::Orbit:
        p.setPen(QPen(blue, 4)); p.drawArc(QRect(6, 18, 52, 28), 30 * 16, 300 * 16);
        p.setPen(Qt::NoPen); p.setBrush(blue); p.drawPolygon(QPolygon({{50, 14}, {58, 26}, {44, 26}}));
        p.setBrush(QColor(150, 185, 230)); p.setPen(QPen(dark, 2)); p.drawEllipse(24, 24, 16, 16);
        break;
    case Icon::PrevView:
        p.setPen(QPen(dark, 3)); p.setBrush(QColor(220, 225, 232)); p.drawRect(18, 18, 38, 30);
        p.setPen(QPen(blue, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(6, 30, 26, 30); p.drawPolyline(QPolygon({{14, 22}, {6, 30}, {14, 38}}));
        break;
    case Icon::NewDoc:
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygon({{14, 6}, {40, 6}, {52, 18}, {52, 58}, {14, 58}}));
        p.drawPolyline(QPolygon({{40, 6}, {40, 18}, {52, 18}}));
        break;
    case Icon::OpenDoc:
        p.setBrush(QColor(240, 200, 90));
        p.drawPolygon(QPolygon({{6, 14}, {24, 14}, {28, 20}, {54, 20}, {54, 52}, {6, 52}}));
        p.setBrush(QColor(250, 220, 120));
        p.drawPolygon(QPolygon({{6, 52}, {16, 28}, {60, 28}, {54, 52}}));
        break;
    case Icon::Disk:
        p.setBrush(QColor(70, 110, 170)); p.drawRect(8, 8, 48, 48);
        p.setBrush(Qt::white); p.drawRect(18, 8, 28, 18); p.drawRect(16, 36, 32, 20);
        break;
    case Icon::Measure:
        p.setBrush(QColor(245, 215, 110)); p.drawRect(6, 22, 52, 20);
        for (int i = 0; i < 6; ++i) p.drawLine(12 + i * 8, 22, 12 + i * 8, i % 2 ? 30 : 34);
        break;
    case Icon::Update:
        p.setPen(QPen(green, 5)); p.drawArc(QRect(10, 10, 44, 44), 60 * 16, 280 * 16);
        p.setPen(Qt::NoPen); p.setBrush(green); p.drawPolygon(QPolygon({{40, 4}, {52, 14}, {36, 18}}));
        break;
    case Icon::PrintEnv:
        p.setBrush(QColor(220, 225, 232)); p.drawRect(8, 8, 48, 48);
        p.setBrush(dark); p.drawRect(22, 14, 20, 10); p.drawPolygon(QPolygon({{28, 24}, {36, 24}, {32, 32}}));
        p.setPen(Qt::NoPen); p.setBrush(orange);
        for (int i = 0; i < 3; ++i) p.drawRect(16, 50 - i * 6, 32, 4);
        break;
    case Icon::Shaded:
    case Icon::Edges:
    case Icon::Wire: {
        QPolygon front({{10, 30}, {40, 30}, {40, 56}, {10, 56}});
        QPolygon top({{10, 30}, {24, 16}, {54, 16}, {40, 30}});
        QPolygon side({{40, 30}, {54, 16}, {54, 42}, {40, 56}});
        if (kind == Icon::Wire) { p.setBrush(Qt::NoBrush); p.setPen(QPen(dark, 2)); }
        else if (kind == Icon::Shaded) { p.setPen(Qt::NoPen); }
        if (kind != Icon::Wire) p.setBrush(QColor(150, 185, 230));
        p.drawPolygon(front);
        if (kind != Icon::Wire) p.setBrush(QColor(185, 210, 240));
        p.drawPolygon(top);
        if (kind != Icon::Wire) p.setBrush(QColor(110, 150, 205));
        p.drawPolygon(side);
        break;
    }
    case Icon::Persp:
        p.setBrush(QColor(150, 185, 230));
        p.drawPolygon(QPolygon({{8, 12}, {56, 22}, {56, 42}, {8, 52}}));
        break;
    case Icon::EndOfPart:
        p.setPen(Qt::NoPen); p.setBrush(QColor(200, 50, 40));
        p.drawRoundedRect(QRect(8, 22, 48, 20), 4, 4);
        p.setBrush(Qt::white); p.drawRect(14, 29, 36, 6);
        break;
    case Icon::Close:
        p.setBrush(QColor(210, 60, 50)); p.setPen(Qt::NoPen); p.drawRoundedRect(QRect(6, 6, 52, 52), 8, 8);
        p.setPen(QPen(Qt::white, 6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(20, 20, 44, 44); p.drawLine(44, 20, 20, 44);
        break;
    }
    return QIcon(pm);
}

QIcon modifierIcon(const QColor& c)
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill = c;
    fill.setAlpha(110);
    p.setPen(QPen(c, 3, Qt::DashLine));
    p.setBrush(fill);
    p.drawPolygon(QPolygon({{10, 26}, {24, 12}, {54, 12}, {54, 40}, {40, 54}, {10, 54}}));
    p.drawLine(10, 26, 40, 26); p.drawLine(40, 26, 54, 12); p.drawLine(40, 26, 40, 54);
    return QIcon(pm);
}

QIcon textIcon(const QString& text)
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(120, 130, 145), 2));
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRect(8, 8, 48, 48), 6, 6);
    p.setPen(QColor(40, 60, 90));
    p.setFont(QFont("Sans", 24, QFont::Bold));
    p.drawText(QRect(8, 8, 48, 48), Qt::AlignCenter, text);
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

    m_view = new OcctView;
    m_canvas = new SketchCanvas;
    connect(m_canvas, &SketchCanvas::statusChanged, this, [this](const QString& t) { statusBar()->showMessage(t); });
    connect(m_view, &OcctView::hint, this, [this](const QString& t) {
        if (t.isEmpty()) statusBar()->showMessage("Bereit");
        else statusBar()->showMessage(t);
    });
    connect(m_view, &OcctView::contextMenuAt, this, [this](const QPoint& pos) { showViewContextMenu(pos); });

    // Grafikbereich: 3D-Fenster mit Navigationsleiste rechts
    m_modelPage = new QWidget;
    auto* mh = new QHBoxLayout(m_modelPage);
    mh->setContentsMargins(0, 0, 0, 0);
    mh->setSpacing(0);
    mh->addWidget(m_view, 1);
    m_navBar = new QToolBar;
    m_navBar->setOrientation(Qt::Vertical);
    m_navBar->setIconSize(QSize(22, 22));
    m_navBar->setStyleSheet("QToolBar { background: #eef1f5; border-left: 1px solid #c8cdd5; spacing: 2px; padding: 4px 1px; }");
    mh->addWidget(m_navBar);

    m_homePage = buildHomePage();

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_homePage);
    m_stack->addWidget(m_modelPage);
    m_stack->addWidget(m_canvas);

    // Dokument-Register unten wie in Inventor
    m_docTabs = new QTabBar;
    m_docTabs->setShape(QTabBar::RoundedSouth);
    m_docTabs->setDocumentMode(true);
    m_docTabs->setExpanding(false);
    m_docTabs->setDrawBase(false);
    m_docTabs->addTab(makeIcon(Icon::Home), QString());
    m_docTabs->setTabToolTip(0, "Startseite");
    connect(m_docTabs, &QTabBar::currentChanged, this, [this](int i) {
        if (m_syncingTabs) return;
        if (i == 0) showHome();
        else showModel();
    });

    auto* central = new QWidget;
    auto* cv = new QVBoxLayout(central);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(m_stack, 1);
    cv->addWidget(m_docTabs);
    setCentralWidget(central);

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

    // Zuletzt verwendetes Druckprofil wiederherstellen
    QSettings settings;
    const QByteArray stored = settings.value("druckprofil").toByteArray();
    if (!stored.isEmpty()) {
        fw::PrinterProfile p;
        if (fw::deserializeProfile(stored.toStdString(), p)) m_profile = p;
    }
    m_profileLabel->setText(QString::fromStdString(m_profile.name));

    if (qEnvironmentVariableIsSet("FORMWERK_DEMO")) loadDemoPart();
    else showHome();
}

QWidget* MainWindow::buildHomePage()
{
    auto* page = new QWidget;
    page->setObjectName("Startseite");
    page->setStyleSheet("#Startseite { background: #f2f4f7; }"
                        "QLabel#Titel { font-size: 22px; font-weight: bold; color: #26364d; }"
                        "QLabel#Abschnitt { font-size: 13px; font-weight: bold; color: #4a5568; margin-top: 10px; }"
                        "QToolButton { background: white; border: 1px solid #cfd5dd; border-radius: 4px; padding: 8px; font-size: 12px; }"
                        "QToolButton:hover { border-color: #2f6fbf; background: #eaf2fc; }"
                        "QListWidget { background: white; border: 1px solid #cfd5dd; font-size: 13px; }"
                        "QListWidget::item { padding: 6px; }"
                        "QListWidget::item:hover { background: #eaf2fc; }");
    auto* h = new QHBoxLayout(page);
    h->setContentsMargins(40, 30, 40, 30);
    h->setSpacing(40);

    auto* left = new QVBoxLayout;
    auto* title = new QLabel("FormWerk");
    title->setObjectName("Titel");
    left->addWidget(title);
    auto* newLabel = new QLabel("Neu");
    newLabel->setObjectName("Abschnitt");
    left->addWidget(newLabel);
    auto bigButton = [](const QIcon& icon, const QString& text) {
        auto* b = new QToolButton;
        b->setIcon(icon);
        b->setIconSize(QSize(48, 48));
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setFixedSize(120, 100);
        return b;
    };
    auto* row = new QHBoxLayout;
    auto* bPart = bigButton(makeIcon(Icon::Extrude), "Bauteil");
    bPart->setToolTip("Neues Bauteil (Strg+N)");
    connect(bPart, &QToolButton::clicked, this, [this] { cmdNew(); });
    auto* bDemo = bigButton(makeIcon(Icon::Revolve), "Beispiel");
    bDemo->setToolTip(QString::fromUtf8("Beispielbauteil öffnen"));
    connect(bDemo, &QToolButton::clicked, this, [this] { if (maybeSave()) loadDemoPart(); });
    row->addWidget(bPart);
    row->addWidget(bDemo);
    row->addStretch();
    left->addLayout(row);
    auto* openLabel = new QLabel(QString::fromUtf8("Öffnen"));
    openLabel->setObjectName("Abschnitt");
    left->addWidget(openLabel);
    auto* row2 = new QHBoxLayout;
    auto* bOpen = bigButton(makeIcon(Icon::OpenDoc), QString::fromUtf8("Öffnen …"));
    bOpen->setToolTip(QString::fromUtf8("Bauteil öffnen (Strg+O)"));
    connect(bOpen, &QToolButton::clicked, this, [this] { cmdOpen(); });
    row2->addWidget(bOpen);
    row2->addStretch();
    left->addLayout(row2);
    left->addStretch();
    h->addLayout(left);

    auto* right = new QVBoxLayout;
    auto* recentLabel = new QLabel("Zuletzt verwendet");
    recentLabel->setObjectName("Abschnitt");
    right->addWidget(recentLabel);
    m_recentList = new QListWidget;
    m_recentList->setIconSize(QSize(32, 32));
    connect(m_recentList, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) {
        const QString path = it->data(Qt::UserRole).toString();
        if (maybeSave()) openFile(path);
    });
    right->addWidget(m_recentList, 1);
    auto* hintLabel = new QLabel(QString::fromUtf8("Doppelklick öffnet die Datei."));
    hintLabel->setStyleSheet("color: #6b7380;");
    right->addWidget(hintLabel);
    h->addLayout(right, 1);
    return page;
}

void MainWindow::refreshHomePage()
{
    m_recentList->clear();
    const QStringList list = QSettings().value("zuletztGeoeffnet").toStringList();
    for (const QString& path : list) {
        QFileInfo fi(path);
        auto* it = new QListWidgetItem(makeIcon(Icon::Extrude),
            QString("%1\n%2  ·  %3").arg(fi.completeBaseName(), fi.absolutePath(),
                fi.exists() ? fi.lastModified().toString("dd.MM.yyyy HH:mm") : QString::fromUtf8("nicht gefunden")));
        it->setData(Qt::UserRole, path);
        if (!fi.exists()) it->setForeground(Qt::gray);
        m_recentList->addItem(it);
    }
    if (list.isEmpty()) {
        auto* it = new QListWidgetItem(QString::fromUtf8("Noch keine Dateien geöffnet"));
        it->setFlags(Qt::NoItemFlags);
        m_recentList->addItem(it);
    }
}

void MainWindow::syncDocTabs(bool home)
{
    m_syncingTabs = true;
    if (m_docOpen && m_docTabs->count() < 2) m_docTabs->addTab(makeIcon(Icon::Extrude), QString());
    if (!m_docOpen && m_docTabs->count() > 1) m_docTabs->removeTab(1);
    if (m_docOpen) m_docTabs->setTabText(1, QString::fromStdString(m_doc.name()) + (m_modified ? "*" : ""));
    m_docTabs->setCurrentIndex(home || !m_docOpen ? 0 : 1);
    m_syncingTabs = false;
}

void MainWindow::showHome()
{
    endPlanePick();
    endFeatureEdit(false);
    if (m_editSketch) finishSketch(true);
    refreshHomePage();
    m_stack->setCurrentWidget(m_homePage);
    m_ribbon->setVisible(false);
    m_browserDock->setVisible(false);
    for (QAction* a : m_docActions) a->setEnabled(m_docOpen);
    syncDocTabs(true);
    statusBar()->showMessage("Bereit");
}

void MainWindow::showModel()
{
    if (!m_docOpen) { showHome(); return; }
    m_stack->setCurrentWidget(m_editSketch ? static_cast<QWidget*>(m_canvas) : m_modelPage);
    m_ribbon->setVisible(true);
    m_browserDock->setVisible(true);
    for (QAction* a : m_docActions) a->setEnabled(true);
    syncDocTabs(false);
}

void MainWindow::setPageShown(QWidget* page, bool on)
{
    const int i = m_ribbon->indexOf(page);
    if (i >= 0) m_ribbon->setTabVisible(i, on);
}

void MainWindow::enterPrintEnvironment()
{
    if (m_inPrintEnv) { m_ribbon->setCurrentWidget(m_printPage); return; }
    m_inPrintEnv = true;
    setPageShown(m_printPage, true);
    m_ribbon->setCurrentWidget(m_printPage);
    statusBar()->showMessage(QString::fromUtf8("3D-Druck: Profil wählen, Modifikatoren setzen, slicen · \"3D-Druck beenden\" kehrt zum Modell zurück"), 8000);
}

void MainWindow::exitPrintEnvironment()
{
    if (!m_inPrintEnv) return;
    m_inPrintEnv = false;
    if (m_printPreview) setPrintPreview(false);
    setPageShown(m_printPage, false);
    m_ribbon->setCurrentWidget(m_modelTabPage);
}

void MainWindow::setVisualStyle(int style)
{
    m_visualStyle = style;
    m_styleShaded->setChecked(style == 0);
    m_styleEdges->setChecked(style == 1);
    m_styleWire->setChecked(style == 2);
    if (!m_bodyAis.IsNull()) {
        applyBodyStyle();
        m_view->redraw();
    }
}

void MainWindow::applyBodyStyle()
{
    const auto& ctx = m_view->context();
    if (ctx.IsNull() || m_bodyAis.IsNull()) return;
    m_bodyAis->Attributes()->SetFaceBoundaryDraw(m_visualStyle == 1);
    m_bodyAis->SetToUpdate();
    if (ctx->IsDisplayed(m_bodyAis)) {
        ctx->SetDisplayMode(m_bodyAis, m_visualStyle == 2 ? AIS_WireFrame : AIS_Shaded, false);
        ctx->Redisplay(m_bodyAis, false);
    }
}

void MainWindow::showViewContextMenu(const QPoint& pos)
{
    QMenu menu;
    menu.addAction(makeIcon(Icon::Sketch), "Neue Skizze", this, [this] { cmdNewSketch(); })->setEnabled(!m_printPreview);
    menu.addAction(makeIcon(Icon::Extrude), "Extrusion …", this, [this] { cmdExtrude(); })->setEnabled(!m_printPreview);
    menu.addAction(makeIcon(Icon::Revolve), "Drehung …", this, [this] { cmdRevolve(); })->setEnabled(!m_printPreview);
    menu.addSeparator();
    menu.addAction(makeIcon(Icon::Fit), "Alles zoomen", this, [this] { m_view->fitAll(); });
    menu.addAction(makeIcon(Icon::PrevView), "Vorherige Ansicht", this, [this] { m_view->previousView(); })
        ->setEnabled(m_view->hasPreviousView());
    menu.addAction(makeIcon(Icon::Home), "Ausgangsansicht", this, [this] { m_view->homeView(); });
    menu.addSeparator();
    if (m_inPrintEnv) menu.addAction(makeIcon(Icon::Close), "3D-Druck beenden", this, [this] { exitPrintEnvironment(); });
    else menu.addAction(makeIcon(Icon::PrintEnv), "3D-Druck", this, [this] { enterPrintEnvironment(); });
    menu.exec(pos);
}

void MainWindow::cmdMeasure()
{
    const TopoDS_Shape& body = m_doc.body();
    if (body.IsNull()) {
        QMessageBox::information(this, "Physikalische Eigenschaften", QString::fromUtf8("Das Bauteil hat noch keinen Körper."));
        return;
    }
    GProp_GProps vol, surf;
    BRepGProp::VolumeProperties(body, vol);
    BRepGProp::SurfaceProperties(body, surf);
    Bnd_Box box;
    BRepBndLib::Add(body, box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    const gp_Pnt c = vol.CentreOfMass();
    const double v = vol.Mass();                       // mm³
    const double grams = v / 1000.0 * m_profile.filamentDensity; // massiv gedruckt
    QMessageBox::information(this, "Physikalische Eigenschaften",
        QString::fromUtf8("Volumen: %1 mm³\nOberfläche: %2 mm²\nMasse (massiv, %3 g/cm³): %4 g\n"
                          "Schwerpunkt: X %5 · Y %6 · Z %7 mm\nAbmessungen: %8 × %9 × %10 mm")
            .arg(v, 0, 'f', 1).arg(surf.Mass(), 0, 'f', 1)
            .arg(m_profile.filamentDensity, 0, 'f', 2).arg(grams, 0, 'f', 1)
            .arg(c.X(), 0, 'f', 2).arg(c.Y(), 0, 'f', 2).arg(c.Z(), 0, 'f', 2)
            .arg(x1 - x0, 0, 'f', 2).arg(y1 - y0, 0, 'f', 2).arg(z1 - z0, 0, 'f', 2));
}

void MainWindow::buildRibbon()
{
    auto act = [this](const QString& text, Icon icon, const QKeySequence& key, auto slot) {
        auto* a = new QAction(makeIcon(icon), text, this);
        const QString plain = QString(text).replace("-\n", "").replace("\n", " ");
        if (!key.isEmpty()) {
            a->setShortcut(key);
            a->setToolTip(QString("%1 (%2)").arg(plain, key.toString(QKeySequence::NativeText)));
        } else {
            a->setToolTip(plain);
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
            QString::fromUtf8("Das Bohrungs-Feature folgt in einem der nächsten Schritte.\n"
                              "Bis dahin: Kreis in eine Skizze zeichnen und mit \"Differenz\" extrudieren."));
    });
    auto* aPrintEnv = act("3D-Druck", Icon::PrintEnv, {}, [this] { enterPrintEnvironment(); });
    aPrintEnv->setToolTip(QString::fromUtf8("3D-Druck-Umgebung: Druckeinstellungen, Modifikatoren, Slicen, G-Code"));

    // --- Prüfen / Verwalten
    auto* aMeasure = act("Physikalische\nEigenschaften", Icon::Measure, {}, [this] { cmdMeasure(); });
    auto* aUpdate = act("Neu\nerstellen", Icon::Update, QKeySequence("Ctrl+Shift+Q"), [this] { rebuild(); });
    aUpdate->setToolTip("Alle Features neu berechnen");

    // --- Ansicht und Navigation (Kuerzel wie in Inventor)
    auto* aHome = act("Ausgangs-\nansicht", Icon::Home, QKeySequence(Qt::Key_F6), [this] { m_view->homeView(); });
    auto* aFit = act("Alles\nzoomen", Icon::Fit, QKeySequence(Qt::Key_Home), [this] { m_view->fitAll(); });
    auto* aPrev = act("Vorherige\nAnsicht", Icon::PrevView, QKeySequence(Qt::Key_F5), [this] { m_view->previousView(); });
    auto* aPan = act("Pan", Icon::Pan, {}, [this] { m_view->setNavMode(OcctView::NavMode::Pan, true); m_view->setFocus(); });
    aPan->setToolTip(QString::fromUtf8("Pan (F2 gedrückt halten + linke Maustaste, oder mittlere Maustaste)"));
    auto* aZoom = act("Zoom", Icon::Zoom, {}, [this] { m_view->setNavMode(OcctView::NavMode::Zoom, true); m_view->setFocus(); });
    aZoom->setToolTip(QString::fromUtf8("Zoom (F3 gedrückt halten + linke Maustaste, oder Mausrad)"));
    auto* aOrbit = act("Orbit", Icon::Orbit, {}, [this] { m_view->setNavMode(OcctView::NavMode::Orbit, true); m_view->setFocus(); });
    aOrbit->setToolTip(QString::fromUtf8("Orbit (F4 gedrückt halten + linke Maustaste, oder Umschalt + mittlere Maustaste)"));
    auto* aTop = act("Oben", Icon::Top, {}, [this] { m_view->setStandardView(V3d_Zpos); });
    auto* aFront = act("Vorne", Icon::Front, {}, [this] { m_view->setStandardView(V3d_Yneg); });
    auto* aRight = act("Rechts", Icon::Right, {}, [this] { m_view->setStandardView(V3d_Xpos); });
    m_styleShaded = act("Schattiert", Icon::Shaded, {}, [this] { setVisualStyle(0); });
    m_styleEdges = act("Mit Kanten", Icon::Edges, {}, [this] { setVisualStyle(1); });
    m_styleWire = act("Drahtkörper", Icon::Wire, {}, [this] { setVisualStyle(2); });
    for (QAction* a : {m_styleShaded, m_styleEdges, m_styleWire}) a->setCheckable(true);
    m_styleEdges->setChecked(true);
    auto* aPersp = act("Perspektive", Icon::Persp, {}, [] {});
    aPersp->setCheckable(true);
    connect(aPersp, &QAction::toggled, this, [this](bool on) { m_view->setPerspective(on); });

    for (QAction* a : {aPan, aZoom, aOrbit, aFit, aPrev, aHome}) m_navBar->addAction(a);

    // --- 3D-Druck (kontextuelles Register der Umgebung)
    auto* aSlice = act("Slicen", Icon::Slice, QKeySequence("Ctrl+R"), [this] { cmdSlice(); });
    m_previewAction = act("Druck-\nvorschau", Icon::Preview, {}, [this] { setPrintPreview(!m_printPreview); });
    m_previewAction->setCheckable(true);
    auto* aSave = act("G-Code\nspeichern", Icon::Save, QKeySequence("Ctrl+G"), [this] { cmdSaveGcode(); });
    auto* aSettings = act("Druckein-\nstellungen", Icon::Settings, {}, [this] { cmdPrintSettings(); });
    auto* aExitPrint = act("3D-Druck\nbeenden", Icon::Close, {}, [this] { exitPrintEnvironment(); });

    auto* aEvent = act("Ereignis", Icon::Pause, {}, [this] { cmdAddEventAtLayer(); });
    aEvent->setToolTip(QString::fromUtf8("Pause, Filamentwechsel, Temperatur o. Ä. ab der angezeigten Schicht einfügen"));

    auto* aZone = act("Infill-\nZone", Icon::Fit, {}, [this] { cmdAddModifier(fw::PrintModifier::Role::InfillZone); });
    auto* aEnforce = act(QString::fromUtf8("Stütze\nerzwingen"), Icon::Slice, {}, [this] { cmdAddModifier(fw::PrintModifier::Role::SupportEnforcer); });
    auto* aBlock = act(QString::fromUtf8("Stütze\nsperren"), Icon::Cancel, {}, [this] { cmdAddModifier(fw::PrintModifier::Role::SupportBlocker); });
    aZone->setIcon(modifierIcon(QColor(60, 160, 90)));
    aEnforce->setIcon(modifierIcon(QColor(40, 170, 200)));
    aBlock->setIcon(modifierIcon(QColor(210, 70, 60)));

    m_modelActions = {aZone, aEnforce, aBlock, aSketch, aExtrude, aRevolve, aHole, aSlice, m_previewAction, aSave,
                      aSettings, aEvent, aPrintEnv, aMeasure, aUpdate, aExitPrint};
    m_docActions = {aSketch, aExtrude, aRevolve, aHole, aPrintEnv, aMeasure, aUpdate, aHome, aFit, aPrev, aPan, aZoom, aOrbit,
                    aTop, aFront, aRight, aSlice, aSave, aEvent, aZone, aEnforce, aBlock, m_previewAction};

    // --- Skizze (kontextuelles Register, nur im Skizzenmodus sichtbar)
    auto* toolGroup = new QActionGroup(this);
    auto tool = [this, toolGroup](const QString& text, const QIcon& icon, SketchCanvas::Tool t, const QString& key = {}) {
        auto* a = new QAction(icon, text, this);
        a->setCheckable(true);
        a->setData(static_cast<int>(t));
        a->setToolTip(key.isEmpty() ? QString(text).replace("\n", " ") : QString("%1 (%2)").arg(QString(text).replace("\n", " "), key));
        toolGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, t] { m_canvas->setTool(t); m_canvas->setFocus(); });
        m_sketchToolActions.push_back(a);
        return a;
    };
    auto* sSelect = tool("Auswählen", makeIcon(Icon::Select), SketchCanvas::Tool::Select, "Esc");
    auto* sLine = tool("Linie", makeIcon(Icon::Line), SketchCanvas::Tool::Line, "L");
    auto* sRect = tool("Rechteck", makeIcon(Icon::Rect), SketchCanvas::Tool::Rectangle, "R");
    auto* sCircle = tool("Kreis", makeIcon(Icon::Circle), SketchCanvas::Tool::Circle, "C");
    auto* sArc = tool("Bogen", makeIcon(Icon::Arc), SketchCanvas::Tool::Arc, "A");
    auto* sDim = tool("Bemaßung", makeIcon(Icon::Dim), SketchCanvas::Tool::Dimension, "D");
    auto* cCoin = tool("Koinzident", textIcon(QString::fromUtf8("●")), SketchCanvas::Tool::Coincident);
    auto* cH = tool("Horizontal", textIcon("H"), SketchCanvas::Tool::Horizontal);
    auto* cV = tool("Vertikal", textIcon("V"), SketchCanvas::Tool::Vertical);
    auto* cPar = tool("Parallel", textIcon(QString::fromUtf8("∥")), SketchCanvas::Tool::Parallel);
    auto* cPerp = tool("Senkrecht", textIcon(QString::fromUtf8("⊥")), SketchCanvas::Tool::Perpendicular);
    auto* cTan = tool("Tangential", textIcon("T"), SketchCanvas::Tool::Tangent);
    auto* cEq = tool("Gleich", textIcon("="), SketchCanvas::Tool::Equal);
    auto* cFix = tool("Fixieren", textIcon("F"), SketchCanvas::Tool::Fix);
    auto* cMid = tool("Mittelpunkt", textIcon("M"), SketchCanvas::Tool::Midpoint);
    auto* sConstr = new QAction(makeIcon(Icon::Construction), "Konstruk-\ntion", this);
    sConstr->setToolTip("Gewähltes Objekt zu Konstruktionsgeometrie machen (zählt nicht zum Profil)");
    connect(sConstr, &QAction::triggered, this, [this] { m_canvas->toggleConstruction(); });
    auto* sShow = new QAction(textIcon("H V"), QString::fromUtf8("Abhängig-\nkeiten"), this);
    sShow->setCheckable(true);
    sShow->setChecked(true);
    connect(sShow, &QAction::toggled, this, [this](bool on) { m_canvas->setShowConstraints(on); });
    auto* sFinish = new QAction(makeIcon(Icon::Finish), "Skizze\nfertig stellen", this);
    connect(sFinish, &QAction::triggered, this, [this] { finishSketch(true); });
    auto* sCancel = new QAction(makeIcon(Icon::Cancel), "Abbrechen", this);
    connect(sCancel, &QAction::triggered, this, [this] { finishSketch(false); });
    connect(m_canvas, &SketchCanvas::toolChanged, this, [this] {
        for (QAction* a : m_sketchToolActions)
            a->setChecked(a->data().toInt() == static_cast<int>(m_canvas->tool()));
    });

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

    // Kontextuelle Register stehen wie in Inventor ganz vorne
    m_sketchPage = makeRibbonPage({makePanel("Erstellen", {sSelect, sLine, sRect, sCircle, sArc}),
                                   makePanel("Bemaßung", {sDim}),
                                   makePanel("Abhängig machen", {cCoin, cH, cV, cPar, cPerp, cTan, cEq, cFix, cMid}),
                                   makePanel("Format", {sConstr, sShow}),
                                   makePanel("Beenden", {sFinish, sCancel})});
    m_ribbon->addTab(m_sketchPage, "Skizze");

    m_profileLabel = new QLabel;
    m_profileLabel->setWordWrap(true);
    m_profileLabel->setMaximumWidth(170);
    m_profileLabel->setStyleSheet("font-size: 11px;");
    auto* printerPanel = makePanel(QString::fromUtf8("Creality Ender 3 V2"), {aSettings, aSlice});
    static_cast<QHBoxLayout*>(static_cast<QVBoxLayout*>(printerPanel->layout())->itemAt(0)->layout())->insertWidget(0, m_profileLabel);
    m_printPage = makeRibbonPage({printerPanel,
                                  makePanel("Modifikatoren", {aZone, aEnforce, aBlock}),
                                  makePanel("Ergebnis", {m_previewAction, aSave}), layerPanel,
                                  makePanel("Beenden", {aExitPrint})});
    m_ribbon->addTab(m_printPage, "3D-Druck");

    m_modelTabPage = makeRibbonPage({makePanel("Skizze", {aSketch}),
                                     makePanel("Erstellen", {aExtrude, aRevolve}),
                                     makePanel("Ändern", {aHole})});
    m_ribbon->addTab(m_modelTabPage, "3D-Modell");
    m_ribbon->addTab(makeRibbonPage({makePanel("Messen", {aMeasure})}), QString::fromUtf8("Prüfen"));
    m_ribbon->addTab(makeRibbonPage({makePanel("Aktualisieren", {aUpdate})}), "Verwalten");
    m_ribbon->addTab(makeRibbonPage({makePanel("Darstellung", {m_styleShaded, m_styleEdges, m_styleWire, aPersp}),
                                     makePanel("Navigieren", {aPan, aZoom, aOrbit, aFit, aPrev, aHome}),
                                     makePanel("Ansichten", {aTop, aFront, aRight})}),
                     "Ansicht");
    m_ribbon->addTab(makeRibbonPage({makePanel("Fertigung", {aPrintEnv})}), "Umgebungen");

    setPageShown(m_sketchPage, false);
    setPageShown(m_printPage, false);
    m_ribbon->setCurrentWidget(m_modelTabPage);
    m_ribbon->setFixedHeight(118);
    m_ribbon->setStyleSheet("QTabWidget::pane { background: #f4f6f9; border-bottom: 1px solid #c8cdd5; }");
}

void MainWindow::buildFileMenu()
{
    auto* fileButton = new QToolButton;
    fileButton->setText("Datei");
    fileButton->setPopupMode(QToolButton::InstantPopup);
    fileButton->setStyleSheet("QToolButton { background: #2f6fbf; color: white; padding: 3px 14px; font-weight: bold; border: none; }"
                              "QToolButton::menu-indicator { image: none; }");
    auto* menu = new QMenu(fileButton);
    auto add = [this, menu](const QIcon& icon, const QString& text, const QKeySequence& key, auto slot) {
        QAction* a = menu->addAction(icon, text, this, slot);
        a->setShortcut(key);
        addAction(a);
        return a;
    };
    auto* aNew = add(makeIcon(Icon::NewDoc), "Neu", QKeySequence::New, [this] { cmdNew(); });
    auto* aOpen = add(makeIcon(Icon::OpenDoc), QString::fromUtf8("Öffnen …"), QKeySequence::Open, [this] { cmdOpen(); });
    m_recentMenu = menu->addMenu(QString::fromUtf8("Zuletzt verwendet"));
    menu->addSeparator();
    auto* aSave = add(makeIcon(Icon::Disk), "Speichern", QKeySequence::Save, [this] { cmdSave(); });
    auto* aSaveAs = add(QIcon(), "Speichern unter …", QKeySequence("Ctrl+Shift+S"), [this] { cmdSaveAs(); });
    menu->addSeparator();
    auto* exportMenu = menu->addMenu(makeIcon(Icon::Export), "Exportieren");
    QAction* aStl = exportMenu->addAction("STL-Datei …", this, [this] { cmdExportStl(); });
    QAction* aGcode = exportMenu->addAction("G-Code …", this, [this] { cmdSaveGcode(); });
    menu->addSeparator();
    auto* aClose = add(makeIcon(Icon::Close), QString::fromUtf8("Schließen"), QKeySequence("Ctrl+W"), [this] { cmdCloseDoc(); });
    add(QIcon(), "Beenden", QKeySequence("Ctrl+Q"), [this] { close(); });
    fileButton->setMenu(menu);
    updateRecentMenu();
    for (QAction* a : {aSave, aSaveAs, aStl, aGcode, aClose}) m_docActions.push_back(a);

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
    auto* aHomePage = new QAction(makeIcon(Icon::Home), "Startseite", this);
    aHomePage->setToolTip("Startseite");
    connect(aHomePage, &QAction::triggered, this, [this] { showHome(); });

    // Schnellzugriff-Werkzeugkasten oberhalb der Register
    auto* quick = new QWidget;
    quick->setObjectName("Schnellzugriff");
    quick->setStyleSheet("#Schnellzugriff { background: #dde3ea; border-bottom: 1px solid #c3cad3; }"
                         "QToolButton { border: none; padding: 1px; } QToolButton:hover { background: #c9d3de; }");
    auto* qh = new QHBoxLayout(quick);
    qh->setContentsMargins(6, 1, 6, 1);
    qh->setSpacing(1);
    auto* appLabel = new QLabel("<b>FormWerk</b>");
    appLabel->setStyleSheet("color: #26364d; padding-right: 8px;");
    qh->addWidget(appLabel);
    for (QAction* a : {aHomePage, aNew, aOpen, aSave, m_undoAction, m_redoAction}) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        b->setIconSize(QSize(18, 18));
        b->setToolTip(a->toolTip().isEmpty() ? a->text() : a->toolTip());
        qh->addWidget(b);
    }
    qh->addStretch();
    m_titleLabel = new QLabel;
    m_titleLabel->setStyleSheet("color: #3b4656;");
    qh->addWidget(m_titleLabel);
    qh->addStretch();

    auto* top = new QWidget;
    auto* tv = new QVBoxLayout(top);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(0);
    tv->addWidget(quick);
    tv->addWidget(m_ribbon);
    setMenuWidget(top);

    auto* corner = new QWidget;
    auto* h = new QHBoxLayout(corner);
    h->setContentsMargins(0, 0, 8, 0);
    h->addWidget(fileButton);
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
        if (item->data(0, Qt::UserRole + 1).toBool()) { cmdEditModifier(item->data(0, Qt::UserRole).toInt()); return; }
        if (auto f = featureOf(item)) cmdEditFeature(f);
    });
    connect(m_browser, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_browser->itemAt(pos);
        if (item && item->data(0, Qt::UserRole + 2).toInt() > 0) {
            const auto plane = static_cast<fw::SketchPlane>(item->data(0, Qt::UserRole + 2).toInt() - 1);
            QMenu menu;
            menu.addAction(makeIcon(Icon::Sketch), "Neue Skizze", this, [this, plane] {
                enterSketch(std::make_shared<fw::Sketch>(m_doc.nextName("Skizze"), plane), true);
            });
            menu.exec(m_browser->viewport()->mapToGlobal(pos));
            return;
        }
        if (item && item->data(0, Qt::UserRole + 1).toBool()) {
            const int idx = item->data(0, Qt::UserRole).toInt();
            QMenu menu;
            menu.addAction("Bearbeiten", this, [this, idx] { cmdEditModifier(idx); });
            menu.addAction(QString::fromUtf8("Löschen"), this, [this, idx] {
                m_doc.modifiers().erase(m_doc.modifiers().begin() + idx);
                rebuild();
            });
            menu.exec(m_browser->viewport()->mapToGlobal(pos));
            return;
        }
        auto f = featureOf(item);
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

    auto* dock = m_browserDock = new QDockWidget("Modell", this);
    dock->setObjectName("Modellbrowser");
    dock->setWidget(m_browser);
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // Eigenschaften-Palette (oberhalb des Browsers, nur waehrend eines Befehls)
    m_panel = new PropertyPanel;
    connect(m_panel, &PropertyPanel::accepted, this, [this] { endFeatureEdit(true); });
    connect(m_panel, &PropertyPanel::rejected, this, [this] { endFeatureEdit(false); });
    m_panelDock = new QDockWidget("Eigenschaften", this);
    m_panelDock->setObjectName("Eigenschaften");
    m_panelDock->setWidget(m_panel);
    m_panelDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, m_panelDock);
    splitDockWidget(m_panelDock, dock, Qt::Vertical);
    m_panelDock->hide();

    // Esc / Enter beenden den Befehl auch, wenn der Fokus im Grafikfenster liegt
    m_panelCancel = new QAction(this);
    m_panelCancel->setShortcut(Qt::Key_Escape);
    m_panelCancel->setEnabled(false);
    connect(m_panelCancel, &QAction::triggered, this, [this] {
        if (m_pickingPlane) endPlanePick();
        else if (m_view->navMode() != OcctView::NavMode::None) m_view->setNavMode(OcctView::NavMode::None, false);
        else endFeatureEdit(false);
    });
    addAction(m_panelCancel);
    m_panelOk = new QAction(this);
    m_panelOk->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    m_panelOk->setEnabled(false);
    connect(m_panelOk, &QAction::triggered, this, [this] { endFeatureEdit(true); });
    addAction(m_panelOk);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(30);
    connect(m_previewTimer, &QTimer::timeout, this, [this] { previewFeature(); });
}

void MainWindow::loadDemoPart()
{
    // Beispielteil: Platte mit Bohrung und aufgesetztem Zapfen
    endFeatureEdit(false);
    if (m_editSketch) finishSketch(false);
    exitPrintEnvironment();
    m_doc.clear("Beispiel");
    m_filePath.clear();
    m_slice = fw::SliceResult{};
    auto sk1 = std::make_shared<fw::Sketch>("Skizze1", fw::SketchPlane::XY);
    sk1->addRectangle(0, 0, 60, 40);
    sk1->addCircle(15, 20, 6);
    m_doc.addFeature(sk1);
    m_doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion1", sk1, 8.0));

    auto sk2 = std::make_shared<fw::Sketch>("Skizze2", fw::SketchPlane::XY, 8.0);
    sk2->addCircle(42, 20, 10);
    m_doc.addFeature(sk2);
    m_doc.addFeature(std::make_shared<fw::ExtrudeFeature>("Extrusion2", sk2, 15.0));

    resetHistory();
    m_docOpen = true;
    m_modified = false;
    showModel();
    QTimer::singleShot(150, this, [this] { rebuild(true); m_modified = false; updateTitle(); });
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

    refreshBrowser();
    if (m_view->context().IsNull()) return;
    displayModel();
    if (fit) m_view->fitAll();
    else m_view->redraw();

    if (!ok) {
        statusBar()->showMessage(QString::fromUtf8("Fehler in %1: %2")
            .arg(QString::fromStdString(m_doc.errorFeature()->name()), QString::fromStdString(m_doc.errorMessage())), 8000);
    }
}

void MainWindow::displayModel()
{
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
        m_bodyAis->Attributes()->SetFaceBoundaryDraw(m_visualStyle == 1);
        m_bodyAis->Attributes()->SetFaceBoundaryAspect(
            new Prs3d_LineAspect(Quantity_NOC_GRAY15, Aspect_TOL_SOLID, 1.0));
        ctx->Display(m_bodyAis, m_visualStyle == 2 ? AIS_WireFrame : AIS_Shaded, 0, false);
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

    displayModifiers(gp_Vec());
}

void MainWindow::refreshBrowser()
{
    m_browser->clear();
    m_itemFeature.clear();

    auto* root = new QTreeWidgetItem(m_browser, {QString::fromStdString(m_doc.name())});
    root->setIcon(0, makeIcon(Icon::Extrude));
    auto* origin = new QTreeWidgetItem(root, {"Ursprung"});
    for (const char* n : {"YZ-Ebene", "XZ-Ebene", "XY-Ebene", "X-Achse", "Y-Achse", "Z-Achse", "Mittelpunkt"}) {
        auto* it = new QTreeWidgetItem(origin, {n});
        const QString name(n);
        if (name == "XY-Ebene") it->setData(0, Qt::UserRole + 2, 1 + static_cast<int>(fw::SketchPlane::XY));
        if (name == "XZ-Ebene") it->setData(0, Qt::UserRole + 2, 1 + static_cast<int>(fw::SketchPlane::XZ));
        if (name == "YZ-Ebene") it->setData(0, Qt::UserRole + 2, 1 + static_cast<int>(fw::SketchPlane::YZ));
    }

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
    auto* endItem = new QTreeWidgetItem(root, {"Bauteilende"});
    endItem->setIcon(0, makeIcon(Icon::EndOfPart));
    if (!m_doc.modifiers().empty()) {
        auto* modRoot = new QTreeWidgetItem(root, {"Druck-Modifikatoren"});
        modRoot->setIcon(0, makeIcon(Icon::Slice));
        for (size_t i = 0; i < m_doc.modifiers().size(); ++i) {
            const auto& mod = m_doc.modifiers()[i];
            QString label = QString::fromStdString(mod.name) + " (" + QString::fromUtf8(fw::modifierRoleName(mod.role));
            if (mod.role == fw::PrintModifier::Role::InfillZone) label += QString(", %1 %").arg(qRound(mod.infillDensity * 100));
            label += ")";
            auto* it = new QTreeWidgetItem(modRoot, {label});
            it->setData(0, Qt::UserRole, static_cast<int>(i));
            it->setData(0, Qt::UserRole + 1, true);
            it->setIcon(0, modifierIcon(mod.role == fw::PrintModifier::Role::InfillZone ? QColor(60, 160, 90)
                                        : mod.role == fw::PrintModifier::Role::SupportEnforcer ? QColor(40, 170, 200)
                                                                                           : QColor(210, 70, 60)));
        }
        modRoot->setExpanded(true);
    }
    root->setExpanded(true);
}

void MainWindow::cmdNewSketch()
{
    if (m_editFeature || m_editSketch || m_pickingPlane) return;
    if (m_printPreview) setPrintPreview(false);
    const auto& ctx = m_view->context();
    if (ctx.IsNull()) {
        NewSketchDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return;
        enterSketch(std::make_shared<fw::Sketch>(m_doc.nextName("Skizze"), dlg.plane(), dlg.offset()), true);
        return;
    }

    // Wie in Inventor: Ursprungsebenen einblenden, Ebene oder ebene Flaeche anklicken
    m_pickingPlane = true;
    double half = 15.0;
    if (!m_doc.body().IsNull()) {
        Bnd_Box box;
        BRepBndLib::Add(m_doc.body(), box);
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        for (double v : {x0, y0, z0, x1, y1, z1}) half = std::max(half, std::abs(v) * 0.6);
    }
    const fw::SketchPlane types[3] = {fw::SketchPlane::XY, fw::SketchPlane::XZ, fw::SketchPlane::YZ};
    for (fw::SketchPlane t : types) {
        const gp_Pln pln(fw::Sketch("", t).plane());
        Handle(AIS_Shape) a = new AIS_Shape(BRepBuilderAPI_MakeFace(pln, -half, half, -half, half).Face());
        a->SetColor(Quantity_Color(0.96, 0.72, 0.30, Quantity_TOC_sRGB));
        a->SetTransparency(0.72);
        ctx->Display(a, AIS_Shaded, 0, false);
        m_planeAis.push_back(a);
    }
    if (!m_bodyAis.IsNull() && ctx->IsDisplayed(m_bodyAis)) {
        ctx->Deactivate(m_bodyAis);
        ctx->Activate(m_bodyAis, AIS_Shape::SelectionMode(TopAbs_FACE));
    }
    for (QAction* a : m_modelActions) a->setEnabled(false);
    m_browser->setEnabled(false);
    m_panelCancel->setEnabled(true);
    m_view->setDragHandler({[this](const Handle(AIS_InteractiveObject)& o) { return pickSketchPlane(o); }, {}, {}});
    statusBar()->showMessage(QString::fromUtf8("2D-Skizze: Ursprungsebene oder ebene Fläche des Bauteils anklicken · Esc bricht ab"));
    m_view->redraw();
}

bool MainWindow::pickSketchPlane(const Handle(AIS_InteractiveObject)& picked)
{
    fw::SketchPtr sketch;
    const std::string name = m_doc.nextName("Skizze");
    const fw::SketchPlane types[3] = {fw::SketchPlane::XY, fw::SketchPlane::XZ, fw::SketchPlane::YZ};
    for (size_t i = 0; i < m_planeAis.size(); ++i)
        if (picked == m_planeAis[i]) sketch = std::make_shared<fw::Sketch>(name, types[i]);

    const auto& ctx = m_view->context();
    if (!sketch && picked == m_bodyAis && ctx->HasDetectedShape()) {
        const TopoDS_Shape shape = ctx->DetectedShape();
        if (shape.ShapeType() != TopAbs_FACE) return true;
        const TopoDS_Face face = TopoDS::Face(shape);
        BRepAdaptor_Surface surf(face);
        if (surf.GetType() != GeomAbs_Plane) {
            statusBar()->showMessage(QString::fromUtf8("Skizzen gehen nur auf ebenen Flächen · Esc bricht ab"));
            return true;
        }
        const gp_Pln pln = surf.Plane();
        gp_Dir n = pln.Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) n.Reverse(); // nach aussen zeigen
        // Skizzen-X waagerecht legen, damit "oben" in der Skizze auch oben im Bauteil ist
        const gp_Dir x = std::abs(n.Z()) < 0.99 ? gp_Dir(gp::DZ().Crossed(n)) : gp::DX();
        const double d = gp_Vec(pln.Location(), gp::Origin()).Dot(gp_Vec(n));
        const gp_Pnt o = gp::Origin().Translated(gp_Vec(n) * -d);
        const double tol = 1e-9;
        if (n.IsEqual(gp::DZ(), tol) && x.IsEqual(gp::DX(), tol))
            sketch = std::make_shared<fw::Sketch>(name, fw::SketchPlane::XY, o.Z());
        else if (n.IsEqual(-gp::DY(), tol) && x.IsEqual(gp::DX(), tol))
            sketch = std::make_shared<fw::Sketch>(name, fw::SketchPlane::XZ, o.Y());
        else if (n.IsEqual(gp::DX(), tol) && x.IsEqual(gp::DY(), tol))
            sketch = std::make_shared<fw::Sketch>(name, fw::SketchPlane::YZ, o.X());
        else
            sketch = std::make_shared<fw::Sketch>(name, gp_Ax3(o, n, x));
    }
    if (!sketch) return false;
    // Erst nach dem Klick umschalten (der Griff-Handler darf sich nicht selbst loeschen)
    QTimer::singleShot(0, this, [this, sketch] {
        endPlanePick();
        enterSketch(sketch, true);
    });
    return true;
}

void MainWindow::endPlanePick()
{
    if (!m_pickingPlane) return;
    m_pickingPlane = false;
    const auto& ctx = m_view->context();
    for (auto& a : m_planeAis) ctx->Remove(a, false);
    m_planeAis.clear();
    if (!m_bodyAis.IsNull() && ctx->IsDisplayed(m_bodyAis)) {
        ctx->Deactivate(m_bodyAis);
        ctx->Activate(m_bodyAis, 0);
    }
    m_view->clearDragHandler();
    m_panelCancel->setEnabled(false);
    for (QAction* a : m_modelActions) a->setEnabled(true);
    m_browser->setEnabled(true);
    statusBar()->showMessage("Bereit");
    m_view->redraw();
}

std::vector<SketchCanvas::RefLine> MainWindow::referenceFor(const fw::Sketch& sk) const
{
    std::vector<SketchCanvas::RefLine> out;
    const TopoDS_Shape& body = m_doc.body();
    if (body.IsNull()) return out;
    const gp_Ax3 ax = sk.plane();
    const gp_Vec X(ax.XDirection()), Y(ax.YDirection()), N(ax.Direction());
    const gp_Pnt O = ax.Location();
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(body, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge& e = TopoDS::Edge(edges(i));
        if (BRep_Tool::Degenerated(e)) continue;
        BRepAdaptor_Curve c(e);
        GCPnts_TangentialDeflection disc(c, 0.15, 0.05);
        SketchCanvas::RefLine r;
        double maxDist = 0;
        for (int k = 1; k <= disc.NbPoints(); ++k) {
            const gp_Vec v(O, disc.Value(k));
            r.pts << QPointF(v.Dot(X), v.Dot(Y));
            maxDist = std::max(maxDist, std::abs(v.Dot(N)));
        }
        r.inPlane = maxDist < 1e-4;
        if (r.pts.size() > 1) out.push_back(std::move(r));
    }
    return out;
}

void MainWindow::enterSketch(const fw::SketchPtr& sketch, bool isNew)
{
    if (m_printPreview) setPrintPreview(false);
    m_editSketch = sketch;
    m_editSketchIsNew = isNew;
    for (QAction* a : m_modelActions) a->setEnabled(false);
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    setPageShown(m_sketchPage, true);
    const int si = m_ribbon->indexOf(m_sketchPage);
    for (int i = 0; i < m_ribbon->count(); ++i) m_ribbon->setTabEnabled(i, i == si);
    m_ribbon->setCurrentWidget(m_sketchPage);
    m_stack->setCurrentWidget(m_canvas);
    m_canvas->setReference(referenceFor(*sketch));
    m_canvas->begin(*sketch);
    m_canvas->setFocus();
    m_browser->setEnabled(false);
}

void MainWindow::finishSketch(bool accept)
{
    if (!m_editSketch) return;
    fw::SketchPtr sketch = m_editSketch;
    m_editSketch.reset();
    for (QAction* a : m_modelActions) a->setEnabled(true);
    for (int i = 0; i < m_ribbon->count(); ++i) m_ribbon->setTabEnabled(i, true);
    setPageShown(m_sketchPage, false);
    m_ribbon->setCurrentWidget(m_inPrintEnv ? m_printPage : m_modelTabPage);
    m_stack->setCurrentWidget(m_modelPage);
    m_browser->setEnabled(true);
    statusBar()->showMessage("Bereit");

    if (accept) {
        sketch->geometry() = m_canvas->geometry();
        if (m_editSketchIsNew && !sketch->isEmpty()) m_doc.addFeature(sketch);
    }
    rebuild();
}

fw::SketchPtr MainWindow::defaultProfileSketch() const
{
    // Wie Inventor: die zuletzt erstellte, noch nicht verbrauchte Skizze vorschlagen
    auto sk = sketches();
    for (auto it = sk.rbegin(); it != sk.rend(); ++it)
        if (!m_doc.isUsed(*it) && !(*it)->isEmpty()) return *it;
    return sk.empty() ? nullptr : sk.back();
}

void MainWindow::cmdExtrude()
{
    if (m_editFeature || m_pickingPlane) return;
    auto sk = defaultProfileSketch();
    if (!sk) {
        QMessageBox::information(this, "Extrusion", "Bitte zuerst eine 2D-Skizze erstellen.");
        return;
    }
    const std::string backup = m_doc.toText();
    auto f = std::make_shared<fw::ExtrudeFeature>(m_doc.nextName("Extrusion"), sk, 10.0,
                                                  m_doc.body().IsNull() ? fw::BooleanOp::Join : fw::BooleanOp::Join);
    m_doc.addFeature(f);
    beginFeatureEdit(f, true, backup);
}

void MainWindow::cmdRevolve()
{
    if (m_editFeature || m_pickingPlane) return;
    auto sk = defaultProfileSketch();
    if (!sk) {
        QMessageBox::information(this, "Drehung", "Bitte zuerst eine 2D-Skizze erstellen.");
        return;
    }
    const std::string backup = m_doc.toText();
    auto f = std::make_shared<fw::RevolveFeature>(m_doc.nextName("Drehung"), sk, fw::RevolveAxis::SketchY, 360.0);
    m_doc.addFeature(f);
    beginFeatureEdit(f, true, backup);
}

void MainWindow::cmdEditFeature(const fw::FeaturePtr& f)
{
    if (m_editFeature) return;
    if (auto s = std::dynamic_pointer_cast<fw::Sketch>(f)) {
        enterSketch(s, false);
    } else if (f->type() == fw::FeatureType::Extrude || f->type() == fw::FeatureType::Revolve) {
        beginFeatureEdit(f, false, m_doc.toText());
    }
}

namespace {
QIcon opIcon(fw::BooleanOp op)
{
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor a(120, 160, 215), dark(50, 60, 75);
    p.setPen(QPen(dark, 2));
    QPainterPath r1, r2;
    r1.addEllipse(QRectF(6, 10, 24, 24));
    r2.addEllipse(QRectF(18, 14, 24, 24));
    switch (op) {
    case fw::BooleanOp::Join: p.fillPath(r1.united(r2), a); p.drawPath(r1.united(r2)); break;
    case fw::BooleanOp::Cut: p.fillPath(r1.subtracted(r2), a); p.drawPath(r1); p.setPen(QPen(dark, 1, Qt::DashLine)); p.drawPath(r2); break;
    case fw::BooleanOp::Intersect: p.fillPath(r1.intersected(r2), a); p.setPen(QPen(dark, 1, Qt::DashLine)); p.drawPath(r1); p.drawPath(r2); break;
    }
    return QIcon(pm);
}

QIcon directionIcon(int kind) // 0 Standard, 1 umgekehrt, 2 symmetrisch
{
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor dark(50, 60, 75), orange(235, 140, 30);
    p.setPen(QPen(dark, 2));
    p.setBrush(QColor(150, 185, 230));
    const int top = kind == 0 ? 8 : kind == 1 ? 24 : 14;
    p.drawRect(12, top, 24, 18);
    p.setPen(QPen(orange, 3));
    p.drawLine(6, 24, 42, 24);
    return QIcon(pm);
}
} // namespace

void MainWindow::beginFeatureEdit(const fw::FeaturePtr& f, bool isNew, const std::string& backup)
{
    if (m_printPreview) setPrintPreview(false);
    m_editFeature = f;
    m_editIsNew = isNew;
    m_editBackup = backup;
    m_editDistance = nullptr;
    for (QAction* a : m_modelActions) a->setEnabled(false);
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    m_browser->setEnabled(false);

    auto sk = sketches();
    auto profileCombo = [&](const fw::SketchPtr& current) {
        auto* c = new QComboBox;
        for (const auto& s : sk) c->addItem(makeIcon(Icon::Sketch), QString::fromStdString(s->name()));
        for (size_t i = 0; i < sk.size(); ++i)
            if (sk[i] == current) c->setCurrentIndex(static_cast<int>(i));
        return c;
    };
    auto sketchAt = [this](int i) { auto s = sketches(); return i >= 0 && i < static_cast<int>(s.size()) ? s[static_cast<size_t>(i)] : nullptr; };

    if (auto e = std::dynamic_pointer_cast<fw::ExtrudeFeature>(f)) {
        m_panel->begin(makeIcon(Icon::Extrude), QString::fromUtf8("Extrusion  ›  %1").arg(QString::fromStdString(e->name())));
        auto* in = m_panel->addSection("Eingabegeometrie");
        auto* profile = profileCombo(e->sketch());
        in->addRow("Profil", profile);

        auto* beh = m_panel->addSection("Verhalten");
        auto* dirGroup = new QButtonGroup(m_panel);
        const int dir = e->symmetric() ? 2 : (e->distance() < 0 ? 1 : 0);
        beh->addRow("Richtung", PropertyPanel::choiceRow(dirGroup,
            {{directionIcon(0), "Standard"}, {directionIcon(1), "Umgekehrt"}, {directionIcon(2), "Symmetrisch"}}, dir));
        auto* dist = new QDoubleSpinBox;
        dist->setRange(0.01, 10000);
        dist->setDecimals(2);
        dist->setSuffix(" mm");
        dist->setValue(std::abs(e->distance()));
        dist->setKeyboardTracking(false);
        beh->addRow("Abstand A", dist);
        m_editDistance = dist;
        m_editDirGroup = dirGroup;

        auto* out = m_panel->addSection("Ausgabe");
        auto* opGroup = new QButtonGroup(m_panel);
        out->addRow("Boolesch", PropertyPanel::choiceRow(opGroup,
            {{opIcon(fw::BooleanOp::Join), "Verbinden"}, {opIcon(fw::BooleanOp::Cut), "Differenz"},
             {opIcon(fw::BooleanOp::Intersect), "Schnittmenge"}}, static_cast<int>(e->operation())));

        auto apply = [this, e, profile, dirGroup, dist, opGroup, sketchAt] {
            if (auto s = sketchAt(profile->currentIndex())) e->setSketch(s);
            const int d = dirGroup->checkedId();
            e->setDistance(d == 1 ? -dist->value() : dist->value());
            e->setSymmetric(d == 2);
            e->setOperation(static_cast<fw::BooleanOp>(opGroup->checkedId()));
            schedulePreview();
        };
        connect(profile, &QComboBox::currentIndexChanged, m_panel, apply);
        connect(dirGroup, &QButtonGroup::idClicked, m_panel, apply);
        connect(dist, &QDoubleSpinBox::valueChanged, m_panel, apply);
        connect(opGroup, &QButtonGroup::idClicked, m_panel, apply);
        m_panel->setMessage(QString::fromUtf8("Tipp: Pfeil im Grafikfenster ziehen, um den Abstand zu ändern."), false);
    } else if (auto r = std::dynamic_pointer_cast<fw::RevolveFeature>(f)) {
        m_panel->begin(makeIcon(Icon::Revolve), QString::fromUtf8("Drehung  ›  %1").arg(QString::fromStdString(r->name())));
        auto* in = m_panel->addSection("Eingabegeometrie");
        auto* profile = profileCombo(r->sketch());
        in->addRow("Profil", profile);
        auto* axis = new QComboBox;
        axis->addItems({"X-Achse der Skizze", "Y-Achse der Skizze"});
        axis->setCurrentIndex(r->axis() == fw::RevolveAxis::SketchX ? 0 : 1);
        in->addRow("Achse", axis);

        auto* beh = m_panel->addSection("Verhalten");
        auto* extentGroup = new QButtonGroup(m_panel);
        const bool full = r->angle() >= 359.999;
        beh->addRow(QString::fromUtf8("Ausdehnung"), PropertyPanel::choiceRow(extentGroup,
            {{textIcon("360"), "Voll"}, {textIcon(QString::fromUtf8("∠")), "Winkel"}}, full ? 0 : 1));
        auto* angle = new QDoubleSpinBox;
        angle->setRange(0.1, 360);
        angle->setSuffix(QString::fromUtf8(" °"));
        angle->setValue(full ? 180.0 : r->angle());
        angle->setEnabled(!full);
        angle->setKeyboardTracking(false);
        beh->addRow("Winkel A", angle);

        auto* out = m_panel->addSection("Ausgabe");
        auto* opGroup = new QButtonGroup(m_panel);
        out->addRow("Boolesch", PropertyPanel::choiceRow(opGroup,
            {{opIcon(fw::BooleanOp::Join), "Verbinden"}, {opIcon(fw::BooleanOp::Cut), "Differenz"},
             {opIcon(fw::BooleanOp::Intersect), "Schnittmenge"}}, static_cast<int>(r->operation())));

        auto apply = [this, r, profile, axis, extentGroup, angle, opGroup, sketchAt] {
            if (auto s = sketchAt(profile->currentIndex())) r->setSketch(s);
            r->setAxis(axis->currentIndex() == 0 ? fw::RevolveAxis::SketchX : fw::RevolveAxis::SketchY);
            const bool full = extentGroup->checkedId() == 0;
            angle->setEnabled(!full);
            r->setAngle(full ? 360.0 : angle->value());
            r->setOperation(static_cast<fw::BooleanOp>(opGroup->checkedId()));
            schedulePreview();
        };
        connect(profile, &QComboBox::currentIndexChanged, m_panel, apply);
        connect(axis, &QComboBox::currentIndexChanged, m_panel, apply);
        connect(extentGroup, &QButtonGroup::idClicked, m_panel, apply);
        connect(angle, &QDoubleSpinBox::valueChanged, m_panel, apply);
        connect(opGroup, &QButtonGroup::idClicked, m_panel, apply);
    }
    m_panel->finish();
    m_panelDock->show();
    m_panelDock->raise();
    m_panelCancel->setEnabled(true);
    m_panelOk->setEnabled(true);
    if (m_editDistance) {
        m_editDistance->setFocus();
        m_editDistance->selectAll();
    }

    // Ziehpfeil fuer den Extrusionsabstand
    m_view->setDragHandler({
        [this](const Handle(AIS_InteractiveObject)& o) {
            if (m_arrowAis.IsNull() || o != m_arrowAis || !m_editDistance) return false;
            auto e = std::dynamic_pointer_cast<fw::ExtrudeFeature>(m_editFeature);
            m_dragStartValue = e ? e->distance() : 0.0;
            return true;
        },
        [this](const QPointF& start, const QPointF& now) { dragArrow(start, now); },
        [this] { previewFeature(); },
    });
    previewFeature();
}

void MainWindow::schedulePreview()
{
    m_previewTimer->start();
}

void MainWindow::previewFeature()
{
    if (!m_editFeature) return;
    const bool ok = m_doc.recompute();
    if (!m_view->context().IsNull()) displayModel();
    if (!ok) {
        m_panel->setMessage(QString::fromUtf8("Fehler: %1").arg(QString::fromStdString(m_doc.errorMessage())), true);
    } else if (m_editDistance) {
        m_panel->setMessage(QString::fromUtf8("Tipp: Pfeil im Grafikfenster ziehen, um den Abstand zu ändern."), false);
    } else {
        m_panel->setMessage({}, false);
    }
    updateArrow();
    m_view->redraw();
}

void MainWindow::updateArrow()
{
    const auto& ctx = m_view->context();
    if (ctx.IsNull()) return;
    if (!m_arrowAis.IsNull()) ctx->Remove(m_arrowAis, false);
    m_arrowAis.Nullify();
    auto e = std::dynamic_pointer_cast<fw::ExtrudeFeature>(m_editFeature);
    if (!e || !e->sketch()) return;

    TopoDS_Shape profile;
    try { profile = e->sketch()->profile(); } catch (...) { return; }
    if (profile.IsNull()) return;
    GProp_GProps props;
    BRepGProp::SurfaceProperties(profile, props);
    m_arrowDir = e->sketch()->plane().Direction();
    const double d = e->symmetric() ? e->distance() / 2.0 : e->distance();
    m_arrowBase = props.CentreOfMass();
    m_arrowSymmetric = e->symmetric();

    Bnd_Box box;
    BRepBndLib::Add(m_doc.body().IsNull() ? profile : m_doc.body(), box);
    const double diag = box.IsVoid() ? 50.0 : std::sqrt(box.SquareExtent());
    const double len = std::max(6.0, diag * 0.12), rad = len * 0.05;
    const gp_Dir out = d < 0 ? m_arrowDir.Reversed() : m_arrowDir;
    const gp_Pnt tip = m_arrowBase.Translated(gp_Vec(m_arrowDir) * d);
    TopoDS_Shape shaft = BRepPrimAPI_MakeCylinder(gp_Ax2(tip, out), rad, len * 0.7).Shape();
    TopoDS_Shape head = BRepPrimAPI_MakeCone(gp_Ax2(tip.Translated(gp_Vec(out) * (len * 0.7)), out), rad * 3, 0, len * 0.3).Shape();
    TopoDS_Compound c;
    BRep_Builder bb;
    bb.MakeCompound(c);
    bb.Add(c, shaft);
    bb.Add(c, head);
    m_arrowAis = new AIS_Shape(c);
    m_arrowAis->SetColor(Quantity_Color(0.95, 0.55, 0.10, Quantity_TOC_sRGB));
    m_arrowAis->SetZLayer(Graphic3d_ZLayerId_Topmost);
    ctx->Display(m_arrowAis, AIS_Shaded, 0, false);
}

void MainWindow::dragArrow(const QPointF& start, const QPointF& now)
{
    auto e = std::dynamic_pointer_cast<fw::ExtrudeFeature>(m_editFeature);
    if (!e || !m_editDistance) return;
    // Bildschirmrichtung der Extrusionsnormalen: Pixel pro Millimeter
    const QPointF p0 = m_view->project(m_arrowBase);
    const QPointF p1 = m_view->project(m_arrowBase.Translated(gp_Vec(m_arrowDir)));
    const QPointF s = p1 - p0;
    const double len2 = s.x() * s.x() + s.y() * s.y();
    if (len2 < 1e-9) return;
    const QPointF delta = now - start;
    double mm = (delta.x() * s.x() + delta.y() * s.y()) / len2;
    if (m_arrowSymmetric) mm *= 2.0;
    double value = m_dragStartValue + mm;
    value = std::round(value * 2.0) / 2.0; // auf 0,5 mm rasten
    if (std::abs(value) < 0.5) value = m_dragStartValue < 0 ? -0.5 : 0.5;
    // Richtung umschalten, wenn der Pfeil durch die Skizzenebene gezogen wird
    if (!m_arrowSymmetric && m_editDirGroup) {
        const int want = value < 0 ? 1 : 0;
        if (m_editDirGroup->checkedId() != want) m_editDirGroup->button(want)->setChecked(true);
    }
    m_editDistance->setValue(std::abs(value)); // loest apply + Vorschau aus
}

void MainWindow::endFeatureEdit(bool accept)
{
    if (!m_editFeature) return;
    m_previewTimer->stop();
    if (accept) {
        // Erst Vorschau aktualisieren, dann pruefen
        const bool ok = m_doc.recompute();
        if (!ok && m_doc.errorFeature() == m_editFeature) {
            m_panel->setMessage(QString::fromUtf8("Fehler: %1").arg(QString::fromStdString(m_doc.errorMessage())), true);
            return;
        }
    }
    m_view->clearDragHandler();
    if (!m_arrowAis.IsNull() && !m_view->context().IsNull()) m_view->context()->Remove(m_arrowAis, false);
    m_arrowAis.Nullify();
    m_editFeature.reset();
    m_editDistance = nullptr;
    m_editDirGroup = nullptr;
    m_panelDock->hide();
    m_panelCancel->setEnabled(false);
    m_panelOk->setEnabled(false);
    for (QAction* a : m_modelActions) a->setEnabled(true);
    m_browser->setEnabled(true);
    if (!accept) m_doc.fromText(m_editBackup);
    m_editBackup.clear();
    rebuild();
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
    const std::vector<fw::PrintModifier> modifiers = m_doc.modifiers();
    auto* progress = &m_sliceProgress;
    auto* cancel = &m_cancelSlice;
    m_sliceWatcher->setFuture(QtConcurrent::run([body, profile, modifiers, progress, cancel] {
        return fw::slice(body, profile, [progress, cancel](double f) {
            progress->store(f);
            return !cancel->load();
        }, modifiers);
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
    enterPrintEnvironment();
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
        displayModifiers(fw::bedPlacement(m_doc.body(), m_profile));
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
        displayModifiers(gp_Vec());
        if (!m_bodyAis.IsNull()) ctx->Display(m_bodyAis, m_visualStyle == 2 ? AIS_WireFrame : AIS_Shaded, 0, false);
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
    if (!m_docOpen) {
        setWindowTitle("FormWerk");
        if (m_titleLabel) m_titleLabel->setText(QString());
    } else {
        setWindowTitle(QString("%1%2 – FormWerk").arg(name, m_modified ? "*" : ""));
        if (m_titleLabel) m_titleLabel->setText(QString("%1%2").arg(name, m_modified ? "*" : ""));
    }
    if (m_docTabs && m_docTabs->count() > 1)
        m_docTabs->setTabText(1, QString::fromStdString(m_doc.name()) + (m_modified ? "*" : ""));
    if (m_undoAction) m_undoAction->setEnabled(!m_undo.empty() && !m_editSketch);
    if (m_redoAction) m_redoAction->setEnabled(!m_redo.empty() && !m_editSketch);
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
    if (!m_docOpen || !m_modified) return true;
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
    endFeatureEdit(false);
    if (m_editSketch) finishSketch(false);
    exitPrintEnvironment();
    m_doc.clear("Bauteil1");
    m_filePath.clear();
    m_slice = fw::SliceResult{};
    resetHistory();
    m_docOpen = true;
    m_modified = false;
    showModel();
    QTimer::singleShot(0, this, [this] { rebuild(true); m_modified = false; updateTitle(); });
}

void MainWindow::cmdCloseDoc()
{
    if (!m_docOpen || !maybeSave()) return;
    if (m_editSketch) finishSketch(false);
    exitPrintEnvironment();
    m_doc.clear("Bauteil1");
    m_filePath.clear();
    m_slice = fw::SliceResult{};
    resetHistory();
    m_docOpen = false;
    m_modified = false;
    rebuild();
    m_modified = false;
    updateTitle();
    showHome();
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
    endFeatureEdit(false);
    if (m_editSketch) finishSketch(false);
    exitPrintEnvironment();
    m_doc.fromText(tmp.toText());
    m_filePath = path;
    m_slice = fw::SliceResult{};
    resetHistory();
    m_docOpen = true;
    showModel();
    QTimer::singleShot(0, this, [this] { rebuild(true); });
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
    if (m_recentList) refreshHomePage();
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

// --- Druck-Modifikatoren ------------------------------------------------------------

void MainWindow::displayModifiers(const gp_Vec& shift)
{
    const auto& ctx = m_view->context();
    if (ctx.IsNull()) return;
    for (auto& a : m_modifierAis) ctx->Remove(a, false);
    m_modifierAis.clear();
    for (const auto& m : m_doc.modifiers()) {
        TopoDS_Shape s;
        const gp_Pnt c(m.cx + shift.X(), m.cy + shift.Y(), m.cz + shift.Z());
        if (m.shape == fw::PrintModifier::Shape::Box)
            s = BRepPrimAPI_MakeBox(gp_Pnt(c.X() - m.sx / 2, c.Y() - m.sy / 2, c.Z() - m.sz / 2), m.sx, m.sy, m.sz).Shape();
        else
            s = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(c.X(), c.Y(), c.Z() - m.sz / 2), gp::DZ()), m.sx / 2, m.sz).Shape();
        Handle(AIS_Shape) a = new AIS_Shape(s);
        const Quantity_Color col = m.role == fw::PrintModifier::Role::InfillZone ? Quantity_Color(0.25, 0.65, 0.35, Quantity_TOC_sRGB)
            : m.role == fw::PrintModifier::Role::SupportEnforcer ? Quantity_Color(0.15, 0.65, 0.80, Quantity_TOC_sRGB)
                                                                  : Quantity_Color(0.85, 0.30, 0.25, Quantity_TOC_sRGB);
        a->SetColor(col);
        a->SetTransparency(0.75);
        ctx->Display(a, AIS_Shaded, -1, false);
        m_modifierAis.push_back(a);
    }
    m_view->redraw();
}

void MainWindow::cmdAddModifier(fw::PrintModifier::Role role)
{
    if (m_doc.body().IsNull()) {
        QMessageBox::information(this, "Modifikator", "Bitte zuerst einen Körper modellieren.");
        return;
    }
    Bnd_Box box;
    BRepBndLib::Add(m_doc.body(), box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    fw::PrintModifier m;
    m.role = role;
    for (int i = 1;; ++i) {
        const std::string n = "Modifikator" + std::to_string(i);
        if (std::none_of(m_doc.modifiers().begin(), m_doc.modifiers().end(), [&](const fw::PrintModifier& o) { return o.name == n; })) {
            m.name = n;
            break;
        }
    }
    m.cx = (x0 + x1) / 2; m.cy = (y0 + y1) / 2; m.cz = (z0 + z1) / 2;
    m.sx = std::max(1.0, (x1 - x0) / 3); m.sy = std::max(1.0, (y1 - y0) / 3); m.sz = std::max(1.0, z1 - z0 + 2);
    ModifierDialog dlg(m, this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_doc.modifiers().push_back(dlg.modifier());
    rebuild();
}

void MainWindow::cmdEditModifier(int index)
{
    if (index < 0 || index >= static_cast<int>(m_doc.modifiers().size())) return;
    ModifierDialog dlg(m_doc.modifiers()[static_cast<size_t>(index)], this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_doc.modifiers()[static_cast<size_t>(index)] = dlg.modifier();
    rebuild();
}
