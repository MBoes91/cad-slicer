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

    // Modell
    void rebuild(bool fit = false);
    void refreshBrowser();
    std::vector<fw::SketchPtr> sketches() const;
    fw::FeaturePtr featureOf(QTreeWidgetItem* item) const;

    // Befehle
    void cmdNewSketch();
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
    QTabWidget* m_ribbon = nullptr;
    QTreeWidget* m_browser = nullptr;
    QSlider* m_layerSlider = nullptr;
    QLabel* m_layerLabel = nullptr;
    QLabel* m_profileLabel = nullptr;
    QAction* m_previewAction = nullptr;

    Handle(AIS_Shape) m_bodyAis;
    std::vector<Handle(AIS_Shape)> m_sketchAis;
    Handle(AIS_Shape) m_bedAis;
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
