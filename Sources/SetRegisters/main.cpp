#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("INR");
    QCoreApplication::setApplicationName("SetRegisters");
	QCoreApplication::setApplicationVersion("3.1");
    MainWindow w;
    w.show();

    return a.exec();
}
