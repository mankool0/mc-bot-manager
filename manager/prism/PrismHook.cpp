#include <QCoreApplication>
#include <QAbstractListModel>
#include <QMetaObject>
#include <QSet>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>

#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/AccountData.h"
#include "minecraft/auth/MinecraftAccount.h"
#include <cstring>

// InstanceList::InstanceIDRole from launcher/InstanceList.h
static constexpr int kInstanceIDRole = 0x34B1CB49;

#ifdef _WIN32
#  include <windows.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#endif


static AccountList* findAccountList()
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return nullptr;
    for (QObject* o : app->findChildren<QObject*>()) {
        if (strcmp(o->metaObject()->className(), "AccountList") == 0)
            return static_cast<AccountList*>(o);
    }
    return nullptr;
}

static QAbstractListModel* findInstanceList()
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return nullptr;
    for (QObject* o : app->findChildren<QObject*>()) {
        if (strcmp(o->metaObject()->className(), "InstanceList") == 0)
            return static_cast<QAbstractListModel*>(o);
    }
    return nullptr;
}

static MinecraftAccountPtr findAccount(AccountList* list, const QString& name)
{
    for (int i = 0, n = list->rowCount({}); i < n; ++i) {
        auto v = list->data(list->index(i, 0), AccountList::PointerRole);
        MinecraftAccountPtr account = v.value<MinecraftAccountPtr>();
        if (!account) continue;
        AccountData* data = account->accountData();
        if (!data) continue;
        if (data->minecraftProfile.name == name)
            return account;
    }
    return nullptr;
}

static QSet<QLocalSocket*> g_subscribers;

static QByteArray buildAccountsPayload()
{
    QByteArray payload = "accounts_changed\n";
    AccountList* list = findAccountList();
    if (list) {
        for (int i = 0, n = list->rowCount({}); i < n; ++i) {
            auto v = list->data(list->index(i, 0), AccountList::PointerRole);
            MinecraftAccountPtr account = v.value<MinecraftAccountPtr>();
            if (!account) continue;
            AccountData* data = account->accountData();
            if (!data) continue;
            payload += QString("account:%1|%2|%3\n")
                .arg(data->minecraftProfile.id,
                     data->minecraftProfile.name,
                     account->internalId())
                .toUtf8();
        }
    }
    payload += "accounts_end\n";
    return payload;
}

static QByteArray buildInstancesPayload()
{
    QByteArray payload = "instances_changed\n";
    QAbstractListModel* list = findInstanceList();
    if (list) {
        for (int i = 0, n = list->rowCount({}); i < n; ++i) {
            QModelIndex idx = list->index(i, 0);
            QString id = list->data(idx, kInstanceIDRole).toString();
            QString name = list->data(idx, Qt::DisplayRole).toString();
            if (id.isEmpty()) continue;
            payload += QString("instance:%1|%2\n").arg(id, name).toUtf8();
        }
    }
    payload += "instances_end\n";
    return payload;
}

static void sendToSubscribers(const QByteArray& data)
{
    for (QLocalSocket* s : std::as_const(g_subscribers)) {
        if (s && s->state() == QLocalSocket::ConnectedState)
            s->write(data);
    }
}

static void initChangeSignals()
{
    static bool connected = false;
    if (connected) return;
    connected = true;

    auto pushAccounts = []() { sendToSubscribers(buildAccountsPayload()); };
    auto pushInstances = []() { sendToSubscribers(buildInstancesPayload()); };

    if (auto* accounts = static_cast<QAbstractItemModel*>(findAccountList())) {
        QObject::connect(accounts, &QAbstractItemModel::rowsInserted,
                         QCoreApplication::instance(), pushAccounts);
        QObject::connect(accounts, &QAbstractItemModel::rowsRemoved,
                         QCoreApplication::instance(), pushAccounts);
        QObject::connect(accounts, &QAbstractItemModel::modelReset,
                         QCoreApplication::instance(), pushAccounts);
    }

    if (auto* instances = static_cast<QAbstractItemModel*>(findInstanceList())) {
        QObject::connect(instances, &QAbstractItemModel::rowsInserted,
                         QCoreApplication::instance(), pushInstances);
        QObject::connect(instances, &QAbstractItemModel::rowsRemoved,
                         QCoreApplication::instance(), pushInstances);
        QObject::connect(instances, &QAbstractItemModel::modelReset,
                         QCoreApplication::instance(), pushInstances);
    }
}

class RefreshWatcher : public QObject {
    Q_OBJECT
public:
    explicit RefreshWatcher(QLocalSocket* socket, QObject* parent = nullptr)
        : QObject(parent), m_socket(socket)
    {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, &RefreshWatcher::onTimeout);
        m_timer->start(60000);
    }

public slots:
    void onActivityChanged(bool active)
    {
        if (active) return;
        m_timer->stop();
        if (m_socket) {
            m_socket->write("ok\n");
            m_socket->disconnectFromServer();
        }
        deleteLater();
    }

    void onTimeout()
    {
        if (m_socket) {
            m_socket->write("error:timeout\n");
            m_socket->disconnectFromServer();
        }
        deleteLater();
    }

private:
    QPointer<QLocalSocket> m_socket;
    QTimer* m_timer;
};

static QString hookPath()
{
#ifdef _WIN32
    // QLocalSocket prepends \\.\pipe\ on Windows
    return "mcbotmanager-prism-hook";
#else
    const char* ov = getenv("MCBM_HOOK_SOCKET");
    if (ov && *ov) return QString::fromUtf8(ov);
    const char* xdg = getenv("XDG_RUNTIME_DIR");
    return QString("%1/mcbotmanager-prism-hook").arg(xdg ? xdg : "/tmp");
#endif
}

static void doRefresh(QLocalSocket* socket, const QString& profile)
{
    AccountList* list = findAccountList();
    if (!list) {
        socket->write("error:no_account_list\n");
        socket->disconnectFromServer();
        return;
    }

    MinecraftAccountPtr account = findAccount(list, profile);
    if (!account) {
        socket->write("error:account_not_found\n");
        socket->disconnectFromServer();
        return;
    }

    // Connect before forceRefresh so we don't miss the signal if the task
    // completes very quickly. activityChanged(false) = AccountList is "Ready".
    auto* watcher = new RefreshWatcher(socket, QCoreApplication::instance());
    QObject::connect(list, SIGNAL(activityChanged(bool)),
                     watcher, SLOT(onActivityChanged(bool)));

    QString id = account->internalId();
    list->requestRefresh(id);
    socket->write("refreshing\n");
    // socket stays open, RefreshWatcher closes it when activityChanged(false) fires.
}

static void doSubscribe(QLocalSocket* socket)
{
    g_subscribers.insert(socket);
    QObject::connect(socket, &QLocalSocket::disconnected, socket, [socket]() {
        g_subscribers.remove(socket);
    });
    socket->write(buildAccountsPayload());
    socket->write(buildInstancesPayload());
    initChangeSignals();
}

static bool dispatchCommand(QLocalSocket* socket, const QString& cmd)
{
    if (cmd == "ping") {
        socket->write("pong\n");
    } else if (cmd == "list_accounts") {
        socket->write(buildAccountsPayload());
    } else if (cmd == "list_instances") {
        socket->write(buildInstancesPayload());
    } else if (cmd == "subscribe") {
        doSubscribe(socket);
        return false;  // keep socket open for push notifications
    } else if (cmd.startsWith("refresh:")) {
        doRefresh(socket, cmd.mid(8));
        return false;
    } else {
        socket->write("error:unknown_command\n");
    }
    return true;
}

static void initializeHookServer()
{
    QString path = hookPath();
    QLocalServer::removeServer(path);

    auto* server = new QLocalServer(QCoreApplication::instance());
    if (!server->listen(path)) {
        server->deleteLater();
        return;
    }

    QObject::connect(server, &QLocalServer::newConnection, server, [server]() {
        while (server->hasPendingConnections()) {
            QLocalSocket* socket = server->nextPendingConnection();
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket]() {
                while (socket->canReadLine()) {
                    QString cmd = QString::fromUtf8(socket->readLine()).trimmed();
                    if (!dispatchCommand(socket, cmd))
                        return;
                }
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

// ---- Platform entry points ----

#ifdef _WIN32

static DWORD WINAPI hookWaiterThread(LPVOID)
{
    while (!QCoreApplication::instance())
        Sleep(50);
    QMetaObject::invokeMethod(QCoreApplication::instance(),
        []() { initializeHookServer(); },
        Qt::QueuedConnection);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        CreateThread(nullptr, 0, hookWaiterThread, nullptr, 0, nullptr);
    return TRUE;
}

#else

__attribute__((constructor))
static void hookInit()
{
    if (!getenv("MCBM_HOOK_SOCKET")) return;

    pthread_t t;
    pthread_create(&t, nullptr, [](void*) -> void* {
        while (!QCoreApplication::instance())
            usleep(50000);
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  []() { initializeHookServer(); },
                                  Qt::QueuedConnection);
        return nullptr;
    }, nullptr);
    pthread_detach(t);
}

#endif

#include "PrismHook.moc"
