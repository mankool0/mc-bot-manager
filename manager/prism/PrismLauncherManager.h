#ifndef PRISMLAUNCHERMANAGER_H
#define PRISMLAUNCHERMANAGER_H

#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QRegularExpression>

struct BotInstance;
struct PrismConfig;
enum class BotStatus;

struct PrismAccountInfo {
    QString uuid;
    QString name;
    QString internalId;
};

struct PrismInstanceInfo {
    QString id;
    QString name;
};

class PrismLauncherManager : public QObject
{
    Q_OBJECT

public:
    static PrismLauncherManager& instance();
    ~PrismLauncherManager();

    static void setPrismConfig(PrismConfig *config);
    static void launchBot(BotInstance *bot);
    static void openPrismGUI();
    static void stopBot(qint64 minecraftPid);
    static void stopPrismGUI();
    static bool isPrismGUIRunning();
    static qint64 getPrismGUIPid();
    static QString hookSocketPath();
    static bool isHookAvailable();

signals:
    void minecraftLaunching(const QString &botName);
    void minecraftStarting(const QString &botName);
    void minecraftStopped(const QString &botName);
    void prismGUIStarted();
    void prismGUIStopped();
    void hookAvailabilityChanged(bool available);
    void accountRefreshStarted(const QString &accountName);
    void accountRefreshSucceeded(const QString &accountName);
    void accountRefreshFailed(const QString &accountName);
    void accountsUpdated(QVector<PrismAccountInfo> accounts);
    void instancesUpdated(QVector<PrismInstanceInfo> instances);

private:
    explicit PrismLauncherManager(QObject *parent = nullptr);
    PrismLauncherManager(const PrismLauncherManager&) = delete;
    PrismLauncherManager& operator=(const PrismLauncherManager&) = delete;

    void launchBotImpl(BotInstance *bot);
    void openPrismGUIImpl();
    void stopBotImpl(qint64 minecraftPid);
    void stopPrismGUIImpl();
    void launchPrismGUIImpl(BotInstance *bot);
    void sendLaunchCommandImpl(BotInstance *bot);
    void processOutput(const QString &output, bool isStderr = false);
    void parsePrismCommand(const QString &command, QString &executable, QStringList &arguments);
    void pingHook();
    void connectSubscriber();
    void handleSubscriberData();
#ifdef Q_OS_WIN
    void injectHookDLL();
#endif

    PrismConfig *prismConfig = nullptr;
    QProcess *prismGUIProcess = nullptr;
    QTimer *hookHeartbeatTimer = nullptr;
    bool m_hookAvailable = false;
    QString m_currentlyRefreshingAccount;

    QLocalSocket *m_subscriberSocket = nullptr;
    bool m_collectingAccounts = false;
    bool m_collectingInstances = false;
    QVector<PrismAccountInfo> m_pendingAccounts;
    QVector<PrismInstanceInfo> m_pendingInstances;
};

#endif // PRISMLAUNCHERMANAGER_H
