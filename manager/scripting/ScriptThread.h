#ifndef SCRIPTTHREAD_H
#define SCRIPTTHREAD_H

#include <QThread>
#include <QString>
#include <QMutex>
#include <atomic>

#undef slots
#include <pybind11/embed.h>
#define slots Q_SLOTS

namespace py = pybind11;

struct ScriptContext;
struct BotInstance;

class ScriptThread : public QThread
{
    Q_OBJECT

public:
    explicit ScriptThread(ScriptContext *context, BotInstance *bot, QObject *parent = nullptr);
    ~ScriptThread() override;

    void stop();
    bool isStopping() const { return stopping.load(); }

signals:
    void scriptFinished(bool success);
    void scriptError(const QString &error);
    void scriptMessage(const QString &message);

protected:
    void run() override;

private:
    ScriptContext *scriptContext;
    BotInstance *botInstance;
    std::atomic<bool> stopping;
};

#endif // SCRIPTTHREAD_H
