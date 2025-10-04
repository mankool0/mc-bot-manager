#ifndef PRISMLAUNCHERMANAGER_H
#define PRISMLAUNCHERMANAGER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QRegularExpression>

struct BotInstance;
struct PrismConfig;
enum class BotStatus;

class PrismLauncherManager : public QObject
{
    Q_OBJECT

public:
    static PrismLauncherManager& instance();
    ~PrismLauncherManager();

    static void setPrismConfig(PrismConfig *config);
    static void launchBot(BotInstance *bot);
    static void stopBot(qint64 minecraftPid);
    static void stopPrismGUI();
    static bool isPrismGUIRunning();
    static qint64 getPrismGUIPid();

signals:
    void minecraftLaunching(const QString &botName);
    void minecraftStarting(const QString &botName);
    void prismGUIStarted();
    void prismGUIStopped();

private:
    explicit PrismLauncherManager(QObject *parent = nullptr);
    PrismLauncherManager(const PrismLauncherManager&) = delete;
    PrismLauncherManager& operator=(const PrismLauncherManager&) = delete;

    void launchBotImpl(BotInstance *bot);
    void stopBotImpl(qint64 minecraftPid);
    void stopPrismGUIImpl();
    void launchPrismGUIImpl(BotInstance *bot);
    void sendLaunchCommandImpl(BotInstance *bot);
    void processOutput(const QString &output, bool isStderr = false);
    void parsePrismCommand(const QString &command, QString &executable, QStringList &arguments);
    PrismConfig *prismConfig = nullptr;
    QProcess *prismGUIProcess = nullptr;
};

#endif // PRISMLAUNCHERMANAGER_H