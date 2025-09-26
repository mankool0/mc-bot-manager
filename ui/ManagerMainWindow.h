#ifndef MANAGERMAINWINDOW_H
#define MANAGERMAINWINDOW_H

#include <QMainWindow>
#include <QMenu>
#include <QAction>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QStackedWidget>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QStandardPaths>
#include <QVector3D>
#include <QSettings>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QIODevice>

class LogManager;
class PrismLauncherManager;
class QProcess;

#include "ui_ManagerMainWindow.h"

enum class BotStatus {
    Offline,
    Starting,
    Online,
    Stopping
};

struct BotInstance {
    QString name;
    BotStatus status = BotStatus::Offline;
    QString instance;
    QString account;
    QString server;
    int connectionId = -1;
    int maxMemory;
    int currentMemory;
    int restartThreshold;
    bool autoRestart;
    bool tokenRefresh;
    bool debugLogging;

    QVector3D position;
    QString dimension;

    QProcess* process = nullptr;
    qint64 minecraftPid = 0;
};

struct PrismConfig {
    QString prismPath;
    QString prismExecutable;
    QStringList instances;
    QStringList accounts;
};

class ManagerMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    ManagerMainWindow(QWidget *parent = nullptr);
    ~ManagerMainWindow();

private slots:
    void showInstancesContextMenu(const QPoint &pos);
    void addNewBot();
    void removeBot();
    void onInstanceSelectionChanged();
    void launchBot();
    void stopBot();
    void restartBot();
    void configurePrismLauncher();
    void loadSettingsFromFile();
    void onConfigurationChanged();
    void launchAllBots();
    void stopAllBots();

private:
    Ui::ManagerMainWindow *ui;
    QList<BotInstance> botInstances;
    QString selectedBotName;
    PrismConfig prismConfig;
    bool loadingConfiguration;

    LogManager *logManager = nullptr;
    PrismLauncherManager *prismLauncherManager = nullptr;

    QStringList pendingLaunchQueue;
    bool isSequentialLaunching = false;
    QTimer *tableUpdateTimer = nullptr;

    QLocalServer *pipeServer = nullptr;
    QMap<int, QIODevice*> botConnections;  // connectionId -> socket
    int nextConnectionId = 1000;

    void setupUI();
    void updateInstancesTable();
    void loadBotConfiguration(const BotInstance &bot);
    void updateStatusDisplay();
    void loadPrismLauncherConfig();
    void parsePrismInstances();
    void parsePrismAccounts();
    void updateInstanceComboBox();
    void updateAccountComboBox();
    QString detectPrismLauncherPath();
    QStringList getUsedInstances() const;

    void saveSettings();
    void loadSettings();
    void saveBotInstance(QSettings &settings, const BotInstance &bot, int index);
    BotInstance loadBotInstance(QSettings &settings, int index);

    bool launchBotByName(const QString &botName);

    void launchNextBotInQueue();
    void onBotStatusChanged();

    void onClearLog();
    void onAutoScrollToggled(bool checked);

    void setupPipeServer();
    void handleNewConnection();
    void handleClientData(QIODevice *socket);
    void processHandshake(QIODevice *socket, const QJsonObject &message);
    void sendToBot(int connectionId, const QJsonObject &message);
    void broadcastToAllBots(const QJsonObject &message);

    static QString statusToString(BotStatus status);
};
#endif // MANAGERMAINWINDOW_H
