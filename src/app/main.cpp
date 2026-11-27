#include "app/MainWindow.h"

#include <QApplication>
#include <QStyleFactory>

#include <cstdlib>

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

    MainWindow w;
    w.show();
    return app.exec();
}
