#include "ScriptThread.h"
#include "ScriptContext.h"
#include "ScriptFileManager.h"
#include "PythonAPI.h"
#include "bot/BotManager.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>

static const char* SCRIPT_SETUP_CODE = R"(
import sys
import utils

# Redirect stdout/stderr to capture print() statements
class ConsoleOutput:
    def __init__(self, is_error=False):
        self.is_error = is_error

    def write(self, text):
        if text and text.strip():
            if self.is_error:
                utils.error(text.rstrip())
            else:
                utils.log(text.rstrip())

    def flush(self):
        pass

sys.stdout = ConsoleOutput(is_error=False)
sys.stderr = ConsoleOutput(is_error=True)

# Set up trace function to check for stop signal
def _trace_for_stop(frame, event, arg):
    if event == 'line' and __check_stop__():
        raise KeyboardInterrupt("Script stopped by user")
    return _trace_for_stop

sys.settrace(_trace_for_stop)

# Event system
_event_handlers = {}

def on(event_name):
    def decorator(func):
        if event_name not in _event_handlers:
            _event_handlers[event_name] = []
        _event_handlers[event_name].append(func)
        return func
    return decorator
)";

ScriptThread::ScriptThread(ScriptContext *context, BotInstance *bot, QObject *parent)
    : QThread(parent), scriptContext(context), botInstance(bot), stopping(false)
{
}

ScriptThread::~ScriptThread()
{
    // By the time destructor is called, the thread should already be finished
    // So this ensures it's stopped
    if (isRunning()) {
        terminate();
        wait();
    }
}

void ScriptThread::stop()
{
    stopping.store(true);
}

void ScriptThread::run()
{
    // Helper to report error and finish
    auto reportError = [&](const QString& error) {
        scriptContext->lastError = error;
        emit scriptError(error);
        emit scriptFinished(false);
    };

    try {
        py::gil_scoped_acquire acquire;

        PythonAPI::setCurrentBot(botInstance->name);
        PythonAPI::setCurrentScript(scriptContext->filename);

        // Change working directory to the script's directory
        QString scriptDir = ScriptFileManager::getScriptDirectory(botInstance->name);
        py::module_::import("os").attr("chdir")(scriptDir.toStdString());

        // Add bot-specific directory to sys.path for bot-local imports
        py::module_::import("sys").attr("path").attr("insert")(0, scriptDir.toStdString());

        scriptContext->globals = py::dict();
        scriptContext->globals["__builtins__"] = py::module_::import("builtins");
        scriptContext->globals["__name__"] = "__main__";

        // Expose the stopping flag to Python via a callback
        scriptContext->globals["__check_stop__"] = py::cpp_function([this]() -> bool {
            return this->stopping.load();
        });

        // Set up stdout/stderr redirection, stop checking, and event system
        py::exec(SCRIPT_SETUP_CODE, scriptContext->globals);
        py::exec(scriptContext->code.toStdString(), scriptContext->globals);

        // Register event handlers
        if (scriptContext->globals.contains("_event_handlers")) {
            py::dict handlers = scriptContext->globals["_event_handlers"];
            scriptContext->eventHandlers.clear();

            for (auto item : handlers) {
                QString eventName = QString::fromStdString(py::str(item.first));
                py::list handlerList = item.second.cast<py::list>();

                QList<py::function> &funcList = scriptContext->eventHandlers[eventName];
                for (auto handler : handlerList) {
                    funcList.append(handler.cast<py::function>());
                }
            }
        }

        scriptContext->lastError.clear();
        emit scriptFinished(true);

    } catch (py::error_already_set &e) {
        // Handle KeyboardInterrupt from stop button
        if (e.matches(PyExc_KeyboardInterrupt) && stopping.load()) {
            scriptContext->lastError.clear();
            emit scriptFinished(false);
        } else {
            // Real error or real KeyboardInterrupt from script
            reportError(QString::fromStdString(e.what()));
        }
    } catch (std::exception &e) {
        reportError(QString("C++ exception: %1").arg(e.what()));
    }
}
