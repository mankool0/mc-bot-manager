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
#include <QDockWidget>
#include <QCloseEvent>

#include "bot/BotManager.h"
#include "ui_ManagerMainWindow.h"

class LogManager;
class PrismLauncherManager;
class PipeServer;
class QProcess;
class NetworkStatsWidget;
class BotConsoleWidget;

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

protected:
    void closeEvent(QCloseEvent *event) override;

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
    void showNetworkStats(bool show);
    void showAboutDialog();

private:
    Ui::ManagerMainWindow *ui;
    QString selectedBotName;
    PrismConfig prismConfig;
    bool loadingConfiguration;
    bool detailsPinned;

    struct ScheduledLaunch {
        QString botName;
        QDateTime launchTime;

        bool operator==(const ScheduledLaunch &other) const {
            return botName == other.botName;
        }
    };
    QList<ScheduledLaunch> scheduledLaunches;
    QTimer *launchSchedulerTimer = nullptr;
    QTimer *uptimeCheckTimer = nullptr;
    QDockWidget *networkStatsDock = nullptr;

    void setupUI();
    void updateInstancesTable();
    void loadBotConfiguration(const BotInstance &bot);
    void updateStatusDisplay();
    void loadPrismLauncherConfig();
    void updateInstanceComboBox();
    void updateAccountComboBox();
    QStringList getUsedInstances() const;

    void saveSettings();
    void loadSettings();
    void saveBotInstance(QSettings &settings, const BotInstance &bot, int index);
    BotInstance loadBotInstance(QSettings &settings, int index);

    bool launchBotByName(const QString &botName);
    void restartBotByName(const QString &botName, const QString &reason);

    void checkScheduledLaunches();
    void checkBotUptimes();

    void onClearLog();
    void onAutoScrollToggled(bool checked);

    void setupPipeServer();
    void onClientConnected(int connectionId, const QString &botName);
    void onClientDisconnected(int connectionId);

    void setupConsoleTab();
    void onConsoleCommandEntered(const QString &command);
    void handleCommandResponse(const QString &botName, bool success, const QString &message);
    void onPinDetailsToggled(bool pinned);

    void setupMeteorTab();
    void onMeteorModulesReceived(const QString &botName);
    void onMeteorSingleModuleUpdated(const QString &botName, const QString &moduleName);
    void onMeteorModuleToggled(const QString &moduleName, bool enabled);
    void onMeteorSettingChanged(const QString &moduleName, const QString &settingPath, const QString &value);

    void setupBaritoneTab();
    void onBaritoneSettingsReceived(const QString &botName);
    void onBaritoneCommandsReceived(const QString &botName);
    void onBaritoneSingleSettingUpdated(const QString &botName, const QString &settingName);
    void onBaritoneSettingChanged(const QString &settingName, const QString &value);

    static QString statusToString(BotStatus status);
};
#endif // MANAGERMAINWINDOW_H
