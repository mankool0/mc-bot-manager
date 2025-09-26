#include "PrismLauncherManager.h"
#include "logging/LogManager.h"
#include "ui/ManagerMainWindow.h"
#include <QTimer>

PrismLauncherManager::PrismLauncherManager(LogManager *logger, QObject *parent)
    : QObject(parent), logManager(logger)
{
}

PrismLauncherManager::~PrismLauncherManager()
{
    stopPrismGUI();
}

void PrismLauncherManager::setPrismConfig(PrismConfig *config)
{
    prismConfig = config;
}

void PrismLauncherManager::launchBot(BotInstance *bot)
{
    if (!prismConfig) {
        logManager->log("PrismLauncher config not set", LogManager::Error);
        return;
    }

    if (isPrismGUIRunning()) {
        logManager->log("PrismLauncher GUI already running, sending launch command...", LogManager::Info);
        sendLaunchCommand(bot);
    } else {
        launchPrismGUI(bot);
    }
}

void PrismLauncherManager::stopPrismGUI()
{
    if (prismGUIProcess) {
        prismGUIProcess->terminate();
        if (!prismGUIProcess->waitForFinished(5000)) {
            prismGUIProcess->kill();
        }
        prismGUIProcess->deleteLater();
        prismGUIProcess = nullptr;
        emit prismGUIStopped();
    }
}

bool PrismLauncherManager::isPrismGUIRunning() const
{
    return prismGUIProcess != nullptr && prismGUIProcess->state() == QProcess::Running;
}

void PrismLauncherManager::launchPrismGUI(BotInstance *bot)
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
        logManager->log("PrismLauncher GUI started, waiting for initialization...", LogManager::Info);
        emit prismGUIStarted();

        QTimer::singleShot(2000, this, [this, bot]() { sendLaunchCommand(bot); });
    });

    connect(prismGUIProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::CrashExit) {
                    logManager
                        ->log(QString("PrismLauncher GUI crashed (exit code: %1)").arg(exitCode),
                              LogManager::Error);
                } else {
                    logManager->log(QString("PrismLauncher GUI exited normally (exit code: %1)")
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
            logManager->log(errorMsg, LogManager::Error);
            break;
        case QProcess::Crashed:
            logManager->log("PrismLauncher GUI crashed", LogManager::Error);
            break;
        default:
            logManager->log("Unknown error occurred with PrismLauncher GUI", LogManager::Error);
        }
        prismGUIProcess = nullptr;
        emit prismGUIStopped();
    });

    logManager->log(QString("Starting PrismLauncher GUI: %1 %2").arg(prismExe, arguments.join(" ")),
                    LogManager::Info);
    prismGUIProcess->start(prismExe, arguments);
}

void PrismLauncherManager::sendLaunchCommand(BotInstance *bot)
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
        logManager->log(QString("Sent launch command for bot '%1'").arg(bot->name), LogManager::Info);
    });

    connect(bot->process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, bot](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::CrashExit) {
                    logManager->log(QString("Launch command for bot '%1' crashed (exit code: %2)")
                                        .arg(bot->name)
                                        .arg(exitCode),
                                    LogManager::Error);
                    bot->status = BotStatus::Offline;
                    bot->minecraftPid = 0;
                } else {
                    logManager->log(QString("Launch command for bot '%1' completed (exit code: %2)")
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
        logManager->log(errorMsg, LogManager::Error);

        bot->status = BotStatus::Offline;
        bot->minecraftPid = 0;
        bot->process->deleteLater();
        bot->process = nullptr;
    });

    logManager->log(QString("Executing launch command: %1 %2").arg(prismExe, arguments.join(" ")),
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

        logManager->logPrism(cleanLine);

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
