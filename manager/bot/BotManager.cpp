#include "BotManager.h"
#include "logging/LogManager.h"
#include "network/PipeServer.h"
#include "ui/BotConsoleWidget.h"
#include "ui/MeteorModulesWidget.h"
#include "ui/BaritoneWidget.h"
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
            if (botInstances[i].consoleWidget) {
                delete botInstances[i].consoleWidget;
                botInstances[i].consoleWidget = nullptr;
            }
            if (botInstances[i].meteorWidget) {
                delete botInstances[i].meteorWidget;
                botInstances[i].meteorWidget = nullptr;
            }
            if (botInstances[i].baritoneWidget) {
                delete botInstances[i].baritoneWidget;
                botInstances[i].baritoneWidget = nullptr;
            }

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

void BotManager::handleModulesResponse(int connectionId, const mankool::mcbot::protocol::GetModulesResponse &response)
{
    instance().handleModulesResponseImpl(connectionId, response);
}

void BotManager::handleModulesResponseImpl(int connectionId, const mankool::mcbot::protocol::GetModulesResponse &response)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (!bot) return;

    bot->meteorModules.clear();
    for (const auto &protoModule : response.modules()) {
        MeteorModuleData moduleData;
        moduleData.name = protoModule.name();
        moduleData.category = protoModule.category();
        moduleData.description = protoModule.description();
        moduleData.enabled = protoModule.enabled();

        for (const auto &protoSetting : protoModule.settings()) {
            MeteorSettingData settingData;
            settingData.name = protoSetting.name();
            settingData.groupName = protoSetting.hasGroupName() ? protoSetting.groupName() : QString();
            settingData.currentValue = protoSetting.currentValue();
            settingData.description = protoSetting.hasDescription() ? protoSetting.description() : QString();
            settingData.type = protoSetting.type();
            settingData.hasMin = protoSetting.hasMinValue();
            settingData.hasMax = protoSetting.hasMaxValue();
            settingData.minValue = protoSetting.hasMinValue() ? protoSetting.minValue() : 0.0;
            settingData.maxValue = protoSetting.hasMaxValue() ? protoSetting.maxValue() : 0.0;
            settingData.possibleValues = QStringList();
            for (const auto &val : protoSetting.possibleValues()) {
                settingData.possibleValues.append(val);
            }

            QString settingPath = getSettingPath(protoSetting);
            moduleData.settings.insert(settingPath, settingData);
        }

        bot->meteorModules.insert(moduleData.name, moduleData);
    }

    QString output = QString("=== Meteor Modules (%1) ===\n").arg(response.modules().size());

    for (const auto &module : response.modules()) {
        QString statusIcon = module.enabled() ? "[✓]" : "[ ]";
        output += QString("%1 %2 (%3)\n").arg(statusIcon, module.name(), module.category());

        if (!module.description().isEmpty()) {
            output += QString("    %1\n").arg(module.description());
        }

        if (!module.settings().empty()) {
            output += "    Settings:\n";

            QMap<QString, QVector<const mankool::mcbot::protocol::SettingInfo*>> groupedSettings;
            for (const auto &setting : module.settings()) {
                QString group = setting.hasGroupName() ? setting.groupName() : QString();
                groupedSettings[group].append(&setting);
            }

            for (auto it = groupedSettings.constBegin(); it != groupedSettings.constEnd(); ++it) {
                const QString &groupName = it.key();
                const auto &settings = it.value();

                if (!groupName.isEmpty()) {
                    output += QString("      [%1]\n").arg(groupName);
                }

                for (const auto *setting : settings) {
                    QString settingPath = getSettingPath(*setting);
                    output += QString("        %1 = %2\n").arg(settingPath, setting->currentValue());
                }
            }
        }
        output += "\n";
    }


    LogManager::log(QString("[%1] Received %2 Meteor modules").arg(bot->name).arg(response.modules().size()), LogManager::Info);

    emit meteorModulesReceived(bot->name);
}

void BotManager::handleModuleConfigResponse(int connectionId, const mankool::mcbot::protocol::SetModuleConfigResponse &response)
{
    instance().handleModuleConfigResponseImpl(connectionId, response);
}

void BotManager::handleModuleConfigResponseImpl(int connectionId, const mankool::mcbot::protocol::SetModuleConfigResponse &response)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (!bot) return;

    QString output;
    QString updatedModuleName;
    bool moduleFound = false;
    if (response.success()) {
        const auto &protoModule = response.updatedModule();
        updatedModuleName = protoModule.name();

        if (bot->meteorModules.contains(protoModule.name())) {
            MeteorModuleData &moduleData = bot->meteorModules[protoModule.name()];
            moduleData.enabled = protoModule.enabled();
            moduleData.category = protoModule.category();
            moduleData.description = protoModule.description();

            moduleData.settings.clear();
            for (const auto &protoSetting : protoModule.settings()) {
                MeteorSettingData settingData;
                settingData.name = protoSetting.name();
                settingData.groupName = protoSetting.hasGroupName() ? protoSetting.groupName() : QString();
                settingData.currentValue = protoSetting.currentValue();
                settingData.description = protoSetting.hasDescription() ? protoSetting.description() : QString();
                settingData.type = protoSetting.type();
                settingData.hasMin = protoSetting.hasMinValue();
                settingData.hasMax = protoSetting.hasMaxValue();
                settingData.minValue = protoSetting.hasMinValue() ? protoSetting.minValue() : 0.0;
                settingData.maxValue = protoSetting.hasMaxValue() ? protoSetting.maxValue() : 0.0;
                settingData.possibleValues = QStringList();
                for (const auto &val : protoSetting.possibleValues()) {
                    settingData.possibleValues.append(val);
                }

                QString settingPath = getSettingPath(protoSetting);
                moduleData.settings.insert(settingPath, settingData);
            }
            moduleFound = true;
        }

        QString statusIcon = protoModule.enabled() ? "[✓]" : "[ ]";
        output = QString("Module updated: %1 %2").arg(statusIcon, protoModule.name());

        if (!protoModule.settings().empty()) {
            output += "\nSettings:";

            QMap<QString, QVector<const mankool::mcbot::protocol::SettingInfo*>> groupedSettings;
            for (const auto &setting : protoModule.settings()) {
                QString group = setting.hasGroupName() ? setting.groupName() : QString();
                groupedSettings[group].append(&setting);
            }

            for (auto it = groupedSettings.constBegin(); it != groupedSettings.constEnd(); ++it) {
                const QString &groupName = it.key();
                const auto &settings = it.value();

                if (!groupName.isEmpty()) {
                    output += QString("\n  [%1]").arg(groupName);
                }

                for (const auto *setting : settings) {
                    QString settingPath = getSettingPath(*setting);
                    output += QString("\n    %1 = %2").arg(settingPath, setting->currentValue());
                }
            }
        }
    } else {
        output = QString("Error: %1").arg(response.errorMessage());
    }


    LogManager::log(QString("[%1] %2").arg(bot->name, output),
                    response.success() ? LogManager::Info : LogManager::Warning);

    if (moduleFound) {
        emit meteorSingleModuleUpdated(bot->name, updatedModuleName);
    }
}

void BotManager::handleModuleStateChanged(int connectionId, const mankool::mcbot::protocol::ModuleStateChanged &stateChange)
{
    instance().handleModuleStateChangedImpl(connectionId, stateChange);
}

void BotManager::handleModuleStateChangedImpl(int connectionId, const mankool::mcbot::protocol::ModuleStateChanged &stateChange)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (!bot) return;

    if (bot->meteorModules.contains(stateChange.moduleName())) {
        MeteorModuleData &moduleData = bot->meteorModules[stateChange.moduleName()];

        if (stateChange.hasEnabled()) {
            moduleData.enabled = stateChange.enabled();
        }

        if (!stateChange.changedSettings().isEmpty()) {
            for (auto it = stateChange.changedSettings().constBegin();
                 it != stateChange.changedSettings().constEnd(); ++it) {
                const QString &settingPath = it.key();
                const QString &newValue = it.value();

                if (moduleData.settings.contains(settingPath)) {
                    moduleData.settings[settingPath].currentValue = newValue;
                }
            }
        }

        emit meteorSingleModuleUpdated(bot->name, stateChange.moduleName());
    }
}

// Helper function to parse command text with support for quoted strings
static QStringList parseCommandWithQuotes(const QString &commandText)
{
    QStringList result;
    QString current;
    bool inQuotes = false;
    bool escaped = false;

    for (int i = 0; i < commandText.length(); ++i) {
        QChar c = commandText[i];

        if (escaped) {
            current += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ' && !inQuotes) {
            if (!current.isEmpty()) {
                result.append(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.isEmpty()) {
        result.append(current);
    }

    return result;
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

    mankool::mcbot::protocol::ManagerToClientMessage msg;
    msg.setMessageId(QString::number(QDateTime::currentMSecsSinceEpoch()));
    msg.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    mankool::mcbot::protocol::ShutdownCommand shutdown;
    shutdown.setReason(reason);
    msg.setShutdown(shutdown);

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

    QStringList parts = parseCommandWithQuotes(commandText);
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
    else if (cmd == "meteor") {
        if (parts.size() < 2) {
            LogManager::log("Usage: meteor list [category] | meteor toggle <module> | meteor set <module> <setting|enabled> <value>", LogManager::Warning);
            return;
        }
        QString subCmd = parts[1].toLower();
        if (subCmd == "list") {
            mankool::mcbot::protocol::GetModulesRequest getModulesReq;
            if (parts.size() > 2) {
                getModulesReq.setCategoryFilter(parts[2]);
            }
            msg.setGetModules(getModulesReq);
        }
        else if (subCmd == "toggle") {
            if (parts.size() < 3) {
                LogManager::log("Usage: meteor toggle <module>", LogManager::Warning);
                return;
            }
            mankool::mcbot::protocol::SetModuleConfigCommand setModuleCmd;
            setModuleCmd.setModuleName(parts[2]);
            // Don't set enabled field - bot will determine current state and flip it
            msg.setSetModuleConfig(setModuleCmd);
        }
        else if (subCmd == "set") {
            if (parts.size() < 4) {
                LogManager::log("Usage: meteor set <module> <setting|group.setting|enabled> <value>", LogManager::Warning);
                return;
            }
            mankool::mcbot::protocol::SetModuleConfigCommand setModuleCmd;
            setModuleCmd.setModuleName(parts[2]);

            QString settingName = parts[3];

            // Special handling for "enabled" setting
            if (settingName.toLower() == "enabled") {
                if (parts.size() < 5) {
                    LogManager::log("Usage: meteor set <module> enabled <true|false>", LogManager::Warning);
                    return;
                }
                QString value = parts[4].toLower();
                if (value == "true" || value == "1" || value == "on" || value == "yes") {
                    setModuleCmd.setEnabled(true);
                } else if (value == "false" || value == "0" || value == "off" || value == "no") {
                    setModuleCmd.setEnabled(false);
                } else {
                    LogManager::log("Invalid enabled value. Use: true/false, 1/0, on/off, yes/no", LogManager::Warning);
                    return;
                }
            } else {
                if (parts.size() < 5) {
                    LogManager::log("Usage: meteor set <module> <setting|group.setting> <value>", LogManager::Warning);
                    return;
                }
                QHash<QString, QString> settings;
                QString settingValue = parts.mid(4).join(' ');
                settings.insert(settingName, settingValue);
                setModuleCmd.setSettings(settings);
            }
            msg.setSetModuleConfig(setModuleCmd);
        }
        else {
            LogManager::log(QString("Unknown meteor subcommand: %1").arg(subCmd), LogManager::Warning);
            return;
        }
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
}

QString BotManager::getSettingPath(const mankool::mcbot::protocol::SettingInfo &setting)
{
    return setting.hasGroupName() && !setting.groupName().isEmpty()
        ? QString("%1.%2").arg(setting.groupName(), setting.name())
        : setting.name();
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

void BotManager::handleBaritoneSettingsResponse(int connectionId, const mankool::mcbot::protocol::GetBaritoneSettingsResponse &response)
{
    instance().handleBaritoneSettingsResponseImpl(connectionId, response);
}

void BotManager::handleBaritoneSettingsResponseImpl(int connectionId, const mankool::mcbot::protocol::GetBaritoneSettingsResponse &response)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (!bot) return;

    bot->baritoneSettings.clear();
    for (const auto &protoSetting : response.settings()) {
        BaritoneSettingData settingData;
        settingData.name = protoSetting.name();
        settingData.type = protoSetting.type();
        settingData.currentValue = protoSetting.currentValue();
        settingData.defaultValue = protoSetting.defaultValue();
        settingData.description = protoSetting.hasDescription() ? protoSetting.description() : QString();

        bot->baritoneSettings.insert(settingData.name, settingData);
    }

    LogManager::log(QString("[%1] Received %2 Baritone settings").arg(bot->name).arg(response.settings().size()), LogManager::Info);

    emit baritoneSettingsReceived(bot->name);
}

void BotManager::handleBaritoneCommandsResponse(int connectionId, const mankool::mcbot::protocol::GetBaritoneCommandsResponse &response)
{
    instance().handleBaritoneCommandsResponseImpl(connectionId, response);
}

void BotManager::handleBaritoneCommandsResponseImpl(int connectionId, const mankool::mcbot::protocol::GetBaritoneCommandsResponse &response)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (!bot) return;

    bot->baritoneCommands.clear();
    for (const auto &protoCommand : response.commands()) {
        BaritoneCommandData commandData;
        commandData.name = protoCommand.name();
        commandData.aliases = QStringList();
        for (const auto &alias : protoCommand.aliases()) {
            commandData.aliases.append(alias);
        }
        commandData.shortDesc = protoCommand.shortDesc();
        commandData.longDesc = QStringList();
        for (const auto &line : protoCommand.longDesc()) {
            commandData.longDesc.append(line);
        }

        bot->baritoneCommands.insert(commandData.name, commandData);
    }

    LogManager::log(QString("[%1] Received %2 Baritone commands").arg(bot->name).arg(response.commands().size()), LogManager::Info);

    emit baritoneCommandsReceived(bot->name);
}

void BotManager::handleBaritoneSettingsSetResponse(int connectionId, const mankool::mcbot::protocol::SetBaritoneSettingsResponse &response)
{
    instance().handleBaritoneSettingsSetResponseImpl(connectionId, response);
}

void BotManager::handleBaritoneSettingsSetResponseImpl(int connectionId, const mankool::mcbot::protocol::SetBaritoneSettingsResponse &response)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (!bot) return;

    QString output;
    if (response.success()) {
        QStringList changedSettings;
        for (const auto &updatedSetting : response.updatedSettings()) {
            changedSettings.append(QString("%1 = %2").arg(updatedSetting.name(), updatedSetting.currentValue()));
        }

        if (!changedSettings.isEmpty()) {
            output = QString("Baritone settings updated: %1").arg(changedSettings.join(", "));
        } else {
            output = QString("Baritone settings updated: %1").arg(response.result());
        }

        for (const auto &protoSetting : response.updatedSettings()) {
            if (bot->baritoneSettings.contains(protoSetting.name())) {
                BaritoneSettingData &settingData = bot->baritoneSettings[protoSetting.name()];
                settingData.currentValue = protoSetting.currentValue();
                settingData.defaultValue = protoSetting.defaultValue();
                settingData.type = protoSetting.type();
                if (protoSetting.hasDescription()) {
                    settingData.description = protoSetting.description();
                }
                emit baritoneSingleSettingUpdated(bot->name, protoSetting.name());
            }
        }
    } else {
        output = QString("Error: %1").arg(response.result());
    }

    LogManager::log(QString("[%1] %2").arg(bot->name, output),
                    response.success() ? LogManager::Info : LogManager::Warning);
}

void BotManager::handleBaritoneCommandResponse(int connectionId, const mankool::mcbot::protocol::ExecuteBaritoneCommandResponse &response)
{
    instance().handleBaritoneCommandResponseImpl(connectionId, response);
}

void BotManager::handleBaritoneCommandResponseImpl(int connectionId, const mankool::mcbot::protocol::ExecuteBaritoneCommandResponse &response)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (!bot) return;

    QString output = response.result();

    if (bot->consoleWidget) {
        bot->consoleWidget->appendResponse(response.success(), output);
    }

    LogManager::log(QString("[%1] %2").arg(bot->name, output),
                    response.success() ? LogManager::Info : LogManager::Warning);
}

void BotManager::handleBaritoneSettingUpdate(int connectionId, const mankool::mcbot::protocol::BaritoneSettingUpdate &update)
{
    instance().handleBaritoneSettingUpdateImpl(connectionId, update);
}

void BotManager::handleBaritoneSettingUpdateImpl(int connectionId, const mankool::mcbot::protocol::BaritoneSettingUpdate &update)
{
    BotInstance *bot = getBotByConnectionIdImpl(connectionId);
    if (!bot) return;

    if (bot->baritoneSettings.contains(update.settingName())) {
        bot->baritoneSettings[update.settingName()].currentValue = update.newValue();
        emit baritoneSingleSettingUpdated(bot->name, update.settingName());
    }
}

void BotManager::requestBaritoneSettings(const QString &botName)
{
    instance().requestBaritoneSettingsImpl(botName);
}

void BotManager::requestBaritoneSettingsImpl(const QString &botName)
{
    BotInstance *bot = getBotByNameImpl(botName);
    if (!bot) {
        LogManager::log(QString("Cannot request Baritone settings: bot '%1' not found").arg(botName), LogManager::Warning);
        return;
    }

    if (bot->connectionId <= 0) {
        LogManager::log(QString("Cannot request Baritone settings: bot '%1' not connected").arg(botName), LogManager::Warning);
        return;
    }

    mankool::mcbot::protocol::ManagerToClientMessage msg;
    msg.setMessageId(QString::number(QDateTime::currentMSecsSinceEpoch()));
    msg.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    mankool::mcbot::protocol::GetBaritoneSettingsRequest request;
    msg.setGetBaritoneSettings(request);

    QProtobufSerializer serializer;
    QByteArray protoData = serializer.serialize(&msg);
    if (protoData.isEmpty()) {
        LogManager::log(QString("Failed to serialize Baritone settings request for bot '%1'").arg(botName), LogManager::Error);
        return;
    }

    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(protoData.size());
    message.append(protoData);

    PipeServer::sendToClient(bot->connectionId, message);
}

void BotManager::requestBaritoneCommands(const QString &botName)
{
    instance().requestBaritoneCommandsImpl(botName);
}

void BotManager::requestBaritoneCommandsImpl(const QString &botName)
{
    BotInstance *bot = getBotByNameImpl(botName);
    if (!bot) {
        LogManager::log(QString("Cannot request Baritone commands: bot '%1' not found").arg(botName), LogManager::Warning);
        return;
    }

    if (bot->connectionId <= 0) {
        LogManager::log(QString("Cannot request Baritone commands: bot '%1' not connected").arg(botName), LogManager::Warning);
        return;
    }

    mankool::mcbot::protocol::ManagerToClientMessage msg;
    msg.setMessageId(QString::number(QDateTime::currentMSecsSinceEpoch()));
    msg.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    mankool::mcbot::protocol::GetBaritoneCommandsRequest request;
    msg.setGetBaritoneCommands(request);

    QProtobufSerializer serializer;
    QByteArray protoData = serializer.serialize(&msg);
    if (protoData.isEmpty()) {
        LogManager::log(QString("Failed to serialize Baritone commands request for bot '%1'").arg(botName), LogManager::Error);
        return;
    }

    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(protoData.size());
    message.append(protoData);

    PipeServer::sendToClient(bot->connectionId, message);
}

void BotManager::sendBaritoneCommand(const QString &botName, const QString &commandText)
{
    instance().sendBaritoneCommandImpl(botName, commandText);
}

void BotManager::sendBaritoneCommandImpl(const QString &botName, const QString &commandText)
{
    BotInstance *bot = getBotByNameImpl(botName);
    if (!bot) {
        LogManager::log(QString("Cannot send Baritone command: bot '%1' not found").arg(botName), LogManager::Warning);
        return;
    }

    if (bot->connectionId <= 0) {
        LogManager::log(QString("Cannot send Baritone command: bot '%1' not connected").arg(botName), LogManager::Warning);
        return;
    }

    mankool::mcbot::protocol::ManagerToClientMessage msg;
    msg.setMessageId(QString::number(QDateTime::currentMSecsSinceEpoch()));
    msg.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    mankool::mcbot::protocol::ExecuteBaritoneCommand execCmd;
    execCmd.setCommand(commandText);
    msg.setExecuteBaritoneCommand(execCmd);

    QProtobufSerializer serializer;
    QByteArray protoData = serializer.serialize(&msg);
    if (protoData.isEmpty()) {
        LogManager::log(QString("Failed to serialize Baritone command for bot '%1'").arg(botName), LogManager::Error);
        return;
    }

    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(protoData.size());
    message.append(protoData);

    PipeServer::sendToClient(bot->connectionId, message);
}

void BotManager::sendBaritoneSettingChange(const QString &botName, const QString &settingName, const QString &value)
{
    instance().sendBaritoneSettingChangeImpl(botName, settingName, value);
}

void BotManager::sendBaritoneSettingChangeImpl(const QString &botName, const QString &settingName, const QString &value)
{
    BotInstance *bot = getBotByNameImpl(botName);
    if (!bot) {
        LogManager::log(QString("Cannot change Baritone setting: bot '%1' not found").arg(botName), LogManager::Warning);
        return;
    }

    if (bot->connectionId <= 0) {
        LogManager::log(QString("Cannot change Baritone setting: bot '%1' not connected").arg(botName), LogManager::Warning);
        return;
    }

    mankool::mcbot::protocol::ManagerToClientMessage msg;
    msg.setMessageId(QString::number(QDateTime::currentMSecsSinceEpoch()));
    msg.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    mankool::mcbot::protocol::SetBaritoneSettingsCommand setCmd;
    QHash<QString, QString> settings;
    settings.insert(settingName, value);
    setCmd.setSettings(settings);
    msg.setSetBaritoneSettings(setCmd);

    QProtobufSerializer serializer;
    QByteArray protoData = serializer.serialize(&msg);
    if (protoData.isEmpty()) {
        LogManager::log(QString("Failed to serialize Baritone setting change for bot '%1'").arg(botName), LogManager::Error);
        return;
    }

    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(protoData.size());
    message.append(protoData);

    PipeServer::sendToClient(bot->connectionId, message);
}
