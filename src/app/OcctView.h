#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <QWidget>

// 3D-Ansicht auf Basis von OpenCascade in einem nativen Qt-Fenster.
// Maussteuerung angelehnt an gaengige CAD-Programme:
//   Mausrad drehen          -> Zoom (um Mauszeiger)
//   Mittlere Taste ziehen   -> Verschieben
//   Umschalt + Mittlere     -> Orbit
//   Rechte Taste ziehen     -> Orbit (Alternative)
//   F6                      -> Ausgangsansicht
class OcctView : public QWidget, protected AIS_ViewController {
    Q_OBJECT
public:
    explicit OcctView(QWidget* parent = nullptr);
    ~OcctView() override;

    const Handle(AIS_InteractiveContext)& context() const { return m_ctx; }
    const Handle(V3d_View)& view() const { return m_view; }

    void fitAll();
    void homeView();
    void setStandardView(V3d_TypeOfOrientation o);
    void redraw();

    QPaintEngine* paintEngine() const override { return nullptr; }

protected:
    void showEvent(QShowEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

    void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                          const Handle(V3d_View)& view) override;

private:
    void initViewer();

    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(AIS_InteractiveContext) m_ctx;
    Handle(AIS_ViewCube) m_cube;
    QSize m_viewSize;
};
