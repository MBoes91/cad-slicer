#pragma once

#include <QPointer>
#include <QWidget>

#include <array>
#include <vector>

class QAction;

// Radiales Markierungsmenue wie in Inventor: bis zu acht Befehle rund um den
// Mauszeiger, darunter eine Liste weiterer Befehle. Bedienung per Klick oder
// als Geste: rechte Taste gedrueckt halten, in Richtung ziehen, loslassen.
class MarkingMenu : public QWidget {
    Q_OBJECT
public:
    enum Dir { N, NE, E, SE, S, SW, W, NW };

    explicit MarkingMenu(QWidget* parent = nullptr);

    void setItem(Dir d, QAction* a);
    void addOverflow(QAction* a);
    void addOverflowSeparator();
    void popup(const QPoint& globalCenter);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    struct Entry {
        QPointer<QAction> action;
        QRect rect;
        bool separator = false;
    };
    void layoutItems();
    int itemAt(const QPoint& p) const;
    void activate(int index);

    std::array<Entry, 8> m_ring;
    std::vector<Entry> m_list;
    QPoint m_center;
    int m_hover = -1;   // 0..7 Ring, 8.. Liste
    bool m_gesture = true;
};
