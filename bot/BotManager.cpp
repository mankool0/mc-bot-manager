#include "BotManager.h"
#include "logging/LogManager.h"
#include "network/PipeServer.h"
#include <QDateTime>
#include <QDataStream>
#include <QtProtobuf/QProtobufSerializer>

BotManager::BotManager(QObject *parent)
    : QObject(parent)
{
}

BotManager& BotManager::instance()
{
    static BotManager instance;
    return instance;
}

QVector<BotInstance>& BotManager::getBots()
{
    return instance().getBotsImpl();
}

BotInstance* BotManager::getBotByConnectionId(int connectionId)
{
    return instance().getBotByConnectionIdImpl(connectionId);
}

BotInstance* BotManager::getBotByConnectionIdImpl(int connectionId)
{
    for (BotInstance &bot : botInstances) {
        if (bot.connectionId == connectionId) {
            return &bot;
        }
    }
    return nullptr;
}

BotInstance* BotManager::getBotByName(const QString &name)
{
    return instance().getBotByNameImpl(name);
}

BotInstance* BotManager::getBotByNameImpl(const QString &name)
{
    for (BotInstance &bot : botInstances) {
        if (bot.name == name) {
            return &bot;
        }
    }
    return nullptr;
}

void BotManager::addBot(const BotInstance &bot)
{
    instance().addBotImpl(bot);
}

void BotManager::addBotImpl(const BotInstance &bot)
{
    botInstances.append(bot);
    emit botAdded(bot.name);
}

void BotManager::removeBot(const QString &name)
{
    instance().removeBotImpl(name);
}

void BotManager::removeBotImpl(const QString &name)
{
    for (int i = 0; i < botInstances.size(); ++i) {
        if (botInstances[i].name == name) {
            botInstances.removeAt(i);
            emit botRemoved(name);
            return;
        }
    }
}

void BotManager::updateBot(const QString &name, const BotInstance &updatedBot)
{
    instance().updateBotImpl(name, updatedBot);
}

void BotManager::updateBotImpl(const QString &name, const BotInstance &updatedBot)
{
    for (int i = 0; i < botInstances.size(); ++i) {
        if (botInstances[i].name == name) {
            botInstances[i] = updatedBot;
            emit botUpdated(name);
            return;
        }
    }
}

void BotManager::handleConnectionInfo(int connectionId, const mankool::mcbot::protocol::ConnectionInfo &info)
{
    instance().handleConnectionInfoImpl(connectionId, info);
}

void BotManager::handleConnectionInfoImpl(int connectionId, const mankool::mcbot::protocol::ConnectionInfo &info)
{
    QString playerName = info.playerName();
    QString clientVersion = info.clientVersion();
    QString modVersion = info.modVersion();
    QString playerUuid = info.playerUuid();

    // Find bot by player name and update its state
    BotInstance *bot = getBotByNameImpl(playerName);
    if (bot) {
        bot->connectionId = connectionId;
        bot->status = BotStatus::Online;
        bot->minecraftPid = info.processId();
        bot->startTime = QDateTime::currentDateTime();

        // Update max memory from connection info (convert bytes to MB)
        if (info.maxMemory() > 0) {
            bot->maxMemory = info.maxMemory() / (1024 * 1024);
        }

        emit botUpdated(bot->name);

        LogManager::log(QString("Bot '%1' connected (Connection ID: %2)")
                       .arg(playerName).arg(connectionId), LogManager::Success);
    } else {
        LogManager::log(QString("Received ConnectionInfo for unknown bot '%1'")
                       .arg(playerName), LogManager::Warning);
    }

    if (bot && bot->debugLogging) {
        LogManager::log(QString("[DEBUG %1] ConnectionInfo received").arg(bot->name), LogManager::Debug);
    }
}

void BotManager::handleServerStatus(int connectionId, const mankool::mcbot::protocol::ServerConnectionStatus &status)
{
    instance().handleServerStatusImpl(connectionId, status);
}

void BotManager::handleServerStatusImpl(int connectionId, const mankool::mcbot::protocol::ServerConnectionStatus &status)
{
    QString serverName = status.serverName();
    QString serverAddr = status.serverAddress();

    // Update bot state
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (bot) {
        bot->server = serverAddr;
        emit botUpdated(bot->name);
    }

    if (bot && bot->debugLogging) {
        LogManager::log(QString("[DEBUG %1] ServerStatus received").arg(bot->name), LogManager::Debug);
    }
}

void BotManager::handlePlayerState(int connectionId, const mankool::mcbot::protocol::PlayerStateUpdate &state)
{
    instance().handlePlayerStateImpl(connectionId, state);
}

void BotManager::handlePlayerStateImpl(int connectionId, const mankool::mcbot::protocol::PlayerStateUpdate &state)
{
    // Update bot state
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (bot) {
        if (state.hasPosition()) {
            bot->position = QVector3D(state.position().x(), state.position().y(), state.position().z());
        }
        if (!state.dimension().isEmpty()) {
            bot->dimension = state.dimension();
        }
        emit botUpdated(bot->name);
    }

    if (bot && bot->debugLogging) {
        LogManager::log(QString("[DEBUG %1] PlayerState received").arg(bot->name), LogManager::Debug);
    }
}

void BotManager::handleInventoryUpdate(int connectionId, const mankool::mcbot::protocol::InventoryUpdate &inventory)
{
    instance().handleInventoryUpdateImpl(connectionId, inventory);
}

void BotManager::handleInventoryUpdateImpl(int connectionId, const mankool::mcbot::protocol::InventoryUpdate &inventory)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);

    if (bot && bot->debugLogging) {
        LogManager::log(QString("[DEBUG %1] InventoryUpdate received").arg(bot->name), LogManager::Debug);
    }
}

void BotManager::handleChatMessage(int connectionId, const mankool::mcbot::protocol::ChatMessage &chat)
{
    instance().handleChatMessageImpl(connectionId, chat);
}

void BotManager::handleChatMessageImpl(int connectionId, const mankool::mcbot::protocol::ChatMessage &chat)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);

    if (bot && bot->debugLogging) {
        LogManager::log(QString("[DEBUG %1] ChatMessage received").arg(bot->name), LogManager::Debug);
    }
}

void BotManager::handleCommandResponse(int connectionId, const mankool::mcbot::protocol::CommandResponse &response)
{
    instance().handleCommandResponseImpl(connectionId, response);
}

void BotManager::handleCommandResponseImpl(int connectionId, const mankool::mcbot::protocol::CommandResponse &response)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);

    if (bot && bot->debugLogging) {
        LogManager::log(QString("[DEBUG %1] CommandResponse received").arg(bot->name), LogManager::Debug);
    }
}

void BotManager::sendCommand(const QString &botName, const QString &commandText)
{
    instance().sendCommandImpl(botName, commandText);
}

void BotManager::sendShutdownCommand(const QString &botName, const QString &reason)
{
    instance().sendShutdownCommandImpl(botName, reason);
}

void BotManager::sendShutdownCommandImpl(const QString &botName, const QString &reason)
{
    BotInstance *bot = getBotByNameImpl(botName);
    if (!bot) {
        LogManager::log(QString("Cannot send shutdown: bot '%1' not found").arg(botName), LogManager::Warning);
        return;
    }

    if (bot->connectionId <= 0) {
        LogManager::log(QString("Cannot send shutdown: bot '%1' not connected").arg(botName), LogManager::Warning);
        return;
    }

    // Create shutdown command message
    mankool::mcbot::protocol::ManagerToClientMessage msg;
    msg.setMessageId(QString::number(QDateTime::currentMSecsSinceEpoch()));
    msg.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    mankool::mcbot::protocol::ShutdownCommand shutdown;
    shutdown.setReason(reason);
    msg.setShutdown(shutdown);

    // Serialize protobuf message
    QProtobufSerializer serializer;
    QByteArray protoData = serializer.serialize(&msg);
    if (protoData.isEmpty()) {
        LogManager::log(QString("Failed to serialize shutdown command for bot '%1'").arg(botName), LogManager::Error);
        return;
    }

    // Wrap with length prefix
    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(protoData.size());
    message.append(protoData);

    PipeServer::sendToClient(bot->connectionId, message);

    LogManager::log(QString("Sent graceful shutdown command to bot '%1'").arg(botName), LogManager::Info);
}

void BotManager::sendCommandImpl(const QString &botName, const QString &commandText)
{
    BotInstance *bot = getBotByNameImpl(botName);
    if (!bot) {
        LogManager::log(QString("Cannot send command: bot '%1' not found").arg(botName), LogManager::Warning);
        return;
    }

    if (bot->connectionId <= 0) {
        LogManager::log(QString("Cannot send command: bot '%1' not connected").arg(botName), LogManager::Warning);
        return;
    }

    QStringList parts = commandText.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return;
    }

    QString cmd = parts[0].toLower();

    mankool::mcbot::protocol::ManagerToClientMessage msg;
    msg.setMessageId(QString::number(QDateTime::currentMSecsSinceEpoch()));
    msg.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    if (cmd == "connect") {
        if (parts.size() < 2) {
            LogManager::log("Usage: connect <server_address>", LogManager::Warning);
            return;
        }
        mankool::mcbot::protocol::ConnectToServerCommand connectCmd;
        connectCmd.setServerAddress(parts.mid(1).join(' '));
        msg.setConnectServer(connectCmd);
    }
    else if (cmd == "disconnect") {
        mankool::mcbot::protocol::DisconnectCommand disconnectCmd;
        disconnectCmd.setReason(parts.size() > 1 ? parts.mid(1).join(' ') : "");
        msg.setDisconnect(disconnectCmd);
    }
    else if (cmd == "chat") {
        if (parts.size() < 2) {
            LogManager::log("Usage: chat <message>", LogManager::Warning);
            return;
        }
        mankool::mcbot::protocol::SendChatCommand chatCmd;
        chatCmd.setMessage(parts.mid(1).join(' '));
        msg.setSendChat(chatCmd);
    }
    else if (cmd == "move") {
        if (parts.size() < 4) {
            LogManager::log("Usage: move <x> <y> <z>", LogManager::Warning);
            return;
        }
        bool okX, okY, okZ;
        double x = parts[1].toDouble(&okX);
        double y = parts[2].toDouble(&okY);
        double z = parts[3].toDouble(&okZ);
        if (!okX || !okY || !okZ) {
            LogManager::log("Invalid coordinates", LogManager::Warning);
            return;
        }
        mankool::mcbot::protocol::MoveToCommand moveCmd;
        mankool::mcbot::protocol::Vec3d pos;
        pos.setX(x);
        pos.setY(y);
        pos.setZ(z);
        moveCmd.setTargetPosition(pos);
        msg.setMoveTo(moveCmd);
    }
    else if (cmd == "lookat") {
        if (parts.size() < 2) {
            LogManager::log("Usage: lookat <x> <y> <z> | lookat entity <id>", LogManager::Warning);
            return;
        }
        mankool::mcbot::protocol::LookAtCommand lookAtCmd;
        if (parts[1].toLower() == "entity") {
            if (parts.size() < 3) {
                LogManager::log("Usage: lookat entity <id>", LogManager::Warning);
                return;
            }
            bool ok;
            int entityId = parts[2].toInt(&ok);
            if (!ok) {
                LogManager::log("Invalid entity ID", LogManager::Warning);
                return;
            }
            lookAtCmd.setEntityId(entityId);
        } else {
            if (parts.size() < 4) {
                LogManager::log("Usage: lookat <x> <y> <z>", LogManager::Warning);
                return;
            }
            bool okX, okY, okZ;
            double x = parts[1].toDouble(&okX);
            double y = parts[2].toDouble(&okY);
            double z = parts[3].toDouble(&okZ);
            if (!okX || !okY || !okZ) {
                LogManager::log("Invalid coordinates", LogManager::Warning);
                return;
            }
            mankool::mcbot::protocol::Vec3d pos;
            pos.setX(x);
            pos.setY(y);
            pos.setZ(z);
            lookAtCmd.setPosition(pos);
        }
        msg.setLookAt(lookAtCmd);
    }
    else if (cmd == "rotate") {
        if (parts.size() < 3) {
            LogManager::log("Usage: rotate <yaw> <pitch>", LogManager::Warning);
            return;
        }
        bool okYaw, okPitch;
        float yaw = parts[1].toFloat(&okYaw);
        float pitch = parts[2].toFloat(&okPitch);
        if (!okYaw || !okPitch) {
            LogManager::log("Invalid rotation values", LogManager::Warning);
            return;
        }
        mankool::mcbot::protocol::SetRotationCommand rotateCmd;
        rotateCmd.setYaw(yaw);
        rotateCmd.setPitch(pitch);
        msg.setSetRotation(rotateCmd);
    }
    else if (cmd == "hotbar") {
        if (parts.size() < 2) {
            LogManager::log("Usage: hotbar <slot>", LogManager::Warning);
            return;
        }
        bool ok;
        int slot = parts[1].toInt(&ok);
        if (!ok || slot < 0 || slot > 8) {
            LogManager::log("Slot must be 0-8", LogManager::Warning);
            return;
        }
        mankool::mcbot::protocol::SwitchHotbarSlotCommand hotbarCmd;
        hotbarCmd.setSlot(slot);
        msg.setSwitchHotbar(hotbarCmd);
    }
    else if (cmd == "use") {
        mankool::mcbot::protocol::UseItemCommand useCmd;
        if (parts.size() > 1 && parts[1].toLower() == "offhand") {
            useCmd.setHand(mankool::mcbot::protocol::HandGadget::Hand::OFF_HAND);
        } else {
            useCmd.setHand(mankool::mcbot::protocol::HandGadget::Hand::MAIN_HAND);
        }
        msg.setUseItem(useCmd);
    }
    else if (cmd == "drop") {
        mankool::mcbot::protocol::DropItemCommand dropCmd;
        dropCmd.setDropAll(parts.size() > 1 && parts[1].toLower() == "all");
        msg.setDropItem(dropCmd);
    }
    else if (cmd == "shutdown") {
        mankool::mcbot::protocol::ShutdownCommand shutdownCmd;
        shutdownCmd.setReason(parts.size() > 1 ? parts.mid(1).join(' ') : "Console command");
        msg.setShutdown(shutdownCmd);
    }
    else {
        LogManager::log(QString("Unknown command: %1").arg(cmd), LogManager::Warning);
        return;
    }

    QProtobufSerializer serializer;
    QByteArray protoData = serializer.serialize(&msg);
    if (protoData.isEmpty()) {
        LogManager::log(QString("Failed to serialize command '%1' for bot '%2'").arg(commandText, botName), LogManager::Error);
        return;
    }

    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(protoData.size());
    message.append(protoData);

    PipeServer::sendToClient(bot->connectionId, message);
    LogManager::log(QString("Sent command to %1: %2").arg(botName, commandText), LogManager::Info);
}

void BotManager::handleHeartbeat(int connectionId, const mankool::mcbot::protocol::HeartbeatMessage &heartbeat)
{
    instance().handleHeartbeatImpl(connectionId, heartbeat);
}

void BotManager::handleHeartbeatImpl(int connectionId, const mankool::mcbot::protocol::HeartbeatMessage &heartbeat)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);

    if (bot) {
        // Update current memory usage (convert bytes to MB)
        if (heartbeat.currentMemory() > 0) {
            int newMemory = heartbeat.currentMemory() / (1024 * 1024);
            if (bot->currentMemory != newMemory) {
                bot->currentMemory = newMemory;
                emit botUpdated(bot->name);
            }
        }

        if (bot->debugLogging) {
            LogManager::log(QString("[DEBUG %1] Heartbeat received").arg(bot->name), LogManager::Debug);
        }
    }
}
