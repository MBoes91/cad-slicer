#pragma once

#include "core/Sketch.h"

#include <QPolygonF>
#include <QWidget>

#include <array>
#include <functional>
#include <optional>
#include <vector>

// 2D-Skizzierumgebung: Zeichnen, Bemassen, Abhaengigkeiten setzen, Ziehen.
// Bearbeitet eine Kopie der Skizzengeometrie; MainWindow uebernimmt sie beim Fertigstellen.
class SketchCanvas : public QWidget {
    Q_OBJECT
public:
    enum class Tool {
        Select, Line, Rectangle, Circle, Arc, Point, Polygon, Slot, SketchFillet, Dimension,
        Coincident, Horizontal, Vertical, Parallel, Perpendicular, Tangent, Equal, Fix, Midpoint
    };

    explicit SketchCanvas(QWidget* parent = nullptr);

    // Kanten des Bauteils, auf die Skizzenebene projiziert (grau im Hintergrund).
    // inPlane: Kante liegt in der Skizzenebene (dunkler dargestellt).
    struct RefLine { QPolygonF pts; bool inPlane = false; };
    void setReference(std::vector<RefLine> lines) { m_reference = std::move(lines); }

    // Parameter des Bauteils fuer Gleichungen; neue Bemassungen heissen d<nextIndex>, d<nextIndex+1> ...
    using Lookup = std::function<bool(const std::string& name, double& value)>;
    void setParameters(Lookup docLookup, int nextIndex) { m_docLookup = std::move(docLookup); m_nextParam = nextIndex; }

    void begin(const fw::Sketch& sketch);
    const fw::SketchGeometry& geometry() const { return m_geo; }
    void setTool(Tool t);
    Tool tool() const { return m_tool; }
    void toggleConstruction();
    void deleteSelection();
    void fit();
    void setShowConstraints(bool on) { m_showConstraints = on; update(); }
    void setPolygonSides(int n) { m_polySides = std::max(3, n); }
    int polygonSides() const { return m_polySides; }
    // Erstellungsarten wie in Inventor
    // Rechteck: 0 zwei Punkte, 1 drei Punkte, 2 zwei Punkte Mittelpunkt, 3 drei Punkte Mittelpunkt
    // Kreis: 0 Mittelpunkt, 1 tangential (drei Linien)
    // Bogen: 0 drei Punkte, 1 Mittelpunkt, 2 tangential
    // Polygon: 0 einbeschrieben (Ecken auf dem Kreis), 1 umschrieben (Seiten beruehren den Kreis)
    void setVariant(Tool t, int v);
    int variant(Tool t) const;

signals:
    void statusChanged(const QString& text);
    void toolChanged();
    void contextMenuAt(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool event(QEvent* e) override;

private:
    // Fragt Wert oder Gleichung ab; false bei Abbruch
    // at: Bildschirmposition fuer das kleine Eingabefeld direkt an der Masszahl (wie in Inventor)
    bool askValue(const QString& title, const QString& label, const QString& initial, double& value, std::string& expr,
                  const QPointF* at = nullptr);
    bool evaluate(const QString& text, double& value, QString& error) const;
    void nameDimension(fw::SkConstraint& c);
    Lookup m_docLookup;
    int m_nextParam = 0;
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
    // Bemassung aus der bisherigen Auswahl, Masszahl an der Sketch-Position sp
    bool dimensionPreview(const QPointF& sp, fw::SkConstraint& out) const;
    QPointF constraintAnchor(const fw::SkConstraint& c) const;
    QRectF dimensionTextRect(int index, QPainter* p = nullptr) const;
    QString dimensionText(const fw::SkConstraint& c) const;
    void editDimension(int index);
    void updateStatus();

    // Zeichnen (Maus und dynamische Eingabe teilen sich diese Schritte)
    int lineTo(int p, bool closeChain);     // liefert neue Linie oder -1
    std::array<int, 4> rectTo(int p);       // liefert die vier Linien oder -1
    int circleTo(double r);                 // liefert den Kreis oder -1
    void polygonTo(const QPointF& sketchCorner);
    // Viereck aus vier Ecken (Sketch-Koordinaten); a/b: vorhandene Eckpunkte oder -1; center: Mittelpunkt oder -1
    void addQuad(const std::array<QPointF, 4>& c, int a, int b, int center, bool axisAligned);
    bool rectVariantCorners(const QPointF& m, std::array<QPointF, 4>& out) const;
    void tangentCircle();
    bool arcPreview(const QPointF& m, QPointF& center, double& r, double& a0, double& sweep) const;
    void slotTo(const QPointF& sketchSide);
    bool filletCorner(int point, double r, const std::string& expr);
    int m_polySides = 6;
    int m_rectMode = 0, m_circleMode = 0, m_arcMode = 1, m_polyMode = 0;
    QPointF m_rectDir;        // drei Punkte Mittelpunkt: Seitenmitte
    bool m_rectDirSet = false;

    // Dynamische Eingabe: Wertefelder am Mauszeiger
    struct DynField {
        QString label;
        QString text;        // getippter Text (leer = Wert folgt der Maus)
        double live = 0;     // aktueller Wert aus der Mausposition
    };
    void updateDynFields();
    bool dynValue(int i, double& v, std::string* expr = nullptr) const;
    QPointF dynTarget() const;              // Skizzenpunkt aus Maus + getippten Werten
    void commitDynamic();
    bool dynKey(QKeyEvent* e);
    std::vector<DynField> m_dyn;
    int m_dynActive = 0;
    bool m_dynEngaged = false; // Eingabe laeuft: Buchstaben gehen in die Felder (Parameternamen)

    fw::SketchGeometry m_geo;
    Tool m_tool = Tool::Select;
    double m_scale = 4.0;     // Pixel je mm
    QPointF m_center{0, 0};   // Skizzenkoordinate in der Fenstermitte
    bool m_showConstraints = true;

    // Laufende Eingabe
    std::vector<int> m_pending;   // bereits gewaehlte Punkte (Zeichenwerkzeuge)
    bool m_placingDim = false;      // Bemassung wird gerade eingetragen (Wertabfrage offen)
    fw::SkConstraint m_placedDim;
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
