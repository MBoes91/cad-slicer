#include "app/MainWindow.h"

#include <QApplication>
#include <QHash>
#include <QStyleFactory>
#include <QTranslator>

#include <cstdlib>

namespace {

// Deutsche Texte fuer Qt-Standardknoepfe und Kontextmenues, ohne .qm-Dateien
class GermanStandardTexts : public QTranslator {
public:
    QString translate(const char* context, const char* source, const char*, int) const override
    {
        static const QHash<QString, QString> buttons = {
            {"OK", "OK"}, {"Cancel", "Abbrechen"}, {"&Cancel", "&Abbrechen"}, {"Save", "Speichern"},
            {"&Save", "&Speichern"}, {"Save All", "Alle speichern"}, {"Discard", "Verwerfen"},
            {"Don't Save", "Nicht speichern"}, {"Close", "Schließen"}, {"Apply", "Übernehmen"},
            {"Reset", "Zurücksetzen"}, {"Restore Defaults", "Standardwerte"}, {"Help", "Hilfe"},
            {"Yes", "Ja"}, {"&Yes", "&Ja"}, {"No", "Nein"}, {"&No", "&Nein"}, {"Yes to &All", "Ja, &alle"},
            {"No to A&ll", "N&ein, keine"}, {"Abort", "Abbrechen"}, {"Retry", "Wiederholen"},
            {"Ignore", "Ignorieren"}, {"Open", "Öffnen"}, {"&Open", "Ö&ffnen"},
            {"&Undo", "&Rückgängig"}, {"&Redo", "Wieder&herstellen"}, {"Cu&t", "&Ausschneiden"},
            {"&Copy", "&Kopieren"}, {"&Paste", "&Einfügen"}, {"Delete", "Löschen"}, {"Select All", "Alles auswählen"},
        };
        const QByteArray ctx(context);
        if (ctx == "QPlatformTheme" || ctx == "QDialogButtonBox" || ctx == "QMessageBox" || ctx == "QLineEdit"
            || ctx == "QWidgetTextControl" || ctx == "QAbstractSpinBox") {
            auto it = buttons.find(QString::fromUtf8(source));
            if (it != buttons.end()) return *it;
        }
        return {};
    }
    bool isEmpty() const override { return false; }
};

} // namespace

int main(int argc, char** argv)
{
#if !defined(_WIN32) && !defined(__APPLE__)
    // Die OpenCascade-Ansicht braucht unter Linux ein X11-Fenster (auch unter Wayland via XWayland)
    if (!std::getenv("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName("FormWerk");
    QApplication::setOrganizationName("FormWerk");
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    static GermanStandardTexts german;
    QApplication::installTranslator(&german);

    MainWindow w;
    w.show();
    return app.exec();
}
