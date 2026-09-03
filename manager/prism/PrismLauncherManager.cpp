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
#include <QPointer>
#include <QThread>
#include <utility>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// Every public entry point can be reached from a script thread (PythonAPI's
// bot.start() runs on ScriptThread) and QProcess may only be touched from the
// thread its owner lives in - one created on a blocked script thread never
// leaves QProcess::Starting.
template <typename Fn>
bool deferToOwnThread(PrismLauncherManager *mgr, Fn &&fn)
{
    if (QThread::currentThread() == mgr->thread()) return false;
    QMetaObject::invokeMethod(mgr, std::forward<Fn>(fn), Qt::QueuedConnection);
    return true;
}

// How often the launch queue checks whether the bot in flight has left
// Starting. Polled because the status is flipped from several places and not
// all of them announce it.
constexpr int kLaunchQueuePollMs = 500;

} // namespace

PrismLauncherManager& PrismLauncherManager::instance()
{
    static PrismLauncherManager instance;
    return instance;
}

PrismLauncherManager::PrismLauncherManager(QObject *parent)
    : QObject(parent)
{
    m_launchQueueTimer = new QTimer(this);
    m_launchQueueTimer->setInterval(kLaunchQueuePollMs);
    connect(m_launchQueueTimer, &QTimer::timeout, this, &PrismLauncherManager::pumpLaunchQueue);

    connect(this, &PrismLauncherManager::hookAvailabilityChanged, this, [this](bool available) {
        if (available) {
            connectSubscriber();
        } else {
            dropSubscriberSocket();
        }
    });
}

PrismLauncherManager::~PrismLauncherManager()
{
    // Normally a no-op: ManagerMainWindow's destructor already stopped it
    stopPrismGUI();
}

void PrismLauncherManager::setPrismConfig(PrismConfig *config)
{
    instance().prismConfig = config;
}

void PrismLauncherManager::launchBot(BotInstance *bot)
{
    if (!bot) return;
    // Resolved again on the other side rather than captured: the queued call
    // runs later, and a bot removed in between would leave a dangling pointer.
    const QString name = bot->name;
    if (deferToOwnThread(&instance(), [name]() {
            if (BotInstance *b = BotManager::getBotByName(name)) {
                instance().launchBotImpl(b);
            }
        })) {
        return;
    }
    instance().launchBotImpl(bot);
}

void PrismLauncherManager::launchBotImpl(BotInstance *bot)
{
    if (!prismConfig) {
        failLaunch(bot, "PrismLauncher config not set");
        return;
    }

    if (prismGUIProcess == nullptr || prismGUIProcess->state() == QProcess::NotRunning) {
        launchPrismGUIImpl(bot);
        return;
    }

    // A launcher is up or on its way up. Starting a second one would give a
    // fleet one `flatpak run` each, all racing for the same instance directory.
    queueLaunch(bot);
    pumpLaunchQueue();
}

void PrismLauncherManager::failLaunch(BotInstance *bot, const QString &reason)
{
    if (!bot) return;
    LogManager::log(QString("[%1] Launch failed: %2").arg(bot->name, reason), LogManager::Error);
    if (bot->status == BotStatus::Starting) {
        bot->status = BotStatus::Error;
        emit BotManager::instance().botUpdated(bot->name);
    }
}

void PrismLauncherManager::queueLaunch(BotInstance *bot)
{
    if (!bot || m_launchQueue.contains(bot->name) || m_inFlightLaunch == bot->name) return;
    m_launchQueue.append(bot->name);
    if (!m_guiReady) {
        LogManager::log(QString("[%1] Waiting for the PrismLauncher GUI before launching (%2 queued)")
                            .arg(bot->name).arg(m_launchQueue.size()),
                        LogManager::Info);
    } else if (!m_inFlightLaunch.isEmpty()) {
        LogManager::log(QString("[%1] Queued for launch behind '%2' (position %3)")
                            .arg(bot->name, m_inFlightLaunch).arg(m_launchQueue.size()),
                        LogManager::Info);
    }
    m_launchQueueTimer->start();
    emit launchQueueChanged();
}

void PrismLauncherManager::pumpLaunchQueue()
{
    bool changed = false;

    if (!m_inFlightLaunch.isEmpty()) {
        BotInstance *inFlight = BotManager::getBotByName(m_inFlightLaunch);
        if (inFlight && inFlight->status == BotStatus::Starting) return;
        if (inFlight && inFlight->status == BotStatus::Online) {
            LogManager::log(QString("[%1] Connected, launch queue continues").arg(m_inFlightLaunch),
                            LogManager::Info);
        } else if (!m_launchQueue.isEmpty()) {
            LogManager::log(QString("[%1] Did not come up, launch queue continues").arg(m_inFlightLaunch),
                            LogManager::Warning);
        }
        m_inFlightLaunch.clear();
        changed = true;
    }

    if (m_guiReady && prismGUIProcess && prismGUIProcess->state() == QProcess::Running) {
        while (!m_launchQueue.isEmpty()) {
            const QString name = m_launchQueue.takeFirst();
            changed = true;
            BotInstance *bot = BotManager::getBotByName(name);
            // Anything but Starting means the wait outlived the request: the
            // bot connected on its own, was stopped, or timed out.
            if (!bot || bot->status != BotStatus::Starting) continue;
            m_inFlightLaunch = name;
            sendLaunchCommandImpl(bot);
            // Armed here rather than when the launch was asked for: a bot at
            // the back of a long queue would time out before its turn.
            BotManager::armStartupTimeout(name);
            break;
        }
    }

    if (m_launchQueue.isEmpty() && m_inFlightLaunch.isEmpty()) {
        m_launchQueueTimer->stop();
    }
    if (changed) emit launchQueueChanged();
}

bool PrismLauncherManager::inFlightStillStarting() const
{
    if (m_inFlightLaunch.isEmpty()) return false;
    const BotInstance *bot = BotManager::getBotByName(m_inFlightLaunch);
    return bot && bot->status == BotStatus::Starting;
}

int PrismLauncherManager::launchQueuePosition(const QString &botName)
{
    return instance().m_launchQueue.indexOf(botName) + 1;
}

void PrismLauncherManager::clearGUIState()
{
    m_guiReady = false;
    m_launchQueueTimer->stop();
    // The bot in flight keeps its own startup timeout; the ones still waiting
    // have none yet and would sit in Starting forever.
    m_inFlightLaunch.clear();
    if (!m_launchQueue.isEmpty()) {
        LogManager::log(QString("PrismLauncher GUI is gone - dropping %1 queued launch(es)")
                            .arg(m_launchQueue.size()),
                        LogManager::Warning);
        const QStringList dropped = std::move(m_launchQueue);
        m_launchQueue.clear();
        for (const QString &name : dropped) {
            failLaunch(BotManager::getBotByName(name), "PrismLauncher GUI is gone");
        }
    }
    emit launchQueueChanged();
    if (hookHeartbeatTimer) hookHeartbeatTimer->stop();
    if (m_hookAvailable) {
        m_hookAvailable = false;
        emit hookAvailabilityChanged(false);
    }
    m_currentlyRefreshingAccount.clear();
}

void PrismLauncherManager::openPrismGUI()
{
    if (deferToOwnThread(&instance(), []() { instance().openPrismGUIImpl(); })) return;
    instance().openPrismGUIImpl();
}

void PrismLauncherManager::openPrismGUIImpl()
{
    if (!prismConfig) {
        LogManager::log("PrismLauncher config not set", LogManager::Error);
        return;
    }
    if (prismGUIProcess != nullptr && prismGUIProcess->state() != QProcess::NotRunning) {
        // A second Prism process with no arguments hands an "activate" message to the running
        // one, which restores and raises its main window (needed once windows start minimized).
        // On Wayland the compositor only lets it flag the taskbar entry.
        QString prismExe;
        QStringList arguments;
        parsePrismCommand(prismConfig->prismExecutable, prismExe, arguments);
        LogManager::log("PrismLauncher GUI is already running, asking it to show its window", LogManager::Info);
        QProcess::startDetached(prismExe, arguments);
        return;
    }
    launchPrismGUIImpl(nullptr);
}

void PrismLauncherManager::stopPrismGUI()
{
    if (deferToOwnThread(&instance(), []() { instance().stopPrismGUIImpl(); })) return;
    instance().stopPrismGUIImpl();
}

void PrismLauncherManager::stopPrismGUIImpl()
{
    if (prismGUIProcess) {
        prismGUIProcess->disconnect();

        // Kill the whole process group so the child processes go too
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
        clearGUIState();
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
    if (cfg && cfg->prismExecutable.contains("flatpak")) {
        return cfg->prismPath + "/mcbotmanager-hook.sock";
    }
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

    connect(timer, &QTimer::timeout, this, [markUnavailable]() {
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
            [markUnavailable](QLocalSocket::LocalSocketError) {
        LogManager::log("Prism hook: not reachable", LogManager::Warning);
        markUnavailable();
    });

    socket->connectToServer(hookSocketPath());
    timer->start(5000);
}

void PrismLauncherManager::stopBot(qint64 minecraftPid)
{
    if (deferToOwnThread(&instance(), [minecraftPid]() {
            instance().stopBotImpl(minecraftPid);
        })) {
        return;
    }
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
        // Disconnected first: a replaced process keeps emitting until it is
        // actually deleted, and would tear down the state of its replacement.
        prismGUIProcess->disconnect(this);
        prismGUIProcess->deleteLater();
    }

    m_guiReady = false;
    prismGUIProcess = new QProcess(this);
    // Handlers below hold this QPointer rather than reading the member: a
    // queued signal from a replaced process must not touch its successor.
    QPointer<QProcess> proc = prismGUIProcess;
    if (bot != nullptr) {
        queueLaunch(bot);
    }

    connect(prismGUIProcess, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        if (!proc) return;
        QByteArray data = proc->readAllStandardOutput();
        processOutput(QString::fromUtf8(data), false);
    });

    connect(prismGUIProcess, &QProcess::readyReadStandardError, this, [this, proc]() {
        if (!proc) return;
        QByteArray data = proc->readAllStandardError();
        processOutput(QString::fromUtf8(data), true);
    });

    // `!proc` first everywhere below: comparing proc to the member alone would
    // pass when both are null (destroyed process, or member already cleared).
    connect(prismGUIProcess, &QProcess::started, this, [this, proc]() {
        if (!proc || proc != prismGUIProcess) return;
        if (!m_launchQueue.isEmpty()) {
            LogManager::log("PrismLauncher GUI started, waiting for initialization...", LogManager::Info);
        } else {
            LogManager::log("PrismLauncher GUI started", LogManager::Info);
        }
        QTimer::singleShot(2000, this, [this, proc]() {
            if (!proc || proc != prismGUIProcess) return;
            m_guiReady = true;
            pumpLaunchQueue();
        });
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
            [this, proc](int exitCode, QProcess::ExitStatus exitStatus) {
                if (!proc || proc != prismGUIProcess) return;
                if (exitStatus == QProcess::CrashExit) {
                    LogManager::log(QString("PrismLauncher GUI crashed (exit code: %1)").arg(exitCode),
                              LogManager::Error);
                } else {
                    LogManager::log(QString("PrismLauncher GUI exited normally (exit code: %1)")
                                        .arg(exitCode),
                                    LogManager::Info);
                }
                prismGUIProcess = nullptr;
                proc->deleteLater();
                clearGUIState();
                emit prismGUIStopped();
            });

    connect(prismGUIProcess, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (!proc || proc != prismGUIProcess) return;
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
        // Read/write errors are reported against a launcher that is still up;
        // forgetting the process there makes the next launch start a second GUI.
        if (proc->state() != QProcess::NotRunning) return;
        prismGUIProcess = nullptr;
        proc->deleteLater();
        clearGUIState();
        emit prismGUIStopped();
    });

    // Set memory config for all bots before Prism GUI starts so it reads correct values on load
    for (const BotInstance *b : std::as_const(BotManager::getBots())) {
        if (b->instance.isEmpty()) continue;
        QString cfgPath = prismConfig->prismPath + "/instances/" + b->instance + "/instance.cfg";
        QSettings cfg(cfgPath, QSettings::IniFormat);
        bool needsUpdate = cfg.value("MaxMemAlloc").toInt() != b->maxMemory
                           || !cfg.value("OverrideMemory").toBool();
        if (needsUpdate) {
            cfg.setValue("MaxMemAlloc", b->maxMemory);
            cfg.setValue("OverrideMemory", true);
            cfg.sync();
            LogManager::log(QString("Set MaxMemAlloc=%1 for instance '%2'").arg(b->maxMemory).arg(b->instance), LogManager::Info);
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
                // Safe to set for the whole sandbox: the hook handles being
                // loaded into non-Qt processes (like bash) via dlsym.
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

    if (prismConfig->useHook && prismConfig->minimizeWindows) {
        // Read by the hook. Plain variables reach Prism inside a Flatpak sandbox too (only
        // LD_PRELOAD and friends are filtered, hence the --env above for those).
        QProcessEnvironment env = prismGUIProcess->processEnvironment();
        if (env.isEmpty())
            env = QProcessEnvironment::systemEnvironment();
        env.insert("MCBM_PRISM_WINDOWS", "minimized");
        prismGUIProcess->setProcessEnvironment(env);
    }

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

    arguments << "-l" << bot->instance;
    arguments << "-a" << bot->account;

    if (!bot->server.isEmpty() && bot->autoConnect) {
        if (bot->proxySettings.enabled && bot->proxyHealth == BotInstance::ProxyHealth::Dead) {
            LogManager::log(QString("Skipping auto-connect for bot '%1': proxy is unreachable").arg(bot->name), LogManager::Warning);
        } else {
            arguments << "-s" << bot->server;
        }
    }

    // A launch still in flight for this bot (double-click, restart) would
    // otherwise be overwritten, and its handlers would tear down the new one.
    dropLaunchProcess(bot);

    QProcess *process = new QProcess(this);
    bot->process = process;
    QPointer<QProcess> proc = process;
    // The handlers resolve the bot by name rather than capturing the pointer:
    // the bot can be removed while the launch command is still running.
    const QString name = bot->name;

    connect(process, &QProcess::started, this, [name]() {
        LogManager::log(QString("Sent launch command for bot '%1'").arg(name), LogManager::Info);
    });

    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [name, proc](int exitCode, QProcess::ExitStatus exitStatus) {
                if (!proc) return;
                BotInstance *bot = BotManager::getBotByName(name);
                if (exitStatus == QProcess::CrashExit) {
                    LogManager::log(QString("Launch command for bot '%1' crashed (exit code: %2)")
                                        .arg(name)
                                        .arg(exitCode),
                                    LogManager::Error);
                    if (bot) {
                        bot->status = BotStatus::Offline;
                        bot->minecraftPid = 0;
                    }
                } else {
                    LogManager::log(QString("Launch command for bot '%1' completed (exit code: %2)")
                                        .arg(name)
                                        .arg(exitCode),
                                    LogManager::Info);
                }
                launchProcessDone(bot, proc);
            });

    connect(process, &QProcess::errorOccurred, this, [name, proc](QProcess::ProcessError error) {
        if (!proc) return;
        QString errorMsg;
        switch (error) {
        case QProcess::FailedToStart:
            errorMsg = QString("Failed to send launch command for bot '%1'").arg(name);
            break;
        case QProcess::Crashed:
            errorMsg = QString("Launch command crashed for bot '%1'").arg(name);
            break;
        default:
            errorMsg = QString("Unknown error occurred while launching bot '%1'").arg(name);
        }
        LogManager::log(errorMsg, LogManager::Error);

        // A crash also reaches finished(CrashExit) and read/write errors are
        // reported while the command still runs; only a failed start gets here.
        if (proc->state() != QProcess::NotRunning) return;
        BotInstance *bot = BotManager::getBotByName(name);
        if (bot) {
            bot->status = BotStatus::Offline;
            bot->minecraftPid = 0;
        }
        launchProcessDone(bot, proc);
    });

    LogManager::log(QString("Executing launch command: %1 %2").arg(prismExe, arguments.join(" ")),
                    LogManager::Info);
    process->start(prismExe, arguments);
}

// Idempotent: both handlers above can reach it for one exit (errorOccurred then
// finished), so the member is only cleared when it still points at this process.
void PrismLauncherManager::launchProcessDone(BotInstance *bot, QProcess *process)
{
    if (!process) return;
    if (bot && bot->process == process) {
        bot->process = nullptr;
    }
    process->disconnect();
    process->deleteLater();
}

void PrismLauncherManager::dropLaunchProcess(BotInstance *bot)
{
    if (!bot) return;
    QProcess *process = std::exchange(bot->process, nullptr);
    if (!process) return;
    process->disconnect();
    if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
    } else {
        // Not killed: the command is a short-lived forwarder to the running
        // launcher, so let it finish the handoff and clean itself up.
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                process, &QObject::deleteLater);
    }
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
            // Only the bot in flight has a launch command out; queued bots
            // are Starting too but nothing of theirs is running yet.
            if (inFlightStillStarting()) {
                emit minecraftLaunching(m_inFlightLaunch);
            }
        }

        if (cleanLine.contains("Profile") && cleanLine.contains("is now in use")) {
            static QRegularExpression profileInUseReg("Profile \"([^\"]+)\" is now in use");
            QRegularExpressionMatch match = profileInUseReg.match(cleanLine);

            if (match.hasMatch()) {
                QString profileId = match.captured(1);

                QVector<BotInstance*> &bots = BotManager::getBots();
                for (const BotInstance *bot : bots) {
                    if (bot->accountId == profileId && bot->status == BotStatus::Starting) {
                        emit minecraftStarting(bot->name);
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

                QVector<BotInstance*> &bots = BotManager::getBots();
                for (const BotInstance *bot : bots) {
                    if (bot->accountId == profileId) {
                        emit minecraftStopped(bot->name);
                        break;
                    }
                }
            }
        }

        // Fallback: detect "Process exited with code" for cases where the profile
        // message is never received (e.g. bot crashes before fully starting)
        if (cleanLine.contains("Process exited with code")) {
            if (inFlightStillStarting()) {
                emit minecraftStopped(m_inFlightLaunch);
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

void PrismLauncherManager::dropSubscriberSocket()
{
    // Taken out of the member before abort(): abort() emits disconnected()
    // synchronously on a connected socket, and that handler clears the member.
    QLocalSocket *socket = std::exchange(m_subscriberSocket, nullptr);
    if (!socket) return;
    socket->abort();
    socket->deleteLater();
}

void PrismLauncherManager::connectSubscriber()
{
    dropSubscriberSocket();

    m_subscriberSocket = new QLocalSocket(this);

    connect(m_subscriberSocket, &QLocalSocket::connected, this, [this]() {
        m_subscriberSocket->write("subscribe\n");
    });

    connect(m_subscriberSocket, &QLocalSocket::readyRead, this,
            &PrismLauncherManager::handleSubscriberData);

    connect(m_subscriberSocket, &QLocalSocket::disconnected, this, [this]() {
        // Also reached re-entrantly from dropSubscriberSocket(), with the
        // member already cleared.
        if (QLocalSocket *socket = std::exchange(m_subscriberSocket, nullptr)) {
            socket->deleteLater();
        }
    });

    connect(m_subscriberSocket, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) {
        if (m_subscriberSocket) {
            LogManager::log("[PrismHook]: " + m_subscriberSocket->errorString(), LogManager::Error);
        }
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
            if (parts.size() == 3) {
                m_pendingAccounts.append({parts[0], parts[1], parts[2]});
            }
        } else if (line == "instances_changed") {
            m_collectingInstances = true;
            m_pendingInstances.clear();
        } else if (line == "instances_end") {
            m_collectingInstances = false;
            emit instancesUpdated(m_pendingInstances);
        } else if (m_collectingInstances && line.startsWith("instance:")) {
            QStringList parts = line.mid(9).split('|');
            if (parts.size() == 2) {
                m_pendingInstances.append({parts[0], parts[1]});
            }
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
