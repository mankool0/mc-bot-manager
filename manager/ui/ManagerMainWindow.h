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
    void showNetworkStats();

private:
    Ui::ManagerMainWindow *ui;
    QString selectedBotName;
    PrismConfig prismConfig;
    bool loadingConfiguration;

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
    void restartBotByName(const QString &botName, const QString &reason);

    void checkScheduledLaunches();
    void checkBotUptimes();

    void onClearLog();
    void onAutoScrollToggled(bool checked);

    void setupPipeServer();
    void onClientConnected(int connectionId, const QString &botName);
    void onClientDisconnected(int connectionId);

    static QString statusToString(BotStatus status);
};
#endif // MANAGERMAINWINDOW_H
