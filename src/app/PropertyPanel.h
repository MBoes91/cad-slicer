#pragma once

#include <QIcon>
#include <QWidget>

class QFormLayout;
class QLabel;
class QVBoxLayout;
class QButtonGroup;

// Eigenschaften-Palette im Stil von Inventor: Titel, aufklappbare Abschnitte,
// OK / Abbrechen. Enter bestaetigt, Esc bricht ab.
class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    // Leert die Palette und setzt den Titel (z. B. "Extrusion › Extrusion1")
    void begin(const QIcon& icon, const QString& title);
    QFormLayout* addSection(const QString& title);
    void finish();
    void setMessage(const QString& text, bool error);

    // Gruppe aus umschaltbaren Symbol-Knoepfen (Richtung, Boolesch …)
    static QWidget* choiceRow(QButtonGroup* group, const QList<QPair<QIcon, QString>>& items, int checked);

signals:
    void accepted();
    void rejected();

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    QLabel* m_icon = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_message = nullptr;
    QWidget* m_body = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
};
