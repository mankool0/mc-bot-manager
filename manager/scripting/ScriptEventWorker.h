#ifndef SCRIPTEVENTWORKER_H
#define SCRIPTEVENTWORKER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <functional>

struct ScriptContext;
class ScriptEngine;

struct ScriptEvent {
    QString scriptFilename;
    QString eventName;
    QVariantList args;
    QString botName; // empty for the global engine
    std::function<void(void*)> argBuilder;
};

Q_DECLARE_METATYPE(ScriptEvent)
Q_DECLARE_OPAQUE_POINTER(ScriptContext*)

class ScriptEventWorker : public QObject
{
    Q_OBJECT

public:
    explicit ScriptEventWorker(ScriptEngine *engine, QObject *parent = nullptr);

public slots:
    void processEvent(const ScriptEvent &event, ScriptContext *ctx);

private:
    ScriptEngine *engine;
};

#endif // SCRIPTEVENTWORKER_H
