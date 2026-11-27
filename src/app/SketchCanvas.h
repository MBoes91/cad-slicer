#pragma once

#include "core/Sketch.h"

#include <QPolygonF>
#include <QWidget>

#include <optional>
#include <vector>

// 2D-Skizzierumgebung: Zeichnen, Bemassen, Abhaengigkeiten setzen, Ziehen.
// Bearbeitet eine Kopie der Skizzengeometrie; MainWindow uebernimmt sie beim Fertigstellen.
class SketchCanvas : public QWidget {
    Q_OBJECT
public:
    enum class Tool {
        Select, Line, Rectangle, Circle, Arc, Dimension,
        Coincident, Horizontal, Vertical, Parallel, Perpendicular, Tangent, Equal, Fix, Midpoint
    };

    explicit SketchCanvas(QWidget* parent = nullptr);

    // Kanten des Bauteils, auf die Skizzenebene projiziert (grau im Hintergrund).
    // inPlane: Kante liegt in der Skizzenebene (dunkler dargestellt).
    struct RefLine { QPolygonF pts; bool inPlane = false; };
    void setReference(std::vector<RefLine> lines) { m_reference = std::move(lines); }

    void begin(const fw::Sketch& sketch);
    const fw::SketchGeometry& geometry() const { return m_geo; }
    void setTool(Tool t);
    Tool tool() const { return m_tool; }
    void toggleConstruction();
    void deleteSelection();
    void fit();
    void setShowConstraints(bool on) { m_showConstraints = on; update(); }

signals:
    void statusChanged(const QString& text);
    void toolChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    std::vector<RefLine> m_reference;
    struct Hit {
        enum Kind { None, Point, Entity, Constraint } kind = None;
        int index = -1;
    };

    QPointF toScreen(double x, double y) const;
    QPointF toSketch(const QPointF& s) const;
    Hit hitTest(const QPointF& screen, bool pointsFirst = true) const;
    int pointAt(const QPointF& screen) const;
    // Punkt unter dem Mauszeiger wiederverwenden oder neuen anlegen
    int pickOrCreatePoint(const QPointF& screen);
    void addAutoHV(int line);
    bool tryAdd(fw::SkConstraint c, bool allowDriven = false);
    void resolve();
    void cancelOperation();
    void handleConstraintTool(const Hit& h);
    void handleDimensionTool(const Hit& h, const QPointF& screen);
    QPointF constraintAnchor(const fw::SkConstraint& c) const;
    QRectF dimensionTextRect(int index, QPainter* p = nullptr) const;
    QString dimensionText(const fw::SkConstraint& c) const;
    void editDimension(int index);
    void updateStatus();

    fw::SketchGeometry m_geo;
    Tool m_tool = Tool::Select;
    double m_scale = 4.0;     // Pixel je mm
    QPointF m_center{0, 0};   // Skizzenkoordinate in der Fenstermitte
    bool m_showConstraints = true;

    // Laufende Eingabe
    std::vector<int> m_pending;   // bereits gewaehlte Punkte (Zeichenwerkzeuge)
    std::vector<Hit> m_picked;    // bereits gewaehlte Objekte (Abhaengigkeiten/Bemassung)
    QPointF m_mouse;
    Hit m_hover;
    Hit m_selected;
    int m_dragPoint = -1;
    int m_dragLabel = -1;
    bool m_panning = false;
    QPointF m_panStart;
    QString m_message;
    fw::SolveResult m_lastSolve;
};
