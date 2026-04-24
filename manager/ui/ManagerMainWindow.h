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
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QCloseEvent>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <functional>

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
    QMap<QString, QString> accountIdToNameMap;
    bool useHook = true;
};

class ManagerMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    ManagerMainWindow(QWidget *parent = nullptr);
    ~ManagerMainWindow();

    static QString getWorldSaveBasePath();
    static void setWorldSaveBasePath(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void showInstancesContextMenu(const QPoint &pos);
    void onHeaderContextMenu(const QPoint &pos);
    void addNewBot();
    void removeBot();
    void onInstanceSelectionChanged();
    void launchBot();
    void stopBot();
    void restartBot();
    void refreshToken();
    void launchPrismLauncher();
    void configurePrismLauncher();
    void configureWorldSavePath();
    void openGlobalSettings();
    void loadSettingsFromFile();
    void onConfigurationChanged();
    void launchAllBots();
    void stopAllBots();
    void showNetworkStats(bool show);
    void showAboutDialog();
    void onTestProxyClicked();
    void checkProxyHealth();
    void onProxyDisconnectDetected(const QString &botName);

private:
    Ui::ManagerMainWindow *ui;
    QString selectedBotName;
    PrismConfig prismConfig;

    bool m_proxyTestPassed = false;
    QString m_proxyTestedHost;
    int m_proxyTestedPort = 0;
    bool loadingConfiguration;
    bool detailsPinned;
    static QString worldSaveBasePath;

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
    QTimer *proxyHealthTimer = nullptr;

    QMap<QString, QDateTime> m_lastAccountRefreshTime;
    QMap<QString, QDateTime> m_lastBotLaunchTime;
    QMap<QString, int>       m_tokenRefreshAttempts;
    QSet<QString>            m_refreshingAccounts;

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
    void sendHookRefresh(const QString &account, const QString &botName, std::function<void(bool)> onDone);
    void refreshAccountThenLaunch(const QString &accountProfile, const QString &botName);
    void onAccountRefreshSucceeded(const QString &accountName);

    void checkScheduledLaunches();
    void checkBotUptimes();
    void checkBotProxyHealth(const QString &botName);

    void saveColumnVisibility();
    void loadColumnVisibility();

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
    void onMeteorSettingChanged(const QString &moduleName, const QString &settingPath, const QVariant &value);

    void setupBaritoneTab();
    void onBaritoneSettingsReceived(const QString &botName);
    void onBaritoneCommandsReceived(const QString &botName);
    void onBaritoneSingleSettingUpdated(const QString &botName, const QString &settingName);
    void onBaritoneSettingChanged(const QString &settingName, const QVariant &value);

    void setupScriptsTab();
    void setupCodeEditorThemeMenu();
    void onEditorThemeChanged(const QString &themeName);

    static QString statusToString(BotStatus status);
};
#endif // MANAGERMAINWINDOW_H
