#include "ui/ManagerMainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    ManagerMainWindow w;
    w.show();
    return a.exec();
}
