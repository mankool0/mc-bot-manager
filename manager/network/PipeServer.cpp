#include "PipeServer.h"
#include "logging/LogManager.h"
#include "bot/BotManager.h"
#include "protocol.qpb.h"
#include "connection.qpb.h"
#include <QtProtobuf/QProtobufSerializer>

PipeServer::PipeServer()
    : QObject(nullptr)
    , server(nullptr)
    , nextConnectionId(1000)
{
}

PipeServer::~PipeServer()
{
    stopImpl();
}

PipeServer& PipeServer::instance()
{
    static PipeServer instance;
    return instance;
}

bool PipeServer::start(const QString &pipeName)
{
    return instance().startImpl(pipeName);
}

bool PipeServer::startImpl(const QString &pipeName)
{
    if (server) {
        LogManager::log("Pipe server already running", LogManager::Warning);
        return false;
    }

    serverPipeName = pipeName;
    server = new QLocalServer(this);

    QLocalServer::removeServer(pipeName);

    if (!server->listen(pipeName)) {
        LogManager::log(QString("Failed to create pipe server '%1': %2")
                       .arg(pipeName)
                       .arg(server->errorString()), LogManager::Error);
        delete server;
        server = nullptr;
        return false;
    }

    LogManager::log(QString("Pipe server listening on '%1'").arg(pipeName), LogManager::Success);

    connect(server, &QLocalServer::newConnection,
            this, &PipeServer::handleNewConnection);

    return true;
}

void PipeServer::stop()
{
    instance().stopImpl();
}

void PipeServer::stopImpl()
{
    if (!server) {
        return;
    }

    for (auto it = connections.begin(); it != connections.end(); ++it) {
        QLocalSocket *socket = it.value();
        if (socket) {
            socket->deleteLater();
        }
    }
    for (auto stream : connectionStreams) {
        delete stream;
    }
    connections.clear();
    connectionBotNames.clear();
    connectionStreams.clear();

    server->close();
    delete server;
    server = nullptr;

    LogManager::log("Pipe server stopped", LogManager::Info);
}

void PipeServer::handleNewConnection()
{
    while (server->hasPendingConnections()) {
        QLocalSocket *socket = server->nextPendingConnection();

        if (!socket) {
            continue;
        }

        int connectionId = nextConnectionId++;
        connections[connectionId] = socket;

        // Create a data stream for this connection
        QDataStream *stream = new QDataStream(socket);
        stream->setByteOrder(QDataStream::LittleEndian);
        stream->setVersion(QDataStream::Qt_5_0);
        connectionStreams[connectionId] = stream;

        LogManager::log(QString("New client connected (Connection ID: %1)").arg(connectionId), LogManager::Info);

        connect(socket, &QLocalSocket::readyRead,
                this, &PipeServer::handleClientData);

        connect(socket, &QLocalSocket::disconnected,
                this, &PipeServer::handleClientDisconnection);
    }
}

void PipeServer::handleClientData()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket) {
        return;
    }

    int connectionId = -1;
    for (auto it = connections.begin(); it != connections.end(); ++it) {
        if (it.value() == socket) {
            connectionId = it.key();
            break;
        }
    }

    if (connectionId == -1) {
        LogManager::log("Data from unknown connection", LogManager::Error);
        return;
    }

    QDataStream *stream = connectionStreams[connectionId];
    if (!stream) {
        LogManager::log("No data stream for connection", LogManager::Error);
        return;
    }

    while (socket->bytesAvailable() >= 4) {
        stream->startTransaction();

        quint32 messageLength;
        *stream >> messageLength;

        QByteArray messageData(messageLength, Qt::Uninitialized);
        int bytesRead = stream->readRawData(messageData.data(), messageLength);

        if (!stream->commitTransaction()) {
            // Not enough data yet - wait for next readyRead
            break;
        }

        if (bytesRead != static_cast<int>(messageLength)) {
            LogManager::log(QString("Read mismatch: expected %1, got %2").arg(messageLength).arg(bytesRead), LogManager::Error);
            continue;
        }

        processMessage(connectionId, messageData);

        // Track bytes received
        BotInstance *bot = BotManager::getBotByConnectionId(connectionId);
        if (bot) {
            bot->bytesReceived += messageLength + 4; // Include length prefix
        }
    }
}

void PipeServer::processMessage(int connectionId, const QByteArray &data)
{
    QProtobufSerializer serializer;

    mankool::mcbot::protocol::ClientToManagerMessage clientMsg;
    if (serializer.deserialize(&clientMsg, data)) {
        if (clientMsg.hasConnectionInfo()) {
            connectionBotNames[connectionId] = clientMsg.connectionInfo().playerName();
            BotManager::handleConnectionInfo(connectionId, clientMsg.connectionInfo());
            emit clientConnected(connectionId, clientMsg.connectionInfo().playerName());
        } else if (clientMsg.hasHeartbeat()) {
            BotManager::handleHeartbeat(connectionId, clientMsg.heartbeat());
        } else if (clientMsg.hasServerStatus()) {
            BotManager::handleServerStatus(connectionId, clientMsg.serverStatus());
        } else if (clientMsg.hasPlayerState()) {
            BotManager::handlePlayerState(connectionId, clientMsg.playerState());
        } else if (clientMsg.hasInventory()) {
            BotManager::handleInventoryUpdate(connectionId, clientMsg.inventory());
        } else if (clientMsg.hasChat()) {
            BotManager::handleChatMessage(connectionId, clientMsg.chat());
        } else if (clientMsg.hasCommandResponse()) {
            BotManager::handleCommandResponse(connectionId, clientMsg.commandResponse());
        } else if (clientMsg.hasModulesResponse()) {
            BotManager::handleModulesResponse(connectionId, clientMsg.modulesResponse());
        } else if (clientMsg.hasModuleConfigResponse()) {
            BotManager::handleModuleConfigResponse(connectionId, clientMsg.moduleConfigResponse());
        } else if (clientMsg.hasModuleStateChanged()) {
            BotManager::handleModuleStateChanged(connectionId, clientMsg.moduleStateChanged());
        } else if (clientMsg.hasBaritoneSettingsResponse()) {
            BotManager::handleBaritoneSettingsResponse(connectionId, clientMsg.baritoneSettingsResponse());
        } else if (clientMsg.hasBaritoneCommandsResponse()) {
            BotManager::handleBaritoneCommandsResponse(connectionId, clientMsg.baritoneCommandsResponse());
        } else if (clientMsg.hasBaritoneSettingsSetResponse()) {
            BotManager::handleBaritoneSettingsSetResponse(connectionId, clientMsg.baritoneSettingsSetResponse());
        } else if (clientMsg.hasBaritoneCommandResponse()) {
            BotManager::handleBaritoneCommandResponse(connectionId, clientMsg.baritoneCommandResponse());
        } else if (clientMsg.hasBaritoneSettingUpdate()) {
            BotManager::handleBaritoneSettingUpdate(connectionId, clientMsg.baritoneSettingUpdate());
        }
        return;
    }

    LogManager::log(QString("Failed to parse message (%1 bytes)").arg(data.size()),
                    LogManager::Error);
}


void PipeServer::handleClientDisconnection()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket) {
        return;
    }

    for (auto it = connections.begin(); it != connections.end(); ++it) {
        if (it.value() == socket) {
            int connectionId = it.key();

            LogManager::log(QString("Client disconnected (Connection ID: %1)").arg(connectionId), LogManager::Warning);

            emit clientDisconnected(connectionId);

            connections.remove(connectionId);
            connectionBotNames.remove(connectionId);

            if (connectionStreams.contains(connectionId)) {
                delete connectionStreams[connectionId];
                connectionStreams.remove(connectionId);
            }
            break;
        }
    }

    socket->deleteLater();
}

void PipeServer::sendToClient(int connectionId, const QByteArray &data)
{
    instance().sendToClientImpl(connectionId, data);
}

void PipeServer::sendToClientImpl(int connectionId, const QByteArray &data)
{
    if (!connections.contains(connectionId)) {
        LogManager::log(QString("No connection found for ID: %1").arg(connectionId), LogManager::Error);
        return;
    }

    QLocalSocket *socket = connections[connectionId];
    socket->write(data);

    // Track bytes sent
    BotInstance *bot = BotManager::getBotByConnectionId(connectionId);
    if (bot) {
        bot->bytesSent += data.size();
    }
}

void PipeServer::broadcastToAll(const QByteArray &data)
{
    instance().broadcastToAllImpl(data);
}

void PipeServer::broadcastToAllImpl(const QByteArray &data)
{
    for (QLocalSocket *socket : connections) {
        socket->write(data);
    }

    LogManager::log(QString("Broadcast %1 bytes to %2 connected clients").arg(data.size()).arg(connections.size()), LogManager::Info);
}

int PipeServer::getConnectionId(const QString &botName)
{
    return instance().getConnectionIdImpl(botName);
}

int PipeServer::getConnectionIdImpl(const QString &botName) const
{
    for (auto it = connectionBotNames.begin(); it != connectionBotNames.end(); ++it) {
        if (it.value() == botName) {
            return it.key();
        }
    }
    return -1;
}

QList<int> PipeServer::getAllConnectionIds()
{
    return instance().getAllConnectionIdsImpl();
}

QList<int> PipeServer::getAllConnectionIdsImpl() const
{
    return connections.keys();
}
