#pragma once

#include <QIcon>
#include <QLineEdit>
#include <QWidget>

#include <functional>

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

// Eingabefeld fuer Werte wie in Inventor: nimmt Zahlen ("12,5 mm") oder
// Gleichungen mit Parametern ("d0 * 2", "Breite / 2") an.
class ValueEdit : public QLineEdit {
    Q_OBJECT
public:
    using Evaluator = std::function<bool(const QString& text, double& value, QString& error)>;
    ValueEdit(const QString& unit, Evaluator eval, QWidget* parent = nullptr);

    void setValue(double v, bool notify = false);
    void setExpression(const QString& expr, double v);
    double value() const { return m_value; }
    QString expression() const { return m_expr; } // leer bei festem Wert

    static QString format(double v);
    void commit(); // Eingabe uebernehmen (auch ohne Fokuswechsel)

signals:
    void valueEdited();

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    QString m_unit;
    Evaluator m_eval;
    double m_value = 0;
    QString m_expr;
};
