#ifndef CUSTOMCOLUMNMANAGER_H
#define CUSTOMCOLUMNMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMutex>
#include <QVariantMap>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
#include <memory>

#undef slots
#include <pybind11/pybind11.h>
#define slots Q_SLOTS

namespace py = pybind11;

class CustomColumnManager;

// Suppress visibility warning: pybind11 types have hidden visibility.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

// Runs on its own thread. Calls each registered provider per bot with the GIL
// held so a slow user-written provider never blocks the UI thread.
class ColumnComputeWorker : public QObject
{
    Q_OBJECT
public:
    explicit ColumnComputeWorker(CustomColumnManager *manager, QObject *parent = nullptr);

public slots:
    // botNames is snapshotted on the main thread; the worker must never touch
    // the live BotInstance list, which the main thread mutates freely.
    void compute(const QStringList &botNames);
    void drainGraveyard();

private:
    CustomColumnManager *m_manager;
};

// Owns the set of custom instance-table columns registered from global scripts.
// A column is a display name plus a Python provider called once per bot.
class CustomColumnManager : public QObject
{
    Q_OBJECT
public:
    // Providers may not run faster than this, however small the requested interval.
    static constexpr int kMinIntervalMs = 50;
    static constexpr int kDefaultIntervalMs = 1000;

    static CustomColumnManager &instance();

    // Called from a script thread with the GIL held. Replaces any existing
    // column with the same name. intervalMs is clamped to kMinIntervalMs; the
    // column is due immediately.
    void registerColumn(const QString &name, const py::function &provider, const QString &scriptFile,
                        int intervalMs = kDefaultIntervalMs);

    // Remove a single column by name / every column of a given script. No GIL
    // needed: removed providers are destroyed on the worker thread (the
    // "graveyard"), so these are safe to call while a provider is running.
    void unregisterColumn(const QString &name);
    void unregisterScript(const QString &scriptFile);

    // Column display names in registration order. Thread-safe, no GIL needed.
    QStringList columnNames();

    // Smallest registered update interval, or kDefaultIntervalMs when no
    // columns exist. Drives the UI's compute-tick timer. Thread-safe.
    int minIntervalMs();

    // True if at least one column is due for a recompute. Cheap (mutex +
    // timestamp compares, no GIL) so the UI timer can poll it every tick.
    bool anythingDue();

    // True if the given script registered at least one column that is still
    // active. Thread-safe, no GIL needed.
    bool hasColumnsForScript(const QString &scriptFile);

    // Stop the compute worker and release all providers. Must be called from
    // the main thread while the interpreter is still alive. Interrupts a
    // running provider and waits a bounded time; returns false if the worker
    // was stuck in a non-interruptible call and had to be abandoned - the
    // Python interpreter must NOT be finalized in that case.
    bool shutdown();

    // Called on the worker thread (acquires the GIL itself). Runs only the due
    // columns, fastest interval first, emitting valuesReady once per column.
    void computeSnapshot(const QStringList &botNames);

    // Called on the worker thread: destroys unregistered providers under the GIL.
    void drainGraveyard();

public slots:
    // Snapshot the bot list and queue a compute pass onto the worker thread.
    // Main thread only. Passes are coalesced, so it never builds a backlog.
    void requestCompute();

signals:
    void columnsChanged();
    // botName -> QVariantMap(columnName -> display string). Emitted from the
    // worker thread (per due column), so cross-thread receivers get it queued.
    void valuesReady(const QVariantMap &results);

private:
    explicit CustomColumnManager(QObject *parent = nullptr);
    ~CustomColumnManager() override;

    struct ColumnDef {
        QString name;
        // shared_ptr so QList copies/detaches move the pointer, not the
        // py::function - only creating and destroying the pointee touches the
        // Python refcount and needs the GIL.
        std::shared_ptr<py::function> provider;
        QString scriptFile;
        int intervalMs = kDefaultIntervalMs;
        // m_clock time of the next recompute; 0 = due immediately. Written by
        // the worker when a pass finishes the column, read under m_mutex.
        qint64 nextDueMs = 0;
    };

    void scheduleGraveyardDrain();

    QList<ColumnDef> m_columns;
    // Removed defs awaiting provider destruction on the worker thread.
    QList<ColumnDef> m_graveyard;
    QMutex m_mutex;
    // Monotonic time base for ColumnDef::nextDueMs (started in the ctor).
    QElapsedTimer m_clock;
    std::atomic<bool> m_shuttingDown{false};
    std::atomic<bool> m_computePending{false};
    std::atomic<unsigned long> m_workerPyThreadId{0};

    QThread *m_workerThread = nullptr;
    ColumnComputeWorker *m_worker = nullptr;
};

#pragma GCC diagnostic pop

#endif // CUSTOMCOLUMNMANAGER_H
