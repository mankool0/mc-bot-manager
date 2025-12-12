#include "ui/ManagerMainWindow.h"

#include <QApplication>
#include "bot/WorldData.h"

int main(int argc, char *argv[])
{
    qRegisterMetaType<ChunkData>();
    Q_INIT_RESOURCE(qutepart_syntax_files);
    Q_INIT_RESOURCE(qutepart_theme_data);

    QApplication a(argc, argv);
    ManagerMainWindow w;
    w.show();
    return a.exec();
}
