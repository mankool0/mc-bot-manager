#ifndef SCRIPTEVENTWORKER_H
#define SCRIPTEVENTWORKER_H

#include <QObject>
#include <QString>
#include <QVariantList>

struct BotInstance;
struct ScriptContext;

struct ScriptEvent {
    QString scriptFilename;
    QString eventName;
    QVariantList args;
    QString botName;
};

Q_DECLARE_METATYPE(ScriptEvent)
Q_DECLARE_OPAQUE_POINTER(ScriptContext*)

class ScriptEventWorker : public QObject
{
    Q_OBJECT

public:
    explicit ScriptEventWorker(BotInstance *bot, QObject *parent = nullptr);

public slots:
    void processEvent(const ScriptEvent &event, ScriptContext *ctx);

private:
    BotInstance *botInstance;
};

#endif // SCRIPTEVENTWORKER_H
