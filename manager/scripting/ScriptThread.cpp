#include "ScriptThread.h"
#include "ScriptContext.h"
#include "ScriptFileManager.h"
#include "EmbeddedPythonLibs.h"
#include "PythonAPI.h"
#include "bot/BotManager.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>

static const char* SCRIPT_SETUP_CODE = R"PY(
import sys
import utils
import ast as _ast

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

def __split_script_phases__(code_str, filename='<script>'):
    """Split into definitions (run first) and execution (run after handlers registered)."""
    tree = _ast.parse(code_str, filename)

    definitions = []
    execution = []

    for node in tree.body:
        if isinstance(node, (_ast.FunctionDef, _ast.AsyncFunctionDef, _ast.ClassDef,
                            _ast.Import, _ast.ImportFrom)):
            definitions.append(node)
        elif isinstance(node, _ast.If) and _is_main_guard(node):
            execution.extend(node.body)
        elif isinstance(node, (_ast.Assign, _ast.AnnAssign, _ast.AugAssign)):
            definitions.append(node)
        elif isinstance(node, _ast.Try) and _is_definition_try_block(node):
            definitions.append(node)
        elif isinstance(node, _ast.Expr) and isinstance(node.value, _ast.Constant) and \
             isinstance(node.value.value, str) and not definitions and not execution:
            definitions.append(node)
        else:
            execution.append(node)

    def_module = _ast.Module(body=definitions, type_ignores=[])
    _ast.fix_missing_locations(def_module)
    def_code = compile(def_module, filename, 'exec') if definitions else None

    exec_module = _ast.Module(body=execution, type_ignores=[])
    _ast.fix_missing_locations(exec_module)
    exec_code = compile(exec_module, filename, 'exec') if execution else None

    return def_code, exec_code

def _is_main_guard(node):
    if not isinstance(node, _ast.If):
        return False
    test = node.test
    if isinstance(test, _ast.Compare):
        if len(test.ops) == 1 and isinstance(test.ops[0], _ast.Eq):
            left = test.left
            right = test.comparators[0] if test.comparators else None
            if (_is_name_node(left) and _is_main_str(right)) or \
               (_is_main_str(left) and _is_name_node(right)):
                return True
    return False

def _is_name_node(node):
    return isinstance(node, _ast.Name) and node.id == '__name__'

def _is_main_str(node):
    return isinstance(node, _ast.Constant) and node.value == '__main__'

def _is_definition_try_block(node):
    def all_definitions(stmts):
        for stmt in stmts:
            if not isinstance(stmt, (_ast.Import, _ast.ImportFrom,
                                     _ast.Assign, _ast.AnnAssign, _ast.Pass,
                                     _ast.Expr, _ast.FunctionDef, _ast.AsyncFunctionDef,
                                     _ast.ClassDef)):
                return False
        return True

    return (all_definitions(node.body) and
            all_definitions(node.orelse) and
            all(all_definitions(h.body) for h in node.handlers))
)PY";

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

        // Add bot-specific directory to sys.path for bot-local imports
        QString scriptDir = ScriptFileManager::getScriptDirectory(botInstance->name);
        py::module_::import("sys").attr("path").attr("insert")(0, scriptDir.toStdString());

        // Evict bundled libs from sys.modules so disk changes are picked up on each run
        py::dict sysModules = py::module_::import("sys").attr("modules").cast<py::dict>();
        for (const QString &modName : EmbeddedPythonLibs::getBundledModules()) {
            py::str key(modName.toStdString());
            if (sysModules.contains(key)) {
                sysModules.attr("__delitem__")(key);
            }
        }

        scriptContext->globals = py::dict();
        scriptContext->globals["__builtins__"] = py::module_::import("builtins");
        scriptContext->globals["__name__"] = "__main__";

        scriptContext->globals["__check_stop__"] = py::cpp_function([this]() -> bool {
            return this->stopping.load();
        });

        py::exec(SCRIPT_SETUP_CODE, scriptContext->globals);

        // Split script and get Python's exec() for code objects
        py::object split_func = scriptContext->globals["__split_script_phases__"];
        py::tuple phases = split_func(scriptContext->code.toStdString(), scriptContext->filename.toStdString());
        py::object def_code = phases[0];
        py::object exec_code = phases[1];
        py::object py_exec = py::module_::import("builtins").attr("exec");

        // Execute definitions (registers event handlers via decorators)
        if (!def_code.is_none()) {
            py_exec(def_code, scriptContext->globals);
        }

        // Copy handlers to C++ before running imperative code
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

            if (!scriptContext->eventHandlers.isEmpty()) {
                emit scriptMessage(QString("[%1] Initialized, %2 event handler(s) registered")
                                       .arg(scriptContext->filename)
                                       .arg(scriptContext->eventHandlers.size()));
            }
        }

        // Execute imperative code (handlers already registered)
        if (!exec_code.is_none()) {
            py_exec(exec_code, scriptContext->globals);
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
