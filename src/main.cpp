#include <QApplication>
#include <QStyleFactory>

#include "ui/main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("LightSSH");
    app.setApplicationName("LightSSH_IDE");
    app.setStyle(QStyleFactory::create("Fusion"));

    MainWindow window;
    window.resize(1200, 760);
    window.show();

    return QApplication::exec();
}