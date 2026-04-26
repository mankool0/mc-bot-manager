#include "ui/ManagerMainWindow.h"
#include "ui/GlobalSettingsDialog.h"

#include <QApplication>
#include <QIcon>
#include "bot/WorldData.h"

int main(int argc, char *argv[])
{
    qRegisterMetaType<ChunkData>();
    qRegisterMetaType<BlockEntityData>();
    qRegisterMetaType<PlayerSaveData>();
    qRegisterMetaType<QVector<EntityData>>();
    qRegisterMetaType<QVector<BlockEntityData>>();
    Q_INIT_RESOURCE(qutepart_syntax_files);
    Q_INIT_RESOURCE(qutepart_theme_data);

    QApplication a(argc, argv);
    a.setApplicationVersion(APP_VERSION);
    a.setDesktopFileName("mc-bot-manager");
    a.setWindowIcon(QIcon(":/icons/icons/mc-bot-manager.svg"));
    GlobalSettingsDialog::applyColorScheme();
    ManagerMainWindow w;
    w.show();
    return a.exec();
}
