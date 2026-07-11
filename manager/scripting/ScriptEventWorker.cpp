#include "ScriptEventWorker.h"
#include "ScriptContext.h"
#include "PythonAPI.h"
#include "scripting/ScriptEngine.h"

#undef slots
#include <pybind11/embed.h>
#define slots Q_SLOTS

#include <QReadLocker>

namespace py = pybind11;

ScriptEventWorker::ScriptEventWorker(ScriptEngine *engine, QObject *parent)
    : QObject(parent), engine(engine)
{}

void ScriptEventWorker::processEvent(const ScriptEvent &event, ScriptContext *ctx)
{
    // Lock order: handlersLock first, then GIL. Must be consistent with stopScript().
    QReadLocker locker(&ctx->handlersLock);

    if (!ctx->running)
        return;

    if (!ctx->eventHandlers.contains(event.eventName))
        return;

    const QList<py::function> handlers = ctx->eventHandlers.value(event.eventName);

    py::gil_scoped_acquire acquire;

    PythonAPI::setCurrentBot(event.botName);
    PythonAPI::setCurrentScript(event.scriptFilename);

    py::list pyArgs;
    try {
        if (event.argBuilder) {
            event.argBuilder(&pyArgs);
        } else {
            for (const QVariant &arg : event.args) {
                pyArgs.append(PythonAPI::qVariantToPyObject(arg));
            }
        }
    } catch (py::error_already_set &e) {
        engine->reportHandlerError(event.scriptFilename,
            QString("Error building args for '%1' event: %2")
                .arg(event.eventName, QString::fromStdString(e.what())));
        return;
    }

    for (const py::function &handler : handlers) {
        try {
            handler(*pyArgs);
        } catch (py::error_already_set &e) {
            engine->reportHandlerError(event.scriptFilename,
                QString("Event handler error in '%1': %2")
                    .arg(event.eventName, QString::fromStdString(e.what())));
        } catch (std::exception &e) {
            engine->reportHandlerError(event.scriptFilename,
                QString("Event handler exception: %1").arg(e.what()));
        }
    }
}
