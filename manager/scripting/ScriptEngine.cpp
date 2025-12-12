#include "ScriptEngine.h"
#include "ScriptContext.h"
#include "ScriptThread.h"
#include "ScriptFileManager.h"
#include "PythonAPI.h"
#include "bot/BotManager.h"
#include "ui/BotConsoleWidget.h"
#include "logging/LogManager.h"
#include <QDebug>

namespace py = pybind11;

bool ScriptEngine::pythonInitialized = false;
int ScriptEngine::engineCount = 0;
PyThreadState *ScriptEngine::mainThreadState = nullptr;

ScriptEngine::ScriptEngine(BotInstance *bot, QObject *parent)
    : QObject(parent), botInstance(bot)
{
    if (engineCount == 0) {
        initializePython();
    }
    engineCount++;
}

ScriptEngine::~ScriptEngine()
{
    QMap<QString, ScriptState> states;
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        ScriptState state;
        state.autorun = it.value()->enabled;
        states[it.key()] = state;
    }
    ScriptFileManager::saveScriptStates(botInstance->name, states);

    stopAllScripts();

    {
        py::gil_scoped_acquire acquire;
        qDeleteAll(scripts);
        scripts.clear();
    }

    engineCount--;
    if (engineCount == 0) {
        cleanupPython();
    }
}

void ScriptEngine::loadScriptsFromDisk()
{
    QStringList scriptFiles = ScriptFileManager::listScripts(botInstance->name);
    QMap<QString, ScriptState> states = ScriptFileManager::loadScriptStates(botInstance->name);

    for (const QString &filename : std::as_const(scriptFiles)) {
        QString code = ScriptFileManager::loadScript(botInstance->name, filename);
        if (!code.isEmpty()) {
            loadScript(filename, code);

            if (states.contains(filename)) {
                enableScript(filename, states[filename].autorun);

                // Auto-run scripts that are set to autorun
                if (states[filename].autorun) {
                    runScript(filename);
                }
            }
        }
    }
}

void ScriptEngine::initializePython()
{
    if (pythonInitialized) return;

    try {
        py::initialize_interpreter();
        setupPythonPath();

        // Release the GIL so script threads can acquire it
        // The main thread doesn't need the GIL unless it's executing Python code
        mainThreadState = PyEval_SaveThread();

        pythonInitialized = true;
    } catch (py::error_already_set &e) {
        LogManager::log(QString("Failed to initialize Python: %1").arg(e.what()), LogManager::Error);
    }
}

void ScriptEngine::cleanupPython()
{
    if (!pythonInitialized) return;

    try {
        // Re-acquire the GIL before finalizing
        if (mainThreadState) {
            PyEval_RestoreThread(mainThreadState);
            mainThreadState = nullptr;
        }

        py::finalize_interpreter();
        pythonInitialized = false;
    } catch (py::error_already_set &e) {
        LogManager::log(QString("Error during Python cleanup: %1").arg(e.what()), LogManager::Error);
    }
}

void ScriptEngine::setupPythonPath()
{
    py::module_ sys = py::module_::import("sys");
    py::list path = sys.attr("path");

    // Add base scripts directory for shared modules between bots
    QString scriptsBaseDir = ScriptFileManager::getBaseScriptDir();
    path.append(scriptsBaseDir.toStdString());
}

bool ScriptEngine::loadScript(const QString &filename, const QString &code)
{
    bool wasEnabled = false;
    if (scripts.contains(filename)) {
        wasEnabled = scripts[filename]->enabled;
        unloadScript(filename);
    }

    py::gil_scoped_acquire acquire;

    ScriptContext *ctx = new ScriptContext();
    ctx->filename = filename;
    ctx->code = code;
    ctx->enabled = wasEnabled;
    ctx->running = false;

    scripts[filename] = ctx;
    emit scriptLoaded(filename);

    return true;
}

void ScriptEngine::unloadScript(const QString &filename)
{
    if (!scripts.contains(filename)) return;

    stopScript(filename);

    ScriptContext *ctx = scripts.take(filename);

    py::gil_scoped_acquire acquire;
    delete ctx;

    emit scriptUnloaded(filename);
}

bool ScriptEngine::reloadScript(const QString &filename)
{
    if (!scripts.contains(filename)) return false;

    ScriptContext *ctx = scripts[filename];
    return loadScript(filename, ctx->code);
}

void ScriptEngine::enableScript(const QString &filename, bool enabled)
{
    if (!scripts.contains(filename)) return;

    scripts[filename]->enabled = enabled;
}

bool ScriptEngine::runScript(const QString &filename)
{
    if (!scripts.contains(filename)) return false;

    ScriptContext *ctx = scripts[filename];
    if (ctx->running || ctx->thread) return false;

    if (botInstance->consoleWidget) {
        botInstance->consoleWidget->appendOutput(QString("[%1] Starting...").arg(filename),
                                                 Qt::darkCyan);
    }

    ctx->running = true;
    ctx->thread = new ScriptThread(ctx, botInstance, this);

    // Connect thread cleanup - delete when thread finishes naturally
    connect(ctx->thread, &QThread::finished, ctx->thread, &QObject::deleteLater, Qt::UniqueConnection);

    // Connect thread signals (use Qt::QueuedConnection to ensure GUI updates happen on main thread)
    connect(ctx->thread, &ScriptThread::scriptFinished, this, [this, filename](bool success) {
        if (!scripts.contains(filename)) return;

        ScriptContext *ctx = scripts[filename];
        ctx->thread = nullptr; // Don't delete here, finished signal handles it

        if (success) {
            if (ctx->eventHandlers.isEmpty()) {
                ctx->running = false;
                if (botInstance->consoleWidget) {
                    botInstance->consoleWidget
                        ->appendOutput(QString("[%1] Completed successfully").arg(filename),
                                       Qt::darkGreen);
                }
                emit scriptStopped(filename);
            } else {
                if (botInstance->consoleWidget) {
                    botInstance->consoleWidget
                        ->appendOutput(QString("[%1] Initialized, %2 event handler(s) registered")
                                           .arg(filename)
                                           .arg(ctx->eventHandlers.size()),
                                       Qt::darkGreen);
                }
            }
        } else {
            // Script stopped (error or user interruption)
            ctx->running = false;

            // Show stopped message if no error was set (user stopped)
            if (ctx->lastError.isEmpty() && botInstance->consoleWidget) {
                botInstance->consoleWidget
                    ->appendOutput(QString("[%1] Stopped").arg(filename),
                                   Qt::darkYellow);
            }

            emit scriptStopped(filename);
        }
    }, Qt::QueuedConnection);

    connect(ctx->thread, &ScriptThread::scriptError, this, [this, filename](const QString &error) {
        if (!scripts.contains(filename)) return;

        emit scriptError(filename, error);

        if (botInstance->consoleWidget) {
            botInstance->consoleWidget->appendOutput(
                QString("[Script Error in %1]").arg(filename),
                Qt::red
            );
            QStringList errorLines = error.split("\n");
            for (const QString &line : std::as_const(errorLines)) {
                botInstance->consoleWidget->appendOutput(line, Qt::red);
            }
        }
    }, Qt::QueuedConnection);

    ctx->thread->start();
    emit scriptStarted(filename);

    return true;
}

void ScriptEngine::stopScript(const QString &filename)
{
    if (!scripts.contains(filename)) return;

    ScriptContext *ctx = scripts[filename];
    if (!ctx->running) return;

    if (ctx->thread) {
        // Imperative script still running in thread - signal it to stop
        ctx->thread->stop();

        if (botInstance->consoleWidget) {
            QString msg = QString("[%1] Sending stop signal...").arg(filename);
            QMetaObject::invokeMethod(botInstance->consoleWidget, [widget = botInstance->consoleWidget, msg]() {
                widget->appendOutput(msg, Qt::darkYellow);
            }, Qt::QueuedConnection);
        }
    } else {
        // Event-driven script (thread finished but event handlers registered)
        ctx->running = false;

        {
            py::gil_scoped_acquire acquire;
            ctx->eventHandlers.clear();
        }

        if (botInstance->consoleWidget) {
            botInstance->consoleWidget->appendOutput(
                QString("[%1] Stopped").arg(filename), Qt::darkYellow);
        }

        emit scriptStopped(filename);
    }
}

void ScriptEngine::stopAllScripts()
{
    // Signal all scripts to stop
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        stopScript(it.key());
    }

    // Wait for all threads to finish
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        ScriptContext *ctx = it.value();
        if (ctx->thread && ctx->thread->isRunning()) {
            ctx->thread->wait();
        }
    }
}

void ScriptEngine::fireEvent(const QString &eventName, const QVariantList &args)
{
    py::gil_scoped_acquire acquire;

    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        ScriptContext *ctx = it.value();

        if (!ctx->running) {
            continue;
        }

        if (!ctx->eventHandlers.contains(eventName)) {
            continue;
        }

        const QList<py::function> &handlers = ctx->eventHandlers[eventName];
        for (const py::function &handler : handlers) {
            try {
                PythonAPI::setCurrentBot(botInstance->name);
                PythonAPI::setCurrentScript(ctx->filename);

                py::list pyArgs;
                for (const QVariant &arg : args) {
                    pyArgs.append(PythonAPI::qVariantToPyObject(arg));
                }

                handler(*pyArgs);

            } catch (py::error_already_set &e) {
                QString stringError = QString::fromStdString(e.what());

                emit scriptError(ctx->filename,
                                 QString("Event handler error in '%1': %2")
                                     .arg(eventName, stringError));

                if (botInstance->consoleWidget) {
                    QString headerMsg = QString("[Event Error in %1 - %2 handler]").arg(ctx->filename, eventName);
                    QStringList errorLines = stringError.split("\n");
                    QMetaObject::invokeMethod(botInstance->consoleWidget, [widget = botInstance->consoleWidget, headerMsg, errorLines]() {
                        widget->appendOutput(headerMsg, Qt::red);
                        for (const QString &line : errorLines) {
                            widget->appendOutput(line, Qt::red);
                        }
                    }, Qt::QueuedConnection);
                }
            } catch (std::exception &e) {
                QString error = QString("Event handler exception: %1").arg(e.what());
                emit scriptError(ctx->filename, error);
            }
        }
    }
}

QStringList ScriptEngine::getScriptNames() const
{
    return scripts.keys();
}

ScriptContext* ScriptEngine::getScript(const QString &filename)
{
    return scripts.value(filename, nullptr);
}

bool ScriptEngine::isScriptEnabled(const QString &filename) const
{
    if (!scripts.contains(filename)) return false;
    return scripts[filename]->enabled;
}

bool ScriptEngine::isScriptRunning(const QString &filename) const
{
    if (!scripts.contains(filename)) return false;
    return scripts[filename]->running;
}

QString ScriptEngine::getScriptError(const QString &filename) const
{
    if (!scripts.contains(filename)) return QString();
    return scripts[filename]->lastError;
}

QString ScriptEngine::getBotName() const
{
    return botInstance ? botInstance->name : QString();
}

