#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("INR");
	QCoreApplication::setApplicationName("Readout");
	QCoreApplication::setApplicationVersion("2.1");
    MainWindow w;
    w.show();

    return a.exec();
}
