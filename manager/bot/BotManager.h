#ifndef BOTMANAGER_H
#define BOTMANAGER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QVector3D>
#include <QProcess>
#include "protocol.qpb.h"
#include "connection.qpb.h"
#include "player.qpb.h"
#include "inventory.qpb.h"
#include "chat.qpb.h"
#include "commands.qpb.h"

class LogManager;

enum class BotStatus {
    Offline,
    Starting,
    Online,
    Stopping,
    Error
};

struct BotInstance {
    QString name;
    BotStatus status = BotStatus::Offline;
    QString instance;
    QString account;
    QString server;
    int connectionId = -1;
    int maxMemory;
    int currentMemory;
    int restartThreshold;
    bool autoRestart;
    bool tokenRefresh;
    bool debugLogging;
    bool manualStop = false;

    QVector3D position;
    QString dimension;

    QProcess* process = nullptr;
    qint64 minecraftPid = 0;

    // Network statistics
    qint64 bytesReceived = 0;
    qint64 bytesSent = 0;
    double dataRateIn = 0.0;   // bytes/sec
    double dataRateOut = 0.0;  // bytes/sec
};

class BotManager : public QObject
{
    Q_OBJECT

public:
    static BotManager& instance();

    // Delete copy constructor and assignment operator
    BotManager(const BotManager&) = delete;
    BotManager& operator=(const BotManager&) = delete;

    static QVector<BotInstance>& getBots();
    static BotInstance* getBotByConnectionId(int connectionId);
    static BotInstance* getBotByName(const QString &name);

    static void addBot(const BotInstance &bot);
    static void removeBot(const QString &name);
    static void updateBot(const QString &name, const BotInstance &updatedBot);

    // Message handlers
    static void handleConnectionInfo(int connectionId, const mankool::mcbot::protocol::ConnectionInfo &info);
    static void handleServerStatus(int connectionId, const mankool::mcbot::protocol::ServerConnectionStatus &status);
    static void handlePlayerState(int connectionId, const mankool::mcbot::protocol::PlayerStateUpdate &state);
    static void handleInventoryUpdate(int connectionId, const mankool::mcbot::protocol::InventoryUpdate &inventory);
    static void handleChatMessage(int connectionId, const mankool::mcbot::protocol::ChatMessage &chat);
    static void handleCommandResponse(int connectionId, const mankool::mcbot::protocol::CommandResponse &response);
    static void handleHeartbeat(int connectionId, const mankool::mcbot::protocol::HeartbeatMessage &heartbeat);

    // Command senders
    static void sendShutdownCommand(const QString &botName, const QString &reason = "");

signals:
    void botAdded(const QString &name);
    void botRemoved(const QString &name);
    void botUpdated(const QString &name);

private:
    explicit BotManager(QObject *parent = nullptr);

    QVector<BotInstance>& getBotsImpl() { return botInstances; }
    BotInstance* getBotByConnectionIdImpl(int connectionId);
    BotInstance* getBotByNameImpl(const QString &name);
    void addBotImpl(const BotInstance &bot);
    void removeBotImpl(const QString &name);
    void updateBotImpl(const QString &name, const BotInstance &updatedBot);
    void handleConnectionInfoImpl(int connectionId, const mankool::mcbot::protocol::ConnectionInfo &info);
    void handleServerStatusImpl(int connectionId, const mankool::mcbot::protocol::ServerConnectionStatus &status);
    void handlePlayerStateImpl(int connectionId, const mankool::mcbot::protocol::PlayerStateUpdate &state);
    void handleInventoryUpdateImpl(int connectionId, const mankool::mcbot::protocol::InventoryUpdate &inventory);
    void handleChatMessageImpl(int connectionId, const mankool::mcbot::protocol::ChatMessage &chat);
    void handleCommandResponseImpl(int connectionId, const mankool::mcbot::protocol::CommandResponse &response);
    void handleHeartbeatImpl(int connectionId, const mankool::mcbot::protocol::HeartbeatMessage &heartbeat);
    void sendShutdownCommandImpl(const QString &botName, const QString &reason);

    QVector<BotInstance> botInstances;
};

#endif // BOTMANAGER_H
