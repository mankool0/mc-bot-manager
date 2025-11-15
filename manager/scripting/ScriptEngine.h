#ifndef SCRIPTENGINE_H
#define SCRIPTENGINE_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QVariantList>

#undef slots
#include <pybind11/embed.h>
#define slots Q_SLOTS

namespace py = pybind11;

struct BotInstance;
struct ScriptContext;

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

    QStringList getScriptNames() const;
    ScriptContext* getScript(const QString &filename);
    bool isScriptEnabled(const QString &filename) const;
    bool isScriptRunning(const QString &filename) const;
    QString getScriptError(const QString &filename) const;
    QString getBotName() const;

signals:
    void scriptLoaded(const QString &filename);
    void scriptUnloaded(const QString &filename);
    void scriptStarted(const QString &filename);
    void scriptStopped(const QString &filename);
    void scriptError(const QString &filename, const QString &error);
    void scriptOutput(const QString &filename, const QString &output);

private:
    BotInstance *botInstance;
    QMap<QString, ScriptContext*> scripts;

    static bool pythonInitialized;
    static int engineCount;
    static PyThreadState *mainThreadState;

    void initializePython();
    void cleanupPython();
    void setupPythonPath();
};

#endif // SCRIPTENGINE_H
