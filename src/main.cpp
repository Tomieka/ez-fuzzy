#include "mainwindow.h"
#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    a.setApplicationName("EZ Fuzzy");
    a.setApplicationVersion("1.0.0");
    a.setOrganizationName("EZ Fuzzy");
    
    MainWindow w;
    w.show();
    return a.exec();
} 