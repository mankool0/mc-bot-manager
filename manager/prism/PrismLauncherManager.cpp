#include "PrismLauncherManager.h"
#include "logging/LogManager.h"
#include "ui/ManagerMainWindow.h"
#include "bot/BotManager.h"
#include <QTimer>
#include <QSettings>
#include <QCoreApplication>
#include <QFile>
#include <QProcessEnvironment>
#include <QLocalSocket>
#include <memory>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

PrismLauncherManager& PrismLauncherManager::instance()
{
    static PrismLauncherManager instance;
    return instance;
}

PrismLauncherManager::PrismLauncherManager(QObject *parent)
    : QObject(parent)
{
    connect(this, &PrismLauncherManager::hookAvailabilityChanged, this, [this](bool available) {
        if (available) {
            connectSubscriber();
        } else if (m_subscriberSocket) {
            m_subscriberSocket->abort();
            m_subscriberSocket->deleteLater();
            m_subscriberSocket = nullptr;
        }
    });
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

void PrismLauncherManager::openPrismGUI()
{
    instance().openPrismGUIImpl();
}

void PrismLauncherManager::openPrismGUIImpl()
{
    if (!prismConfig) {
        LogManager::log("PrismLauncher config not set", LogManager::Error);
        return;
    }
    if (prismGUIProcess != nullptr && prismGUIProcess->state() == QProcess::Running) {
        LogManager::log("PrismLauncher GUI is already running", LogManager::Info);
        return;
    }
    launchPrismGUIImpl(nullptr);
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

bool PrismLauncherManager::isHookAvailable()
{
    return instance().m_hookAvailable;
}

qint64 PrismLauncherManager::getPrismGUIPid()
{
    if (instance().prismGUIProcess && instance().prismGUIProcess->state() == QProcess::Running) {
        return instance().prismGUIProcess->processId();
    }
    return 0;
}

QString PrismLauncherManager::hookSocketPath()
{
#ifdef Q_OS_WIN
    // QLocalSocket uses named pipes on Windows; connectToServer adds the \\.\pipe prefix
    return "mcbotmanager-prism-hook";
#else
    PrismConfig *cfg = instance().prismConfig;
    if (cfg && cfg->prismExecutable.contains("flatpak"))
        return cfg->prismPath + "/mcbotmanager-hook.sock";
    QByteArray xdg = qgetenv("XDG_RUNTIME_DIR");
    return (xdg.isEmpty() ? QString("/tmp") : QString::fromUtf8(xdg))
           + "/mcbotmanager-prism-hook";
#endif
}

void PrismLauncherManager::pingHook()
{
    auto* socket = new QLocalSocket(this);
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);

    auto markUnavailable = [this, socket, timer]() {
        timer->stop();
        socket->abort();
        socket->deleteLater();
        timer->deleteLater();
        if (m_hookAvailable) {
            m_hookAvailable = false;
            emit hookAvailabilityChanged(false);
        }
    };

    connect(timer, &QTimer::timeout, this, [this, markUnavailable]() {
        LogManager::log("Prism hook: no response to ping", LogManager::Warning);
        markUnavailable();
    });

    connect(socket, &QLocalSocket::connected, socket, [socket]() {
        socket->write("ping\n");
    });

    connect(socket, &QLocalSocket::readyRead, this, [this, socket, timer]() {
        if (socket->canReadLine()) {
            QString line = QString::fromUtf8(socket->readLine()).trimmed();
            if (line == "pong") {
                if (!m_hookAvailable) {
                    m_hookAvailable = true;
                    emit hookAvailabilityChanged(true);
                }
            } else {
                LogManager::log("Prism hook: unexpected ping response: " + line, LogManager::Warning);
            }
            timer->stop();
            socket->disconnectFromServer();
            socket->deleteLater();
            timer->deleteLater();
        }
    });

    connect(socket, &QLocalSocket::errorOccurred, this,
            [this, markUnavailable](QLocalSocket::LocalSocketError) {
        LogManager::log("Prism hook: not reachable", LogManager::Warning);
        markUnavailable();
    });

    socket->connectToServer(hookSocketPath());
    timer->start(5000);
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
        if (bot != nullptr) {
            LogManager::log("PrismLauncher GUI started, waiting for initialization...", LogManager::Info);
            QTimer::singleShot(2000, this, [this, bot]() { sendLaunchCommandImpl(bot); });
        } else {
            LogManager::log("PrismLauncher GUI started", LogManager::Info);
        }
        emit prismGUIStarted();

        if (prismConfig && prismConfig->useHook) {
            // Start hook heartbeat - first ping after 5s (hook init), then every 30s
            if (!hookHeartbeatTimer) {
                hookHeartbeatTimer = new QTimer(this);
                connect(hookHeartbeatTimer, &QTimer::timeout, this, &PrismLauncherManager::pingHook);
            }
            QTimer::singleShot(5000, this, [this]() {
                pingHook();
                hookHeartbeatTimer->start(30000);
            });
        }
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
                if (hookHeartbeatTimer) hookHeartbeatTimer->stop();
                if (m_hookAvailable) {
                    m_hookAvailable = false;
                    emit hookAvailabilityChanged(false);
                }
                m_currentlyRefreshingAccount.clear();
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
        if (hookHeartbeatTimer) hookHeartbeatTimer->stop();
        if (m_hookAvailable) {
            m_hookAvailable = false;
            emit hookAvailabilityChanged(false);
        }
        m_currentlyRefreshingAccount.clear();
        emit prismGUIStopped();
    });

    // Set memory config for all bots before Prism GUI starts so it reads correct values on load
    for (const BotInstance &b : BotManager::getBots()) {
        if (b.instance.isEmpty()) continue;
        QString cfgPath = prismConfig->prismPath + "/instances/" + b.instance + "/instance.cfg";
        QSettings cfg(cfgPath, QSettings::IniFormat);
        bool needsUpdate = cfg.value("MaxMemAlloc").toInt() != b.maxMemory
                           || !cfg.value("OverrideMemory").toBool();
        if (needsUpdate) {
            cfg.setValue("MaxMemAlloc", b.maxMemory);
            cfg.setValue("OverrideMemory", true);
            cfg.sync();
            LogManager::log(QString("Set MaxMemAlloc=%1 for instance '%2'").arg(b.maxMemory).arg(b.instance), LogManager::Info);
        }
    }

#ifndef Q_OS_WIN
    if (prismConfig->useHook) {
        bool isFlatpak = prismConfig->prismExecutable.contains("flatpak");
        QString hookLib;
        QString hookSocket;

        if (isFlatpak) {
            hookLib = prismConfig->prismPath + "/libprismhook.so";
            hookSocket = prismConfig->prismPath + "/mcbotmanager-hook.sock";
            
            QString srcLib = QCoreApplication::applicationDirPath() + "/libprismhook.so";
            QString srcCore = QCoreApplication::applicationDirPath() + "/libprismhook_core.so";
            
            auto copyIfChanged = [](const QString &src, const QString &dest) {
                if (!QFile::exists(src)) return;
                QFileInfo srcInfo(src), destInfo(dest);
                if (destInfo.exists()
                    && destInfo.size() == srcInfo.size()
                    && destInfo.lastModified() == srcInfo.lastModified()) return;
                QFile::remove(dest);
                QFile::copy(src, dest);
            };

            copyIfChanged(srcLib, hookLib);
            copyIfChanged(srcCore, prismConfig->prismPath + "/libprismhook_core.so");
        } else {
            hookLib = QCoreApplication::applicationDirPath() + "/libprismhook.so";
            hookSocket = hookSocketPath();
            // Ensure core exists in the same place
            QString srcCore = QCoreApplication::applicationDirPath() + "/libprismhook_core.so";
            if (!QFile::exists(srcCore)) {
                LogManager::log("Warning: libprismhook_core.so not found", LogManager::Warning);
            }
        }

        if (QFile::exists(hookLib)) {
            if (isFlatpak) {
                // Use --env for both variables. The hook itself now handles
                // being loaded into non-Qt processes (like bash) safely via dlsym.
                arguments.insert(arguments.size() - 1, "--env=LD_PRELOAD=" + hookLib);
                arguments.insert(arguments.size() - 1, "--env=MCBM_HOOK_SOCKET=" + hookSocket);
            } else {
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                QString existing = env.value("LD_PRELOAD");
                env.insert("LD_PRELOAD", existing.isEmpty() ? hookLib : existing + ":" + hookLib);
                env.insert("MCBM_HOOK_SOCKET", hookSocket);
                prismGUIProcess->setProcessEnvironment(env);
            }
        }
    }
#endif

#ifdef Q_OS_WIN
    if (prismConfig->useHook) {
        connect(prismGUIProcess, &QProcess::started, this, [this]() {
            injectHookDLL();
        });
    }
#endif

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

    if (!bot->server.isEmpty() && bot->autoConnect) {
        if (bot->proxySettings.enabled && bot->proxyHealth == BotInstance::ProxyHealth::Dead) {
            LogManager::log(QString("Skipping auto-connect for bot '%1': proxy is unreachable").arg(bot->name), LogManager::Warning);
        } else {
            arguments << "-s" << bot->server;
        }
    }

    bot->process = new QProcess(this);

    connect(bot->process, &QProcess::started, this, [bot]() {
        LogManager::log(QString("Sent launch command for bot '%1'").arg(bot->name), LogManager::Info);
    });

    connect(bot->process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [bot](int exitCode, QProcess::ExitStatus exitStatus) {
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

    connect(bot->process, &QProcess::errorOccurred, this, [bot](QProcess::ProcessError error) {
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

        if (cleanLine.contains("org.prismlauncher.EntryPoint")
            || cleanLine.contains("net.minecraft.client.main.Main")
            || cleanLine.contains("cpw.mods.modlauncher.Launcher")) {
            // Find the bot currently in Starting status (only one bot launches at a time)
            QVector<BotInstance> &bots = BotManager::getBots();
            for (const BotInstance &bot : bots) {
                if (bot.status == BotStatus::Starting) {
                    emit minecraftLaunching(bot.name);
                    break;
                }
            }
        }

        if (cleanLine.contains("Profile") && cleanLine.contains("is now in use")) {
            static QRegularExpression profileInUseReg("Profile \"([^\"]+)\" is now in use");
            QRegularExpressionMatch match = profileInUseReg.match(cleanLine);

            if (match.hasMatch()) {
                QString profileId = match.captured(1);

                QVector<BotInstance> &bots = BotManager::getBots();
                for (const BotInstance &bot : bots) {
                    if (bot.accountId == profileId && bot.status == BotStatus::Starting) {
                        emit minecraftStarting(bot.name);
                        break;
                    }
                }
            }
        }

        if (cleanLine.contains("Profile") && cleanLine.contains("is no longer in use")) {
            static QRegularExpression profileNotInUseReg("Profile \"([^\"]+)\" is no longer in use");
            QRegularExpressionMatch match = profileNotInUseReg.match(cleanLine);

            if (match.hasMatch()) {
                QString profileId = match.captured(1);

                QVector<BotInstance> &bots = BotManager::getBots();
                for (const BotInstance &bot : bots) {
                    if (bot.accountId == profileId) {
                        emit minecraftStopped(bot.name);
                        break;
                    }
                }
            }
        }

        // Fallback: detect "Process exited with code" for cases where the profile
        // message is never received (e.g. bot crashes before fully starting)
        if (cleanLine.contains("Process exited with code")) {
            QVector<BotInstance> &bots = BotManager::getBots();
            for (const BotInstance &bot : bots) {
                if (bot.status == BotStatus::Starting) {
                    emit minecraftStopped(bot.name);
                    break;
                }
            }
        }

        // Track Prism's background account refresh schedule
        if (cleanLine.contains("RefreshSchedule: Processing account")) {
            static QRegularExpression refreshStartReg(
                "RefreshSchedule: Processing account \"([^\"]+)\"");
            QRegularExpressionMatch m = refreshStartReg.match(cleanLine);
            if (m.hasMatch()) {
                m_currentlyRefreshingAccount = m.captured(1);
                emit accountRefreshStarted(m_currentlyRefreshingAccount);
            }
        }

        if (!m_currentlyRefreshingAccount.isEmpty()) {
            if (cleanLine.contains("RefreshSchedule: Background account refresh succeeded")) {
                emit accountRefreshSucceeded(m_currentlyRefreshingAccount);
                m_currentlyRefreshingAccount.clear();
            } else if (cleanLine.contains("RefreshSchedule: Background account refresh failed")) {
                emit accountRefreshFailed(m_currentlyRefreshingAccount);
                m_currentlyRefreshingAccount.clear();
            }
        }
    }
}

#ifdef Q_OS_WIN
void PrismLauncherManager::injectHookDLL()
{
    QString hookDll = QCoreApplication::applicationDirPath() + "/prismhook.dll";
    if (!QFile::exists(hookDll) || !prismGUIProcess) return;

    DWORD pid = (DWORD)prismGUIProcess->processId();
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        LogManager::log("Failed to open Prism process for hook injection", LogManager::Warning);
        return;
    }

    auto pathW = hookDll.toStdWString();
    size_t pathBytes = (pathW.size() + 1) * sizeof(wchar_t);
    LPVOID mem = VirtualAllocEx(hProcess, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (mem) {
        WriteProcessMemory(hProcess, mem, pathW.c_str(), pathBytes, nullptr);
        FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        HANDLE thread = CreateRemoteThread(hProcess, nullptr, 0,
                                           (LPTHREAD_START_ROUTINE)loadLib, mem, 0, nullptr);
        if (thread) {
            WaitForSingleObject(thread, 10000);
            CloseHandle(thread);
            LogManager::log("Prism hook DLL injected successfully", LogManager::Info);
        } else {
            LogManager::log("Failed to inject hook DLL into Prism", LogManager::Warning);
        }
        VirtualFreeEx(hProcess, mem, 0, MEM_RELEASE);
    }
    CloseHandle(hProcess);
}
#endif

void PrismLauncherManager::connectSubscriber()
{
    if (m_subscriberSocket) {
        m_subscriberSocket->abort();
        m_subscriberSocket->deleteLater();
        m_subscriberSocket = nullptr;
    }

    m_subscriberSocket = new QLocalSocket(this);

    connect(m_subscriberSocket, &QLocalSocket::connected, this, [this]() {
        m_subscriberSocket->write("subscribe\n");
    });

    connect(m_subscriberSocket, &QLocalSocket::readyRead, this,
            &PrismLauncherManager::handleSubscriberData);

    connect(m_subscriberSocket, &QLocalSocket::disconnected, this, [this]() {
        m_subscriberSocket->deleteLater();
        m_subscriberSocket = nullptr;
    });

    connect(m_subscriberSocket, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) {
        LogManager::log("[PrismHook]: " + m_subscriberSocket->errorString(), LogManager::Error);
    });

    m_subscriberSocket->connectToServer(hookSocketPath());
}

void PrismLauncherManager::handleSubscriberData()
{
    while (m_subscriberSocket && m_subscriberSocket->canReadLine()) {
        QString line = QString::fromUtf8(m_subscriberSocket->readLine()).trimmed();

        if (line == "accounts_changed") {
            m_collectingAccounts = true;
            m_pendingAccounts.clear();
        } else if (line == "accounts_end") {
            m_collectingAccounts = false;
            emit accountsUpdated(m_pendingAccounts);
        } else if (m_collectingAccounts && line.startsWith("account:")) {
            QStringList parts = line.mid(8).split('|');
            if (parts.size() == 3)
                m_pendingAccounts.append({parts[0], parts[1], parts[2]});
        } else if (line == "instances_changed") {
            m_collectingInstances = true;
            m_pendingInstances.clear();
        } else if (line == "instances_end") {
            m_collectingInstances = false;
            emit instancesUpdated(m_pendingInstances);
        } else if (m_collectingInstances && line.startsWith("instance:")) {
            QStringList parts = line.mid(9).split('|');
            if (parts.size() == 2)
                m_pendingInstances.append({parts[0], parts[1]});
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
