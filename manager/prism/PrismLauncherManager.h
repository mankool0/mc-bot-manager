#ifndef PRISMLAUNCHERMANAGER_H
#define PRISMLAUNCHERMANAGER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QRegularExpression>

class LogManager;
struct BotInstance;
struct PrismConfig;
enum class BotStatus;

class PrismLauncherManager : public QObject
{
    Q_OBJECT

public:
    explicit PrismLauncherManager(LogManager *logger, QObject *parent = nullptr);
    ~PrismLauncherManager();

    void setPrismConfig(PrismConfig *config);
    void launchBot(BotInstance *bot);
    void stopPrismGUI();
    bool isPrismGUIRunning() const;

signals:
    void minecraftLaunching(const QString &botName);
    void minecraftStarting(const QString &botName);
    void prismGUIStarted();
    void prismGUIStopped();

private:
    void launchPrismGUI(BotInstance *bot);
    void sendLaunchCommand(BotInstance *bot);
    void processOutput(const QString &output, bool isStderr = false);
    void parsePrismCommand(const QString &command, QString &executable, QStringList &arguments);

    LogManager *logManager;
    PrismConfig *prismConfig = nullptr;
    QProcess *prismGUIProcess = nullptr;
};

#endif // PRISMLAUNCHERMANAGER_H