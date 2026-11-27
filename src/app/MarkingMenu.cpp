#include "app/MarkingMenu.h"

#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

#include <cmath>

namespace {
constexpr int kW = 580, kRingH = 300, kItemW = 160, kItemH = 28, kListW = 230, kListH = 24;
constexpr double kRx = 150, kRy = 100;
}

MarkingMenu::MarkingMenu(QWidget* parent) : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
}

void MarkingMenu::setItem(Dir d, QAction* a)
{
    m_ring[d].action = a;
}

void MarkingMenu::addOverflow(QAction* a)
{
    m_list.push_back({a, {}, false});
}

void MarkingMenu::addOverflowSeparator()
{
    m_list.push_back({nullptr, {}, true});
}

void MarkingMenu::layoutItems()
{
    m_center = QPoint(kW / 2, kRingH / 2);
    for (int i = 0; i < 8; ++i) {
        const double ang = M_PI / 2 - i * M_PI / 4; // N = oben, im Uhrzeigersinn
        const QPointF c(m_center.x() + std::cos(ang) * kRx, m_center.y() - std::sin(ang) * kRy);
        QRect r(0, 0, kItemW, kItemH);
        // Links/rechts liegende Eintraege am Rand des Kreises ausrichten
        if (i == E || i == NE || i == SE) r.moveTopLeft(QPoint(int(c.x()) - 30, int(c.y()) - kItemH / 2));
        else if (i == W || i == NW || i == SW) r.moveTopRight(QPoint(int(c.x()) + 30, int(c.y()) - kItemH / 2));
        else r.moveCenter(c.toPoint());
        m_ring[i].rect = r;
    }
    int y = kRingH + 4;
    for (auto& e : m_list) {
        const int h = e.separator ? 7 : kListH;
        e.rect = QRect(m_center.x() - kListW / 2, y, kListW, h);
        y += h;
    }
    resize(kW, y + 4);

    QRegion mask(QRect(m_center.x() - 14, m_center.y() - 14, 28, 28), QRegion::Ellipse);
    for (const auto& e : m_ring)
        if (e.action) mask += e.rect.adjusted(-1, -1, 1, 1);
    if (!m_list.empty())
        mask += QRect(m_center.x() - kListW / 2 - 1, kRingH + 3, kListW + 2, y - kRingH);
    setMask(mask);
}

void MarkingMenu::popup(const QPoint& globalCenter)
{
    layoutItems();
    QPoint topLeft = globalCenter - m_center;
    if (QScreen* s = screen()) {
        const QRect avail = s->availableGeometry();
        topLeft.setY(std::min(topLeft.y(), avail.bottom() - height()));
    }
    move(topLeft);
    show();
}

int MarkingMenu::itemAt(const QPoint& p) const
{
    for (int i = 0; i < 8; ++i)
        if (m_ring[i].action && m_ring[i].action->isEnabled() && m_ring[i].rect.contains(p)) return i;
    for (size_t i = 0; i < m_list.size(); ++i)
        if (m_list[i].action && m_list[i].action->isEnabled() && m_list[i].rect.contains(p)) return 8 + int(i);
    // Geste: weit genug vom Zentrum weg zaehlt die Richtung
    const QPoint d = p - m_center;
    if (m_gesture && std::hypot(d.x(), d.y()) > 45 && p.y() < kRingH) {
        double ang = std::atan2(-double(d.y()) / kRy, double(d.x()) / kRx); // 0 = Osten
        int idx = int(std::lround((M_PI / 2 - ang) / (M_PI / 4))) % 8;
        if (idx < 0) idx += 8;
        if (m_ring[idx].action && m_ring[idx].action->isEnabled()) return idx;
    }
    return -1;
}

void MarkingMenu::activate(int index)
{
    QAction* a = index < 8 ? m_ring[index].action.data() : m_list[size_t(index - 8)].action.data();
    close();
    if (a) a->trigger();
}

void MarkingMenu::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(250, 251, 253));

    // Zentrum und Verbindungslinie zum aktiven Eintrag
    if (m_hover >= 0 && m_hover < 8) {
        p.setPen(QPen(QColor(47, 111, 191), 2));
        p.drawLine(m_center, m_ring[m_hover].rect.center());
    }
    p.setPen(QPen(QColor(120, 130, 145), 2));
    p.setBrush(Qt::white);
    p.drawEllipse(m_center, 12, 12);

    auto drawEntry = [&](const Entry& e, bool hover, bool ring) {
        if (e.separator) {
            p.setPen(QColor(210, 215, 222));
            p.drawLine(e.rect.left() + 6, e.rect.center().y(), e.rect.right() - 6, e.rect.center().y());
            return;
        }
        if (!e.action) return;
        const bool enabled = e.action->isEnabled();
        p.setPen(QPen(hover ? QColor(47, 111, 191) : QColor(185, 192, 202), 1));
        p.setBrush(hover ? QColor(214, 230, 250) : Qt::white);
        if (ring) p.drawRoundedRect(QRectF(e.rect).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        else p.fillRect(e.rect, hover ? QColor(214, 230, 250) : Qt::white);
        const QIcon icon = e.action->icon();
        int x = e.rect.left() + 6;
        if (!icon.isNull()) {
            icon.paint(&p, QRect(x, e.rect.center().y() - 9, 18, 18), Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
            x += 24;
        }
        p.setPen(enabled ? QColor(30, 40, 55) : QColor(160, 166, 175));
        QString text = e.action->text();
        text.replace("-\n", "").replace('\n', ' ').remove('&');
        p.drawText(QRect(x, e.rect.top(), e.rect.right() - x - 4, e.rect.height()), Qt::AlignVCenter | Qt::AlignLeft, text);
    };
    for (int i = 0; i < 8; ++i) drawEntry(m_ring[i], m_hover == i, true);
    if (!m_list.empty()) {
        const QRect lr(m_center.x() - kListW / 2, kRingH + 4, kListW, height() - kRingH - 8);
        p.setPen(QColor(185, 192, 202));
        p.setBrush(Qt::white);
        p.drawRect(lr.adjusted(0, 0, -1, -1));
        for (size_t i = 0; i < m_list.size(); ++i) drawEntry(m_list[i], m_hover == 8 + int(i), false);
    }
}

void MarkingMenu::mouseMoveEvent(QMouseEvent* e)
{
    const int h = itemAt(e->position().toPoint());
    if (h != m_hover) {
        m_hover = h;
        update();
    }
}

void MarkingMenu::mousePressEvent(QMouseEvent* e)
{
    if (!rect().contains(e->position().toPoint()) || !mask().contains(e->position().toPoint())) {
        close();
        return;
    }
    m_gesture = false;
}

void MarkingMenu::mouseReleaseEvent(QMouseEvent* e)
{
    const QPoint p = e->position().toPoint();
    const int h = itemAt(p);
    if (h >= 0) {
        activate(h);
        return;
    }
    // Loslassen nahe der Mitte: Menue bleibt offen fuer einen Klick
    m_gesture = false;
}

void MarkingMenu::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) close();
    else QWidget::keyPressEvent(e);
}
