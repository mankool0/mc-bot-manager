#ifndef SCRIPTENGINE_H
#define SCRIPTENGINE_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QVariantList>
#include <QThread>
#include <QPointer>
#include "ScriptEventWorker.h"

#undef slots
#include <pybind11/embed.h>
#define slots Q_SLOTS

namespace py = pybind11;

struct BotInstance;
struct ScriptContext;
class BotConsoleWidget;

class ScriptEngine : public QObject
{
    Q_OBJECT

public:
    explicit ScriptEngine(BotInstance *bot, QObject *parent = nullptr);
    ~ScriptEngine();

    void loadScriptsFromDisk();

    bool loadScript(const QString &filename, const QString &code);
    void unloadScript(const QString &filename);
    bool reloadScript(const QString &filename);
    void enableScript(const QString &filename, bool enabled);

    bool runScript(const QString &filename);
    void stopScript(const QString &filename);
    void stopAllScripts();

    void fireEvent(const QString &eventName, const QVariantList &args);
    void fireEvent(const QString &eventName, std::function<void(void*)> argBuilder);

    QStringList getScriptNames() const;
    ScriptContext* getScript(const QString &filename);
    bool isScriptEnabled(const QString &filename) const;
    bool isScriptRunning(const QString &filename) const;
    QString getScriptError(const QString &filename) const;

    // Once set, the interpreter is never finalized: used when a blocked script
    // thread had to be abandoned at shutdown and may still be inside Python.
    static void setSkipPythonFinalize();

    // Storage/identity key for this engine's scripts. The bot name for a
    // bot-bound engine, or "_global" for the manager-wide engine.
    QString getScopeName() const;
    bool isGlobal() const { return botInstance == nullptr; }

    // Console this engine's output goes to. A bot-bound engine uses its bot's
    // console; the global engine's is settable.
    void setConsole(BotConsoleWidget *console);

    QString loadEventData();

signals:
    void scriptLoaded(const QString &filename);
    void scriptUnloaded(const QString &filename);
    void scriptStarted(const QString &filename);
    void scriptStopped(const QString &filename);
    void scriptError(const QString &filename, const QString &error);
    void scriptOutput(const QString &filename, const QString &output);
    void eventReady(const ScriptEvent &event, ScriptContext *ctx);

private:
    BotInstance *botInstance;
    QString m_scopeName;
    QPointer<BotConsoleWidget> m_globalConsole;
    QMap<QString, ScriptContext*> scripts;

    QThread *m_eventWorkerThread;
    ScriptEventWorker *m_eventWorker;

    BotConsoleWidget* console() const;

    static bool pythonInitialized;
    static bool skipPythonFinalize;
    static int engineCount;
    static PyThreadState *mainThreadState;

    void initializePython();
    void cleanupPython();
    void setupPythonPath();
};

#endif // SCRIPTENGINE_H
