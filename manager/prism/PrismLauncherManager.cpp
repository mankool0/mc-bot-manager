#include "PrismLauncherManager.h"
#include "logging/LogManager.h"
#include "ui/ManagerMainWindow.h"
#include <QTimer>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#endif

PrismLauncherManager& PrismLauncherManager::instance()
{
    static PrismLauncherManager instance;
    return instance;
}

PrismLauncherManager::PrismLauncherManager(QObject *parent)
    : QObject(parent)
{
}

PrismLauncherManager::~PrismLauncherManager()
{
    stopPrismGUI();
}

void PrismLauncherManager::setPrismConfig(PrismConfig *config)
{
    instance().prismConfig = config;
}

void PrismLauncherManager::launchBot(BotInstance *bot)
{
    instance().launchBotImpl(bot);
}

void PrismLauncherManager::launchBotImpl(BotInstance *bot)
{
    if (!prismConfig) {
        LogManager::log("PrismLauncher config not set", LogManager::Error);
        return;
    }

    if (prismGUIProcess != nullptr && prismGUIProcess->state() == QProcess::Running) {
        LogManager::log("PrismLauncher GUI already running, sending launch command...", LogManager::Info);
        sendLaunchCommandImpl(bot);
    } else {
        launchPrismGUIImpl(bot);
    }
}

void PrismLauncherManager::stopPrismGUI()
{
    instance().stopPrismGUIImpl();
}

void PrismLauncherManager::stopPrismGUIImpl()
{
    if (prismGUIProcess) {
        // Disconnect all signals to prevent crashes during deletion
        prismGUIProcess->disconnect();

        // Kill the entire process group to ensure child processes are terminated
#ifdef Q_OS_UNIX
        if (prismGUIProcess->state() == QProcess::Running) {
            qint64 pid = prismGUIProcess->processId();
            if (pid > 0) {
                ::kill(-pid, SIGTERM);
            }
        }
#endif

        prismGUIProcess->terminate();
        if (!prismGUIProcess->waitForFinished(3000)) {
            prismGUIProcess->kill();
            prismGUIProcess->waitForFinished(1000);
        }
        prismGUIProcess->deleteLater();
        prismGUIProcess = nullptr;
        emit prismGUIStopped();
    }
}

bool PrismLauncherManager::isPrismGUIRunning()
{
    return instance().prismGUIProcess != nullptr && instance().prismGUIProcess->state() == QProcess::Running;
}

qint64 PrismLauncherManager::getPrismGUIPid()
{
    if (instance().prismGUIProcess && instance().prismGUIProcess->state() == QProcess::Running) {
        return instance().prismGUIProcess->processId();
    }
    return 0;
}

void PrismLauncherManager::stopBot(qint64 minecraftPid)
{
    instance().stopBotImpl(minecraftPid);
}

void PrismLauncherManager::stopBotImpl(qint64 minecraftPid)
{
    if (!prismConfig) {
        LogManager::log("Cannot stop bot: PrismLauncher config not set", LogManager::Error);
        return;
    }

    if (minecraftPid <= 0) {
        LogManager::log("Cannot stop bot: Invalid Minecraft PID", LogManager::Error);
        return;
    }

    #ifdef Q_OS_WIN
    // Windows - direct kill
    QProcess::startDetached("taskkill", QStringList() << "/PID" << QString::number(minecraftPid) << "/F");
    LogManager::log(QString("Force stopping Minecraft process %1").arg(minecraftPid), LogManager::Info);
    #else
    // Unix-like systems - check if using Flatpak
    if (prismConfig->prismExecutable.contains("flatpak")) {
        qint64 prismPid = getPrismGUIPid();
        if (prismPid > 0) {
            // Use flatpak enter to kill the Minecraft process inside the sandbox
            QStringList args;
            args << "enter" << QString::number(prismPid) << "kill" << QString::number(minecraftPid);

            QProcess *killProcess = new QProcess(this);
            connect(killProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    killProcess, &QProcess::deleteLater);
            killProcess->start("flatpak", args);

            LogManager::log(QString("Force stopping Minecraft process %1 in Flatpak sandbox %2")
                           .arg(minecraftPid).arg(prismPid), LogManager::Info);
        } else {
            LogManager::log("Cannot stop bot: PrismLauncher GUI not running", LogManager::Warning);
        }
    } else {
        // Direct kill for non-Flatpak Unix
        QProcess::startDetached("kill", QStringList() << QString::number(minecraftPid));
        LogManager::log(QString("Force stopping Minecraft process %1").arg(minecraftPid), LogManager::Info);
    }
    #endif
}

void PrismLauncherManager::launchPrismGUIImpl(BotInstance *bot)
{
    QString prismExe;
    QStringList arguments;
    parsePrismCommand(prismConfig->prismExecutable, prismExe, arguments);

    if (prismGUIProcess) {
        prismGUIProcess->deleteLater();
    }

    prismGUIProcess = new QProcess(this);

    connect(prismGUIProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        QByteArray data = prismGUIProcess->readAllStandardOutput();
        processOutput(QString::fromUtf8(data), false);
    });

    connect(prismGUIProcess, &QProcess::readyReadStandardError, this, [this]() {
        QByteArray data = prismGUIProcess->readAllStandardError();
        processOutput(QString::fromUtf8(data), true);
    });

    connect(prismGUIProcess, &QProcess::started, this, [this, bot]() {
        LogManager::log("PrismLauncher GUI started, waiting for initialization...", LogManager::Info);
        emit prismGUIStarted();

        QTimer::singleShot(2000, this, [this, bot]() { sendLaunchCommandImpl(bot); });
    });

    connect(prismGUIProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::CrashExit) {
                    LogManager::log(QString("PrismLauncher GUI crashed (exit code: %1)").arg(exitCode),
                              LogManager::Error);
                } else {
                    LogManager::log(QString("PrismLauncher GUI exited normally (exit code: %1)")
                                        .arg(exitCode),
                                    LogManager::Info);
                }
                prismGUIProcess = nullptr;
                emit prismGUIStopped();
            });

    connect(prismGUIProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        QString errorMsg;
        switch (error) {
        case QProcess::FailedToStart:
            errorMsg = QString("Failed to start PrismLauncher GUI - Command: %1")
                           .arg(prismConfig->prismExecutable);
            LogManager::log(errorMsg, LogManager::Error);
            break;
        case QProcess::Crashed:
            LogManager::log("PrismLauncher GUI crashed", LogManager::Error);
            break;
        default:
            LogManager::log("Unknown error occurred with PrismLauncher GUI", LogManager::Error);
        }
        prismGUIProcess = nullptr;
        emit prismGUIStopped();
    });

    LogManager::log(QString("Starting PrismLauncher GUI: %1 %2").arg(prismExe, arguments.join(" ")),
                    LogManager::Info);

#ifdef Q_OS_UNIX
    // Start process in its own process group so we can kill the entire group
    prismGUIProcess->setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif

    prismGUIProcess->start(prismExe, arguments);
}

void PrismLauncherManager::sendLaunchCommandImpl(BotInstance *bot)
{
    QString prismExe;
    QStringList arguments;
    parsePrismCommand(prismConfig->prismExecutable, prismExe, arguments);

    arguments << "-l" << bot->instance;    // Launch instance
    arguments << "-a" << bot->account;     // Use account

    if (!bot->server.isEmpty()) {
        arguments << "-s" << bot->server;
    }

    bot->process = new QProcess(this);

    connect(bot->process, &QProcess::started, this, [this, bot]() {
        LogManager::log(QString("Sent launch command for bot '%1'").arg(bot->name), LogManager::Info);
    });

    connect(bot->process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, bot](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::CrashExit) {
                    LogManager::log(QString("Launch command for bot '%1' crashed (exit code: %2)")
                                        .arg(bot->name)
                                        .arg(exitCode),
                                    LogManager::Error);
                    bot->status = BotStatus::Offline;
                    bot->minecraftPid = 0;
                } else {
                    LogManager::log(QString("Launch command for bot '%1' completed (exit code: %2)")
                                        .arg(bot->name)
                                        .arg(exitCode),
                                    LogManager::Info);
                }

                bot->process->deleteLater();
                bot->process = nullptr;
            });

    connect(bot->process, &QProcess::errorOccurred, this, [this, bot](QProcess::ProcessError error) {
        QString errorMsg;
        switch (error) {
        case QProcess::FailedToStart:
            errorMsg = QString("Failed to send launch command for bot '%1'").arg(bot->name);
            break;
        case QProcess::Crashed:
            errorMsg = QString("Launch command crashed for bot '%1'").arg(bot->name);
            break;
        default:
            errorMsg = QString("Unknown error occurred while launching bot '%1'").arg(bot->name);
        }
        LogManager::log(errorMsg, LogManager::Error);

        bot->status = BotStatus::Offline;
        bot->minecraftPid = 0;
        bot->process->deleteLater();
        bot->process = nullptr;
    });

    LogManager::log(QString("Executing launch command: %1 %2").arg(prismExe, arguments.join(" ")),
                    LogManager::Info);
    bot->process->start(prismExe, arguments);
}

void PrismLauncherManager::processOutput(const QString &output, bool isStderr)
{
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : std::as_const(lines)) {
        QString cleanLine = line;

        if (isStderr) {
            static QRegularExpression stderrReg = QRegularExpression("\\x1b\\[[0-9;]*m");
            cleanLine.remove(stderrReg);
        }

        LogManager::logPrism(cleanLine);

        if (cleanLine.contains("org.prismlauncher.EntryPoint") ||
            cleanLine.contains("net.minecraft.client.main.Main") ||
            cleanLine.contains("cpw.mods.modlauncher.Launcher")) {
            emit minecraftLaunching(""); // TODO: Identify which bot
        }

        if (cleanLine.contains("Profile") && cleanLine.contains("is now in use")) {
            emit minecraftStarting(""); // TODO: Identify which bot
        }
    }
}

void PrismLauncherManager::parsePrismCommand(const QString &command, QString &executable, QStringList &arguments)
{
    arguments.clear();

    if (command.contains(" ")) {
        QStringList parts = command.split(" ");
        executable = parts.takeFirst();
        arguments = parts;
    } else {
        executable = command;
    }
}
