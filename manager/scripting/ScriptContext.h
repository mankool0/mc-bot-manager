#ifndef SCRIPTCONTEXT_H
#define SCRIPTCONTEXT_H

#include <QString>
#include <QDateTime>
#include <QMap>
#include <QList>
#include <QMutex>
#include <QQueue>
#include <QReadWriteLock>
#include <QVariantMap>
#include <QWaitCondition>

#undef slots
#include <pybind11/pybind11.h>
#define slots Q_SLOTS

namespace py = pybind11;

class ScriptThread;

// Suppress visibility warning: pybind11 types have hidden visibility
// See: https://github.com/pybind/pybind11/discussions/4862
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

struct ScriptContext {
    QString filename;
    QString code;
    bool enabled;
    bool running;
    QDateTime lastModified;
    QString lastError;

    py::dict globals;
    py::object mainModule;

    QMap<QString, QList<py::function>> eventHandlers;
    mutable QReadWriteLock handlersLock;

    // Message inbox for comms.receive(). Guarded by inboxMutex; inboxCond wakes
    // a blocked receive. inboxMutex is a leaf lock: never take another lock or
    // the GIL while holding it.
    static constexpr int kMaxInbox = 1000;
    QQueue<QVariantMap> inbox;
    QMutex inboxMutex;
    QWaitCondition inboxCond;
    int droppedMessages = 0;

    ScriptThread *thread = nullptr;

    ScriptContext() : enabled(false), running(false) {}
};

#pragma GCC diagnostic pop

#endif // SCRIPTCONTEXT_H
