#include "ScriptEventWorker.h"
#include "ScriptContext.h"
#include "PythonAPI.h"
#include "bot/BotManager.h"
#include "ui/BotConsoleWidget.h"
#include "scripting/ScriptEngine.h"

#undef slots
#include <pybind11/embed.h>
#define slots Q_SLOTS

#include <QReadLocker>
#include <QMetaObject>
#include <QDateTime>

namespace py = pybind11;

ScriptEventWorker::ScriptEventWorker(BotInstance *bot, QObject *parent)
    : QObject(parent), botInstance(bot)
{}

void ScriptEventWorker::processEvent(const ScriptEvent &event, ScriptContext *ctx)
{
    // Lock order: handlersLock first, then GIL. Must be consistent with stopScript().
    QReadLocker locker(&ctx->handlersLock);

    if (!ctx->running)
        return;

    if (!ctx->eventHandlers.contains(event.eventName))
        return;

    const QList<py::function> &handlers = ctx->eventHandlers[event.eventName];

    py::gil_scoped_acquire acquire;

    PythonAPI::setCurrentBot(event.botName);
    PythonAPI::setCurrentScript(event.scriptFilename);

    py::list pyArgs;
    for (const QVariant &arg : event.args) {
        pyArgs.append(PythonAPI::qVariantToPyObject(arg));
    }

    for (const py::function &handler : handlers) {
        try {
            handler(*pyArgs);
        } catch (py::error_already_set &e) {
            QString stringError = QString::fromStdString(e.what());

            QMetaObject::invokeMethod(botInstance->scriptEngine,
                [engine = botInstance->scriptEngine,
                 filename = event.scriptFilename,
                 eventName = event.eventName,
                 stringError]() {
                    emit engine->scriptError(
                        filename,
                        QString("Event handler error in '%1': %2").arg(eventName, stringError));
                }, Qt::QueuedConnection);

            if (botInstance->consoleWidget) {
                QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
                QString headerMsg = QString("[%1] [Event Error in %2 - %3 handler]")
                                        .arg(ts, event.scriptFilename, event.eventName);
                QStringList errorLines = stringError.split("\n");
                QMetaObject::invokeMethod(botInstance->consoleWidget,
                    [widget = botInstance->consoleWidget, headerMsg, errorLines]() {
                        widget->appendOutput(headerMsg, Qt::red);
                        for (const QString &line : errorLines) {
                            widget->appendOutput(line, Qt::red);
                        }
                    }, Qt::QueuedConnection);
            }
        } catch (std::exception &e) {
            QString error = QString("Event handler exception: %1").arg(e.what());
            QMetaObject::invokeMethod(botInstance->scriptEngine,
                [engine = botInstance->scriptEngine,
                 filename = event.scriptFilename, error]() {
                    emit engine->scriptError(filename, error);
                }, Qt::QueuedConnection);
        }
    }
}
