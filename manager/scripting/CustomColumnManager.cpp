#include "CustomColumnManager.h"
#include "PythonAPI.h"
#include "bot/BotManager.h"
#include "logging/LogManager.h"

#undef slots
#include <pybind11/embed.h>
#define slots Q_SLOTS

#include <QMetaObject>
#include <algorithm>
#include <thread>

namespace py = pybind11;

// How long to wait for the worker to finish on its own, then for the interrupt
// to land, before abandoning the thread.
static const int kShutdownGraceMs = 500;
static const int kShutdownInterruptMs = 2500;

// Treat a column as due slightly early so a pass that finishes just after a
// tick does not slip a whole interval to the next one.
static qint64 dueToleranceMs(int intervalMs)
{
    return qMin(intervalMs / 10, CustomColumnManager::kMinIntervalMs);
}

ColumnComputeWorker::ColumnComputeWorker(CustomColumnManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
}

void ColumnComputeWorker::compute(const QStringList &botNames)
{
    m_manager->computeSnapshot(botNames);
}

void ColumnComputeWorker::drainGraveyard()
{
    m_manager->drainGraveyard();
}

CustomColumnManager &CustomColumnManager::instance()
{
    static CustomColumnManager s_instance;
    return s_instance;
}

CustomColumnManager::CustomColumnManager(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_workerThread = new QThread(this);
    m_worker = new ColumnComputeWorker(this);
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_workerThread->start();
}

CustomColumnManager::~CustomColumnManager()
{
    // shutdown() normally ran already, leaving this a no-op (m_workerThread is
    // null if the worker had to be abandoned).
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void CustomColumnManager::registerColumn(const QString &name, const py::function &provider, const QString &scriptFile,
                                         int intervalMs)
{
    intervalMs = qMax(intervalMs, kMinIntervalMs);

    // Caller holds the GIL (needed to copy the provider into the shared_ptr).
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_shuttingDown.load())
            return;

        bool replaced = false;
        for (ColumnDef &def : m_columns) {
            if (def.name == name) {
                def.provider = std::make_shared<py::function>(provider);
                def.scriptFile = scriptFile;
                // An interval change must reach the compute-tick timer via columnsChanged.
                changed = def.intervalMs != intervalMs;
                def.intervalMs = intervalMs;
                def.nextDueMs = 0; // new provider: fill immediately
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_columns.append({name, std::make_shared<py::function>(provider), scriptFile, intervalMs, 0});
            changed = true;
        }
    }

    if (changed)
        emit columnsChanged();
}

void CustomColumnManager::unregisterColumn(const QString &name)
{
    bool removed = false;
    {
        QMutexLocker locker(&m_mutex);
        for (int i = 0; i < m_columns.size(); ++i) {
            if (m_columns[i].name == name) {
                m_graveyard.append(std::move(m_columns[i]));
                m_columns.removeAt(i);
                removed = true;
                break;
            }
        }
    }

    if (removed) {
        scheduleGraveyardDrain();
        emit columnsChanged();
    }
}

void CustomColumnManager::unregisterScript(const QString &scriptFile)
{
    bool removed = false;
    {
        QMutexLocker locker(&m_mutex);
        for (int i = m_columns.size() - 1; i >= 0; --i) {
            if (m_columns[i].scriptFile == scriptFile) {
                m_graveyard.append(std::move(m_columns[i]));
                m_columns.removeAt(i);
                removed = true;
            }
        }
    }

    if (removed) {
        scheduleGraveyardDrain();
        emit columnsChanged();
    }
}

void CustomColumnManager::scheduleGraveyardDrain()
{
    // During shutdown the drain is done by shutdown() itself (or the defs are
    // deliberately leaked if the worker had to be abandoned).
    if (m_shuttingDown.load() || !m_worker)
        return;
    QMetaObject::invokeMethod(m_worker, &ColumnComputeWorker::drainGraveyard,
                              Qt::QueuedConnection);
}

void CustomColumnManager::drainGraveyard()
{
    QList<ColumnDef> dead;
    {
        QMutexLocker locker(&m_mutex);
        dead.swap(m_graveyard);
    }
    if (dead.isEmpty())
        return;

    py::gil_scoped_acquire acquire;
    dead.clear();
}

QStringList CustomColumnManager::columnNames()
{
    QMutexLocker locker(&m_mutex);
    QStringList names;
    names.reserve(m_columns.size());
    for (const ColumnDef &def : std::as_const(m_columns))
        names.append(def.name);
    return names;
}

bool CustomColumnManager::hasColumnsForScript(const QString &scriptFile)
{
    QMutexLocker locker(&m_mutex);
    for (const ColumnDef &def : std::as_const(m_columns)) {
        if (def.scriptFile == scriptFile)
            return true;
    }
    return false;
}

int CustomColumnManager::minIntervalMs()
{
    QMutexLocker locker(&m_mutex);
    int min = -1;
    for (const ColumnDef &def : std::as_const(m_columns)) {
        if (min < 0 || def.intervalMs < min)
            min = def.intervalMs;
    }
    return min < 0 ? kDefaultIntervalMs : min;
}

bool CustomColumnManager::anythingDue()
{
    const qint64 now = m_clock.elapsed();
    QMutexLocker locker(&m_mutex);
    for (const ColumnDef &def : std::as_const(m_columns)) {
        if (def.nextDueMs <= now + dueToleranceMs(def.intervalMs))
            return true;
    }
    return false;
}

void CustomColumnManager::requestCompute()
{
    if (m_shuttingDown.load() || !m_worker)
        return;

    // Skip the worker wakeup + GIL acquisition when no column is due.
    if (!anythingDue())
        return;

    // Coalesce: at most one pass queued beyond the one that may be running,
    // so a provider slower than the timer never builds a backlog.
    if (m_computePending.exchange(true))
        return;

    // Snapshot the bot names here on the main thread; the worker must never
    // read the live bot list, which this thread mutates on add/remove.
    QStringList botNames;
    const QVector<BotInstance *> &bots = BotManager::getBots();
    botNames.reserve(bots.size());
    for (const BotInstance *bot : bots)
        botNames.append(bot->name);

    QMetaObject::invokeMethod(m_worker, [worker = m_worker, botNames]() {
        worker->compute(botNames);
    }, Qt::QueuedConnection);
}

static QString providerValueToString(const py::object &value)
{
    if (value.is_none())
        return QStringLiteral("-");
    try {
        return QString::fromStdString(py::str(value));
    } catch (...) {
        return QStringLiteral("?");
    }
}

void CustomColumnManager::computeSnapshot(const QStringList &botNames)
{
    m_computePending.store(false);

    if (m_shuttingDown.load())
        return;

    // Providers run with a current bot set (for API calls) but their console
    // output/errors belong to the global console, not the targeted bot's.
    PythonAPI::setForceGlobalConsole(true);

    py::gil_scoped_acquire acquire;

    // Remembered so shutdown() can raise KeyboardInterrupt in a blocked provider.
    m_workerPyThreadId.store(PyThread_get_thread_ident());

    // Copy the due (name, provider) pairs under the lock so providers can be
    // called without holding the mutex (a provider may run for a while).
    QList<ColumnDef> defs;
    {
        const qint64 now = m_clock.elapsed();
        QMutexLocker locker(&m_mutex);
        for (const ColumnDef &def : std::as_const(m_columns)) {
            if (def.nextDueMs <= now + dueToleranceMs(def.intervalMs))
                defs.append(def);
        }
    }
    if (defs.isEmpty())
        return;

    // Fastest columns first, so a quick column is not held back by a slow one.
    std::sort(defs.begin(), defs.end(), [](const ColumnDef &a, const ColumnDef &b) {
        return a.intervalMs < b.intervalMs;
    });

    for (const ColumnDef &def : std::as_const(defs)) {
        QVariantMap results;
        for (const QString &botName : botNames) {
            if (m_shuttingDown.load())
                return;
            QString cell;
            // Bind the current bot so the provider can use either unqualified
            // (bot.inventory()) or explicit (bot.inventory(bot_name)) calls.
            PythonAPI::setCurrentBot(botName);
            PythonAPI::setCurrentScript(def.scriptFile);
            try {
                py::object out = (*def.provider)(botName.toStdString());
                cell = providerValueToString(out);
            } catch (py::error_already_set &e) {
                cell = QStringLiteral("!");
                PythonAPI::error(std::string("column '") + def.name.toStdString()
                                 + "' failed for " + botName.toStdString() + ": " + e.what());
            } catch (std::exception &e) {
                cell = QStringLiteral("!");
                PythonAPI::error(std::string("column '") + def.name.toStdString()
                                 + "' failed for " + botName.toStdString() + ": " + e.what());
            }
            results[botName] = QVariantMap{{def.name, cell}};
        }

        // Reschedule from completion so a slow provider never runs back-to-back.
        // Re-look-up the def: it may have been unregistered or replaced mid-pass.
        {
            const qint64 done = m_clock.elapsed();
            QMutexLocker locker(&m_mutex);
            for (ColumnDef &live : m_columns) {
                if (live.name == def.name && live.provider == def.provider)
                    live.nextDueMs = done + live.intervalMs;
            }
        }

        if (!results.isEmpty())
            emit valuesReady(results);
    }
}

bool CustomColumnManager::shutdown()
{
    m_shuttingDown.store(true);

    if (m_workerThread) {
        m_workerThread->quit();

        std::thread interrupter;
        if (!m_workerThread->wait(kShutdownGraceMs)) {
            // A provider is blocking the pass. Interrupt it from a helper
            // thread: the GIL acquire may itself block on a provider stuck in a
            // GIL-holding C call, which the main thread must never wait on.
            if (unsigned long tid = m_workerPyThreadId.load()) {
                interrupter = std::thread([tid]() {
                    py::gil_scoped_acquire acquire;
                    PyThreadState_SetAsyncExc(tid, PyExc_KeyboardInterrupt);
                });
            }

            if (!m_workerThread->wait(kShutdownInterruptMs)) {
                // Stuck in a non-interruptible call. Abandon the thread (the
                // process is exiting); destroying a running QThread aborts, so
                // leak it and the worker. The interpreter must stay alive for
                // the leaked thread, hence the false return.
                if (interrupter.joinable())
                    interrupter.detach();
                LogManager::log(
                    "A custom column provider is blocked and will not stop; "
                    "abandoning the column compute thread",
                    LogManager::Warning);
                m_workerThread->setParent(nullptr);
                m_workerThread = nullptr;
                m_worker = nullptr;
                {
                    // Destroying the providers needs the GIL, which may never
                    // be free again - leak them so the singleton's destructor
                    // does not decref Python objects at process exit.
                    QMutexLocker locker(&m_mutex);
                    (void)new QList<ColumnDef>(std::move(m_columns));
                    (void)new QList<ColumnDef>(std::move(m_graveyard));
                    m_columns = {};
                    m_graveyard = {};
                }
                return false;
            }
        }
        // Worker exited, so the GIL is free and the interrupter finishes promptly.
        if (interrupter.joinable())
            interrupter.join();
        m_worker = nullptr; // deleted via QThread::finished -> deleteLater
    }

    // Release the provider functions while the interpreter is still alive.
    py::gil_scoped_acquire acquire;
    QMutexLocker locker(&m_mutex);
    m_columns.clear();
    m_graveyard.clear();
    return true;
}
