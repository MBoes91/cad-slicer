#include "app/PropertyPanel.h"

#include <QButtonGroup>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

PropertyPanel::PropertyPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName("Eigenschaften");
    setStyleSheet("#Eigenschaften { background: #f7f8fa; }"
                  "QToolButton#Abschnitt { border: none; border-bottom: 1px solid #d5dae1; font-weight: bold; color: #3b4656;"
                  " text-align: left; padding: 3px 0; background: transparent; }"
                  "QToolButton#Wahl { border: 1px solid #c8ced6; border-radius: 3px; background: white; padding: 2px; }"
                  "QToolButton#Wahl:checked { border: 1px solid #2f6fbf; background: #d6e6fa; }");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 6, 8, 8);
    v->setSpacing(6);

    auto* head = new QHBoxLayout;
    m_icon = new QLabel;
    m_title = new QLabel;
    m_title->setStyleSheet("font-weight: bold; font-size: 13px; color: #26364d;");
    head->addWidget(m_icon);
    head->addWidget(m_title, 1);
    v->addLayout(head);

    m_body = new QWidget;
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(4);
    v->addWidget(m_body);

    m_message = new QLabel;
    m_message->setWordWrap(true);
    m_message->hide();
    v->addWidget(m_message);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* ok = new QPushButton("OK");
    ok->setDefault(true);
    ok->setStyleSheet("QPushButton { background: #2f6fbf; color: white; border: none; border-radius: 3px; padding: 5px 18px; }"
                      "QPushButton:hover { background: #3b7fd3; }");
    auto* cancel = new QPushButton("Abbrechen");
    connect(ok, &QPushButton::clicked, this, &PropertyPanel::accepted);
    connect(cancel, &QPushButton::clicked, this, &PropertyPanel::rejected);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    v->addLayout(buttons);
    v->addStretch();
}

void PropertyPanel::begin(const QIcon& icon, const QString& title)
{
    m_icon->setPixmap(icon.pixmap(22, 22));
    m_title->setText(title);
    setMessage({}, false);
    // Alten Inhalt entfernen
    while (QLayoutItem* it = m_bodyLayout->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
}

QFormLayout* PropertyPanel::addSection(const QString& title)
{
    auto* header = new QToolButton;
    header->setObjectName("Abschnitt");
    header->setText(title);
    header->setCheckable(true);
    header->setChecked(true);
    header->setArrowType(Qt::DownArrow);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* content = new QWidget;
    auto* form = new QFormLayout(content);
    form->setContentsMargins(4, 2, 0, 6);
    form->setLabelAlignment(Qt::AlignLeft);
    connect(header, &QToolButton::toggled, content, [header, content](bool on) {
        content->setVisible(on);
        header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    m_bodyLayout->addWidget(header);
    m_bodyLayout->addWidget(content);
    return form;
}

void PropertyPanel::finish()
{
    m_body->adjustSize();
}

void PropertyPanel::setMessage(const QString& text, bool error)
{
    m_message->setText(text);
    m_message->setStyleSheet(error ? "color: #b3261e;" : "color: #4a5568;");
    m_message->setVisible(!text.isEmpty());
}

QWidget* PropertyPanel::choiceRow(QButtonGroup* group, const QList<QPair<QIcon, QString>>& items, int checked)
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(3);
    for (int i = 0; i < items.size(); ++i) {
        auto* b = new QToolButton;
        b->setObjectName("Wahl");
        b->setIcon(items[i].first);
        b->setIconSize(QSize(24, 24));
        b->setToolTip(items[i].second);
        b->setCheckable(true);
        b->setChecked(i == checked);
        group->addButton(b, i);
        h->addWidget(b);
    }
    group->setExclusive(true);
    h->addStretch();
    return w;
}

void PropertyPanel::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        emit accepted();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        emit rejected();
        return;
    }
    QWidget::keyPressEvent(e);
}
