#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <QElapsedTimer>
#include <QWidget>

#include <vector>

// 3D-Ansicht auf Basis von OpenCascade in einem nativen Qt-Fenster.
// Maussteuerung wie in Inventor:
//   Mausrad drehen            -> Zoom (um Mauszeiger)
//   Mittlere Taste ziehen     -> Verschieben
//   Umschalt + Mittlere       -> Orbit
//   Doppelklick Mitte         -> Alles zoomen
//   F2 / F3 / F4 halten + linke Taste -> Verschieben / Zoom / Orbit
//   F5                        -> vorherige Ansicht
//   Rechte Taste              -> Kontextmenue (Signal contextMenuAt)
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

    enum class NavMode { None, Pan, Zoom, Orbit };
    // sticky = true: bleibt aktiv bis Esc/Rechtsklick (wie die Werkzeuge der Navigationsleiste)
    void setNavMode(NavMode mode, bool sticky);
    NavMode navMode() const { return m_navMode; }
    void previousView();
    bool hasPreviousView() const;
    void setPerspective(bool on);

signals:
    void hint(const QString& text);
    void contextMenuAt(const QPoint& globalPos);

public:

    QPaintEngine* paintEngine() const override { return nullptr; }

protected:
    void showEvent(QShowEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void enterEvent(QEnterEvent*) override;

    void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                          const Handle(V3d_View)& view) override;

private:
    void initViewer();
    Aspect_VKeyMouse mapButtons(Qt::MouseButtons b) const;
    Aspect_VKeyFlags mapFlags(Qt::MouseButtons b, Qt::KeyboardModifiers m) const;
    Handle(Graphic3d_Camera) snapshot() const;
    void remember(const Handle(Graphic3d_Camera)& cam);
    void rememberCurrent();

    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(AIS_InteractiveContext) m_ctx;
    Handle(AIS_ViewCube) m_cube;
    QSize m_viewSize;
    bool m_fitPending = false;
    NavMode m_navMode = NavMode::None;
    bool m_navSticky = false;
    Qt::MouseButtons m_lastButtons = Qt::NoButton;
    Handle(Graphic3d_Camera) m_pressCamera;
    std::vector<Handle(Graphic3d_Camera)> m_history;
    QElapsedTimer m_wheelTimer;
};
