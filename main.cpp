// WSPR Scheduler Map 3 - Qt 6 port of WSPR Scheduler Map 2 (C# Wspr_Map).
#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

#ifndef WSPRMAP_VERSION
#define WSPRMAP_VERSION "0"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("WSPR Scheduler Map 3");
    QApplication::setOrganizationName("G4GCI");
    QApplication::setApplicationVersion(WSPRMAP_VERSION);
    QApplication::setWindowIcon(QIcon(":/wsprmap.ico"));
    QGuiApplication::setDesktopFileName("wspr-scheduler-map-3");   // Linux: the menu entry's icon in the taskbar

    MainWindow w;
    w.show();
    w.start();
    return app.exec();
}
