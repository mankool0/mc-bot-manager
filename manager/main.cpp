#include "ui/ManagerMainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(qutepart_syntax_files);
    Q_INIT_RESOURCE(qutepart_theme_data);

    QApplication a(argc, argv);
    ManagerMainWindow w;
    w.show();
    return a.exec();
}
