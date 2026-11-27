#pragma once

#include "core/PartDocument.h"
#include "slicer/Slicer.h"

#include <AIS_Shape.hxx>

#include <QMainWindow>

#include <map>

class OcctView;
class QTreeWidget;
class QTreeWidgetItem;
class QSlider;
class QLabel;
class QTabWidget;
class QAction;
class ToolpathPreview;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    // Aufbau
    void buildRibbon();
    void buildBrowser();
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
    void setPrintPreview(bool on);
    void updatePreview();

    fw::PartDocument m_doc;
    fw::PrinterProfile m_profile;
    fw::SliceResult m_slice;

    OcctView* m_view = nullptr;
    QTabWidget* m_ribbon = nullptr;
    QTreeWidget* m_browser = nullptr;
    QSlider* m_layerSlider = nullptr;
    QLabel* m_layerLabel = nullptr;
    QAction* m_previewAction = nullptr;

    Handle(AIS_Shape) m_bodyAis;
    std::vector<Handle(AIS_Shape)> m_sketchAis;
    Handle(AIS_Shape) m_bedAis;
    Handle(ToolpathPreview) m_previewAis;
    bool m_printPreview = false;
    std::map<QTreeWidgetItem*, fw::FeaturePtr> m_itemFeature;
};
