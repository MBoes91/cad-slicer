#pragma once

#include "core/PartDocument.h"
#include "slicer/Slicer.h"

#include <AIS_Shape.hxx>

#include <QFutureWatcher>
#include <QMainWindow>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <map>

class OcctView;
class QTreeWidget;
class QTreeWidgetItem;
class QSlider;
class QLabel;
class QTabWidget;
class QAction;
class ToolpathPreview;
class QProgressBar;
class QPushButton;
class QMenu;
class QTimer;
class QStackedWidget;
class SketchCanvas;
class QTabBar;
class QToolBar;
class QListWidget;
class QDockWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    // Aufbau
    void buildRibbon();
    void buildBrowser();
    void buildFileMenu();
    void loadDemoPart();
    QWidget* buildHomePage();
    void refreshHomePage();
    void showHome();
    void showModel();
    void syncDocTabs(bool home);
    void setPageShown(QWidget* page, bool on);
    void enterPrintEnvironment();
    void exitPrintEnvironment();
    void setVisualStyle(int style);
    void applyBodyStyle();
    void showViewContextMenu(const QPoint& globalPos);
    void cmdMeasure();
    void cmdCloseDoc();

    // Modell
    void rebuild(bool fit = false);
    void refreshBrowser();
    std::vector<fw::SketchPtr> sketches() const;
    fw::FeaturePtr featureOf(QTreeWidgetItem* item) const;

    // Befehle
    void cmdNewSketch();
    void enterSketch(const fw::SketchPtr& sketch, bool isNew);
    void finishSketch(bool accept);
    void cmdExtrude();
    void cmdRevolve();
    void cmdEditFeature(const fw::FeaturePtr& f);
    void cmdDeleteFeature(const fw::FeaturePtr& f);
    void cmdToggleSuppress(const fw::FeaturePtr& f);
    void cmdExportStl();
    void cmdSlice();
    void cmdSaveGcode();
    void cmdPrintSettings();
    void cmdAddEventAtLayer();
    void cmdAddModifier(fw::PrintModifier::Role role);
    void cmdEditModifier(int index);
    void displayModifiers(const gp_Vec& shift);
    void setPrintPreview(bool on);
    void startSlice(std::function<void()> then);
    void sliceFinished();

    // Datei und Verlauf
    void cmdNew();
    void cmdOpen();
    bool openFile(const QString& path);
    bool cmdSave();
    bool cmdSaveAs();
    bool writeFile(const QString& path);
    bool maybeSave();
    void addRecent(const QString& path);
    void updateRecentMenu();
    void updateTitle();
    void resetHistory();
    void cmdUndo();
    void cmdRedo();
    void restoreText(const std::string& text);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void updatePreview();

    fw::PartDocument m_doc;
    fw::PrinterProfile m_profile;
    fw::SliceResult m_slice;

    OcctView* m_view = nullptr;
    SketchCanvas* m_canvas = nullptr;
    QStackedWidget* m_stack = nullptr;
    fw::SketchPtr m_editSketch;
    bool m_editSketchIsNew = false;
    std::vector<QAction*> m_modelActions;
    std::vector<QAction*> m_sketchToolActions;
    std::vector<QAction*> m_docActions; // nur mit offenem Dokument
    QWidget* m_modelPage = nullptr;
    QWidget* m_homePage = nullptr;
    QListWidget* m_recentList = nullptr;
    QTabBar* m_docTabs = nullptr;
    bool m_syncingTabs = false;
    QToolBar* m_navBar = nullptr;
    QDockWidget* m_browserDock = nullptr;
    QWidget* m_sketchPage = nullptr;
    QWidget* m_printPage = nullptr;
    QWidget* m_modelTabPage = nullptr;
    QLabel* m_titleLabel = nullptr;
    bool m_docOpen = false;
    bool m_inPrintEnv = false;
    int m_visualStyle = 1; // 0 schattiert, 1 mit Kanten, 2 Drahtkoerper
    QAction* m_styleShaded = nullptr;
    QAction* m_styleEdges = nullptr;
    QAction* m_styleWire = nullptr;
    QTabWidget* m_ribbon = nullptr;
    QTreeWidget* m_browser = nullptr;
    QSlider* m_layerSlider = nullptr;
    QLabel* m_layerLabel = nullptr;
    QLabel* m_profileLabel = nullptr;
    QAction* m_previewAction = nullptr;

    Handle(AIS_Shape) m_bodyAis;
    std::vector<Handle(AIS_Shape)> m_sketchAis;
    Handle(AIS_Shape) m_bedAis;
    std::vector<Handle(AIS_Shape)> m_modifierAis;
    Handle(ToolpathPreview) m_previewAis;
    bool m_printPreview = false;
    std::map<QTreeWidgetItem*, fw::FeaturePtr> m_itemFeature;

    // Datei / Verlauf
    QString m_filePath;
    bool m_modified = false;
    bool m_restoring = false;
    std::string m_lastText;
    std::vector<std::string> m_undo, m_redo;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QMenu* m_recentMenu = nullptr;
    unsigned m_generation = 0;

    // Slicen im Hintergrund
    QFutureWatcher<fw::SliceResult>* m_sliceWatcher = nullptr;
    std::atomic<double> m_sliceProgress{0.0};
    std::atomic<bool> m_cancelSlice{false};
    unsigned m_sliceGeneration = 0;
    std::function<void()> m_afterSlice;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QTimer* m_progressTimer = nullptr;
};
