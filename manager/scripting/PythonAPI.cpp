#include "PythonAPI.h"
#include "bot/BotManager.h"
#include "ui/BotConsoleWidget.h"
#include "prism/PrismLauncherManager.h"
#include "world/ItemRegistry.h"
#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#include <QReadWriteLock>
#include <pybind11/stl.h>

thread_local QString PythonAPI::currentBot;
thread_local QString PythonAPI::currentScript;

void PythonAPI::setCurrentBot(const QString &botName)
{
    currentBot = botName;
}

QString PythonAPI::getCurrentBot()
{
    return currentBot;
}

void PythonAPI::setCurrentScript(const QString &scriptName)
{
    currentScript = scriptName;
}

QString PythonAPI::getCurrentScript()
{
    return currentScript;
}

QString PythonAPI::resolveBotName(const std::string &botName)
{
    return botName.empty() ? currentBot : QString::fromStdString(botName);
}

BotInstance* PythonAPI::ensureBotOnline(const QString &botName)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (!bot || bot->status != BotStatus::Online) {
        throw std::runtime_error("Bot is not online");
    }
    return bot;
}

QVariant PythonAPI::pyObjectToQVariant(const py::object &value)
{
    if (py::isinstance<py::bool_>(value)) {
        return value.cast<bool>();
    } else if (py::isinstance<py::int_>(value)) {
        return value.cast<int>();
    } else if (py::isinstance<py::float_>(value)) {
        return value.cast<double>();
    } else if (py::isinstance<py::str>(value)) {
        return QString::fromStdString(value.cast<std::string>());
    } else if (py::isinstance<py::list>(value)) {
        py::list pyList = value.cast<py::list>();

        bool isStringList = true;
        for (const auto &item : pyList) {
            if (!py::isinstance<py::str>(item)) {
                isStringList = false;
                break;
            }
        }

        if (isStringList) {
            QStringList qList;
            for (const auto &item : pyList) {
                qList.append(QString::fromStdString(item.cast<std::string>()));
            }
            return qList;
        } else {
            QVariantList qList;
            for (const auto &item : pyList) {
                qList.append(pyObjectToQVariant(py::cast<py::object>(item)));
            }
            return qList;
        }
    } else if (py::isinstance<py::dict>(value)) {
        py::dict pyDict = value.cast<py::dict>();

        // Check for __type__ key to determine what to convert to
        if (pyDict.contains("__type__")) {
            std::string type = pyDict["__type__"].cast<std::string>();

            if (type == "RGBColor") {
                RGBColor color;
                color.red = pyDict["red"].cast<int>();
                color.green = pyDict["green"].cast<int>();
                color.blue = pyDict["blue"].cast<int>();
                return QVariant::fromValue(color);
            } else if (type == "RGBAColor") {
                RGBAColor color;
                color.red = pyDict["red"].cast<int>();
                color.green = pyDict["green"].cast<int>();
                color.blue = pyDict["blue"].cast<int>();
                color.alpha = pyDict["alpha"].cast<int>();
                return QVariant::fromValue(color);
            } else if (type == "Vec3i") {
                Vec3i vec;
                vec.x = pyDict["x"].cast<int>();
                vec.y = pyDict["y"].cast<int>();
                vec.z = pyDict["z"].cast<int>();
                return QVariant::fromValue(vec);
            } else if (type == "Vector3d") {
                Vector3d vec;
                vec.x = pyDict["x"].cast<double>();
                vec.y = pyDict["y"].cast<double>();
                vec.z = pyDict["z"].cast<double>();
                return QVariant::fromValue(vec);
            } else if (type == "Keybind") {
                Keybind keybind;
                keybind.keyName = QString::fromStdString(pyDict["key"].cast<std::string>());
                return QVariant::fromValue(keybind);
            } else if (type == "ESPBlockData") {
                ESPBlockData data;
                data.shapeMode = static_cast<ESPBlockData::ShapeMode>(pyDict["shape_mode"].cast<int>());

                py::dict lineColor = pyDict["line_color"].cast<py::dict>();
                data.lineColor.red = lineColor["red"].cast<int>();
                data.lineColor.green = lineColor["green"].cast<int>();
                data.lineColor.blue = lineColor["blue"].cast<int>();
                data.lineColor.alpha = lineColor["alpha"].cast<int>();

                py::dict sideColor = pyDict["side_color"].cast<py::dict>();
                data.sideColor.red = sideColor["red"].cast<int>();
                data.sideColor.green = sideColor["green"].cast<int>();
                data.sideColor.blue = sideColor["blue"].cast<int>();
                data.sideColor.alpha = sideColor["alpha"].cast<int>();

                data.tracer = pyDict["tracer"].cast<bool>();

                py::dict tracerColor = pyDict["tracer_color"].cast<py::dict>();
                data.tracerColor.red = tracerColor["red"].cast<int>();
                data.tracerColor.green = tracerColor["green"].cast<int>();
                data.tracerColor.blue = tracerColor["blue"].cast<int>();
                data.tracerColor.alpha = tracerColor["alpha"].cast<int>();

                return QVariant::fromValue(data);
            } else if (type == "StringMap") {
                StringMap map;
                for (const auto &item : pyDict) {
                    QString key = QString::fromStdString(item.first.cast<std::string>());
                    if (key == "__type__") continue;
                    QString val = QString::fromStdString(item.second.cast<std::string>());
                    map[key] = val;
                }
                return QVariant::fromValue(map);
            } else if (type == "StringListMap") {
                StringListMap map;
                for (const auto &item : pyDict) {
                    QString key = QString::fromStdString(item.first.cast<std::string>());
                    if (key == "__type__") continue;
                    py::list pyList = item.second.cast<py::list>();
                    QStringList qList;
                    for (const auto &listItem : pyList) {
                        qList.append(QString::fromStdString(listItem.cast<std::string>()));
                    }
                    map[key] = qList;
                }
                return QVariant::fromValue(map);
            } else if (type == "ESPBlockDataMap") {
                ESPBlockDataMap map;
                for (const auto &item : pyDict) {
                    QString key = QString::fromStdString(item.first.cast<std::string>());
                    if (key == "__type__") continue;
                    py::dict valueDict = item.second.cast<py::dict>();

                    ESPBlockData data;
                    data.shapeMode = static_cast<ESPBlockData::ShapeMode>(valueDict["shape_mode"].cast<int>());

                    py::dict lineColor = valueDict["line_color"].cast<py::dict>();
                    data.lineColor.red = lineColor["red"].cast<int>();
                    data.lineColor.green = lineColor["green"].cast<int>();
                    data.lineColor.blue = lineColor["blue"].cast<int>();
                    data.lineColor.alpha = lineColor["alpha"].cast<int>();

                    py::dict sideColor = valueDict["side_color"].cast<py::dict>();
                    data.sideColor.red = sideColor["red"].cast<int>();
                    data.sideColor.green = sideColor["green"].cast<int>();
                    data.sideColor.blue = sideColor["blue"].cast<int>();
                    data.sideColor.alpha = sideColor["alpha"].cast<int>();

                    data.tracer = valueDict["tracer"].cast<bool>();

                    py::dict tracerColor = valueDict["tracer_color"].cast<py::dict>();
                    data.tracerColor.red = tracerColor["red"].cast<int>();
                    data.tracerColor.green = tracerColor["green"].cast<int>();
                    data.tracerColor.blue = tracerColor["blue"].cast<int>();
                    data.tracerColor.alpha = tracerColor["alpha"].cast<int>();

                    map[key] = data;
                }
                return QVariant::fromValue(map);
            }
        }

        // No __type__ key - treat as regular QVariantMap
        QVariantMap qMap;
        for (const auto &item : pyDict) {
            QString key = QString::fromStdString(item.first.cast<std::string>());
            qMap[key] = pyObjectToQVariant(py::cast<py::object>(item.second));
        }
        return qMap;
    } else {
        throw py::type_error("Unsupported value type");
    }
}

py::dict PythonAPI::rgbaColorToDict(const RGBAColor &color)
{
    py::dict dict;
    dict["__type__"] = "RGBAColor";
    dict["red"] = color.red;
    dict["green"] = color.green;
    dict["blue"] = color.blue;
    dict["alpha"] = color.alpha;
    return dict;
}

py::dict PythonAPI::espBlockDataToDict(const ESPBlockData &data)
{
    py::dict dict;
    dict["__type__"] = "ESPBlockData";
    dict["shape_mode"] = static_cast<int>(data.shapeMode);
    dict["line_color"] = rgbaColorToDict(data.lineColor);
    dict["side_color"] = rgbaColorToDict(data.sideColor);
    dict["tracer"] = data.tracer;
    dict["tracer_color"] = rgbaColorToDict(data.tracerColor);
    return dict;
}

py::object PythonAPI::qVariantToPyObject(const QVariant &value)
{
    switch (value.typeId()) {
        case QMetaType::Bool:
            return py::cast(value.toBool());
        case QMetaType::Int:
            return py::cast(value.toInt());
        case QMetaType::LongLong:
            return py::cast(value.toLongLong());
        case QMetaType::Float:
            return py::cast(value.toFloat());
        case QMetaType::Double:
            return py::cast(value.toDouble());
        case QMetaType::QString:
            return py::cast(value.toString().toStdString());
        case QMetaType::QStringList: {
            QStringList list = value.toStringList();
            py::list pyList;
            for (const QString &str : std::as_const(list)) {
                pyList.append(str.toStdString());
            }
            return pyList;
        }
        case QMetaType::QVariantList: {
            QVariantList list = value.toList();
            py::list pyList;
            for (const QVariant &item : std::as_const(list)) {
                pyList.append(qVariantToPyObject(item));
            }
            return pyList;
        }
        case QMetaType::QVariantMap: {
            QVariantMap map = value.toMap();
            py::dict pyDict;
            for (auto it = map.begin(); it != map.end(); ++it) {
                pyDict[it.key().toStdString().c_str()] = qVariantToPyObject(it.value());
            }
            return pyDict;
        }
        default: {
            // Handle custom metatypes
            int typeId = value.userType();

            if (typeId == qMetaTypeId<RGBColor>()) {
                RGBColor color = value.value<RGBColor>();
                py::dict dict;
                dict["__type__"] = "RGBColor";
                dict["red"] = color.red;
                dict["green"] = color.green;
                dict["blue"] = color.blue;
                return dict;
            } else if (typeId == qMetaTypeId<RGBAColor>()) {
                return rgbaColorToDict(value.value<RGBAColor>());
            } else if (typeId == qMetaTypeId<Vec3i>()) {
                Vec3i vec = value.value<Vec3i>();
                py::dict dict;
                dict["__type__"] = "Vec3i";
                dict["x"] = vec.x;
                dict["y"] = vec.y;
                dict["z"] = vec.z;
                return dict;
            } else if (typeId == qMetaTypeId<Vector3d>()) {
                Vector3d vec = value.value<Vector3d>();
                py::dict dict;
                dict["__type__"] = "Vector3d";
                dict["x"] = vec.x;
                dict["y"] = vec.y;
                dict["z"] = vec.z;
                return dict;
            } else if (typeId == qMetaTypeId<Keybind>()) {
                Keybind keybind = value.value<Keybind>();
                py::dict dict;
                dict["__type__"] = "Keybind";
                dict["key"] = keybind.keyName.toStdString();
                return dict;
            } else if (typeId == qMetaTypeId<ESPBlockData>()) {
                return espBlockDataToDict(value.value<ESPBlockData>());
            } else if (typeId == qMetaTypeId<StringMap>()) {
                StringMap map = value.value<StringMap>();
                py::dict dict;
                dict["__type__"] = "StringMap";
                for (auto it = map.begin(); it != map.end(); ++it) {
                    dict[it.key().toStdString().c_str()] = it.value().toStdString();
                }
                return dict;
            } else if (typeId == qMetaTypeId<StringListMap>()) {
                StringListMap map = value.value<StringListMap>();
                py::dict dict;
                dict["__type__"] = "StringListMap";
                for (auto it = map.begin(); it != map.end(); ++it) {
                    py::list pyList;
                    for (const QString &str : it.value()) {
                        pyList.append(str.toStdString());
                    }
                    dict[it.key().toStdString().c_str()] = pyList;
                }
                return dict;
            } else if (typeId == qMetaTypeId<ESPBlockDataMap>()) {
                ESPBlockDataMap map = value.value<ESPBlockDataMap>();
                py::dict dict;
                dict["__type__"] = "ESPBlockDataMap";
                for (auto it = map.begin(); it != map.end(); ++it) {
                    dict[it.key().toStdString().c_str()] = espBlockDataToDict(it.value());
                }
                return dict;
            }

            return py::none();
        }
    }
}

py::object PythonAPI::getPosition(const std::string &botName)
{
    QString name = resolveBotName(botName);

    // Release GIL before locking mutex to avoid deadlock
    py::gil_scoped_release release;

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        py::gil_scoped_acquire acquire;
        return py::none();
    }

    QMutexLocker locker(bot->dataMutex.get());
    double x = bot->position.x();
    double y = bot->position.y();
    double z = bot->position.z();
    locker.unlock();

    // Re-acquire GIL before creating Python objects
    py::gil_scoped_acquire acquire;
    py::dict result;
    result["x"] = x;
    result["y"] = y;
    result["z"] = z;
    return result;
}

py::object PythonAPI::getDimension(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online || bot->dimension.isEmpty()) {
        return py::none();
    }

    return py::cast(bot->dimension.toStdString());
}

py::object PythonAPI::getHealth(const std::string &botName)
{
    QString name = resolveBotName(botName);

    py::gil_scoped_release release;

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        py::gil_scoped_acquire acquire;
        return py::none();
    }

    QMutexLocker locker(bot->dataMutex.get());
    float health = bot->health;
    locker.unlock();

    py::gil_scoped_acquire acquire;
    return py::cast(health);
}

py::object PythonAPI::getHunger(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    return py::cast(bot->foodLevel);
}

py::object PythonAPI::getSaturation(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    return py::cast(bot->saturation);
}

py::object PythonAPI::getAir(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    return py::cast(bot->air);
}

py::object PythonAPI::getExperienceLevel(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    return py::cast(bot->experienceLevel);
}

py::object PythonAPI::getExperienceProgress(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    return py::cast(bot->experienceProgress);
}

py::object PythonAPI::getSelectedSlot(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    return py::cast(bot->selectedSlot);
}

py::object PythonAPI::getServer(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->server.isEmpty()) {
        return py::none();
    }

    return py::cast(bot->server.toStdString());
}

py::object PythonAPI::getAccount(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->account.isEmpty()) {
        return py::none();
    }

    return py::cast(bot->account.toStdString());
}

py::object PythonAPI::getUptime(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || !bot->startTime.isValid()) {
        return py::none();
    }

    qint64 seconds = bot->startTime.secsTo(QDateTime::currentDateTime());
    return py::cast(seconds);
}

bool PythonAPI::isOnline(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (bot) {
        return bot->status == BotStatus::Online && bot->connectionId >= 0;
    }

    return false;
}

std::string PythonAPI::getStatus(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot) {
        throw std::runtime_error("Bot not found: " + name.toStdString());
    }

    switch (bot->status) {
        case BotStatus::Offline: return "Offline";
        case BotStatus::Starting: return "Starting";
        case BotStatus::Online: return "Online";
        case BotStatus::Stopping: return "Stopping";
        case BotStatus::Error: return "Error";
        default: return "Unknown";
    }
}

py::object PythonAPI::getInventory(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    py::list result;
    for (const auto &item : std::as_const(bot->inventory)) {
        if (!item.itemId().isEmpty()) {
            py::dict itemDict;
            itemDict["slot"] = static_cast<int>(item.slot());
            itemDict["item_id"] = item.itemId().toStdString();
            itemDict["count"] = static_cast<int>(item.count());
            itemDict["display_name"] = item.displayName().toStdString();
            result.append(itemDict);
        }
    }

    return result;
}

py::object PythonAPI::getScreen(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    QString screenClass = bot->currentScreenClass;

    // Return None for empty screen (in-game, no GUI)
    if (screenClass.isEmpty()) {
        return py::none();
    }

    return py::cast(screenClass.toStdString());
}

py::dict PythonAPI::getNetworkStats(const std::string &botName)
{
    QString name = resolveBotName(botName);
    py::dict result;

    BotInstance *bot = BotManager::getBotByName(name);
    if (bot) {
        result["bytes_received"] = static_cast<long long>(bot->bytesReceived);
        result["bytes_sent"] = static_cast<long long>(bot->bytesSent);
        result["data_rate_in"] = bot->dataRateIn;
        result["data_rate_out"] = bot->dataRateOut;
    } else {
        result["bytes_received"] = 0LL;
        result["bytes_sent"] = 0LL;
        result["data_rate_in"] = 0.0;
        result["data_rate_out"] = 0.0;
    }

    return result;
}

py::list PythonAPI::listAllBots()
{
    py::list result;

    const QVector<BotInstance> &bots = BotManager::getBots();
    for (const auto &bot : bots) {
        py::dict botDict;
        botDict["name"] = bot.name.toStdString();

        QString statusStr;
        switch (bot.status) {
            case BotStatus::Offline: statusStr = "Offline"; break;
            case BotStatus::Starting: statusStr = "Starting"; break;
            case BotStatus::Online: statusStr = "Online"; break;
            case BotStatus::Stopping: statusStr = "Stopping"; break;
            case BotStatus::Error: statusStr = "Error"; break;
        }
        botDict["status"] = statusStr.toStdString();

        result.append(botDict);
    }

    return result;
}

void PythonAPI::sendChat(const std::string &message, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);

    BotManager::sendCommand(name, QString("chat %1").arg(QString::fromStdString(message)), true);
}

void PythonAPI::sendCommand(const std::string &command, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);

    BotManager::sendCommand(name, QString::fromStdString(command), true);
}

void PythonAPI::startBot(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot) {
        throw std::runtime_error("Bot not found");
    }

    if (bot->status == BotStatus::Online || bot->status == BotStatus::Starting) {
        return;
    }

    if (bot->instance.isEmpty()) {
        throw std::runtime_error("Bot has no instance configured");
    }

    if (bot->account.isEmpty()) {
        throw std::runtime_error("Bot has no account configured");
    }

    bot->status = BotStatus::Starting;
    bot->manualStop = false;

    PrismLauncherManager::launchBot(bot);
}

void PythonAPI::stopBot(const std::string &reason, const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot) {
        throw std::runtime_error("Bot not found");
    }

    if (bot->status == BotStatus::Offline) {
        return;
    }

    bot->status = BotStatus::Stopping;
    bot->manualStop = true;

    QString qReason = reason.empty() ? "Stopped by script" : QString::fromStdString(reason);
    BotManager::sendShutdownCommand(name, qReason);
}

void PythonAPI::restartBot(const std::string &reason, const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot) {
        throw std::runtime_error("Bot not found");
    }

    bot->manualStop = false;
    QString qReason = reason.empty() ? "Restarting by script" : QString::fromStdString(reason);
    BotManager::sendShutdownCommand(name, qReason);
}

void PythonAPI::baritoneGoto(double x, double y, double z, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    BotManager::sendBaritoneCommand(name, QString("goto %1 %2 %3").arg(x).arg(y).arg(z));
}

void PythonAPI::baritoneGoto(double x, double z, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    BotManager::sendBaritoneCommand(name, QString("goto %1 %2").arg(x).arg(z));
}

void PythonAPI::baritoneFollow(const std::string &player, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    BotManager::sendBaritoneCommand(name, QString("follow player %1").arg(QString::fromStdString(player)));
}

void PythonAPI::baritoneCancel(const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    BotManager::sendBaritoneCommand(name, "cancel");
}

void PythonAPI::baritoneMine(const std::string &blockType, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    BotManager::sendBaritoneCommand(name, QString("mine %1").arg(QString::fromStdString(blockType)));
}

void PythonAPI::baritoneFarm(const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    BotManager::sendBaritoneCommand(name, "farm");
}

void PythonAPI::baritoneCommand(const std::string &command, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    BotManager::sendBaritoneCommand(name, QString::fromStdString(command));
}

void PythonAPI::baritoneSetSetting(const std::string &setting, const py::object &value, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    QVariant qValue = pyObjectToQVariant(value);
    BotManager::sendBaritoneSettingChange(name, QString::fromStdString(setting), qValue);
}

py::object PythonAPI::baritoneGetSetting(const std::string &setting, const std::string &bot)
{
    QString name = resolveBotName(bot);

    BotInstance *botInst = BotManager::getBotByName(name);
    if (!botInst) {
        return py::none();
    }

    QString qSetting = QString::fromStdString(setting);
    if (botInst->baritoneSettings.contains(qSetting)) {
        const BaritoneSettingData &data = botInst->baritoneSettings[qSetting];
        return qVariantToPyObject(data.currentValue);
    }

    return py::none();
}

py::dict PythonAPI::baritoneGetProcessStatus(const std::string &bot)
{
    QString name = resolveBotName(bot);

    BotInstance *botInst = BotManager::getBotByName(name);
    if (!botInst) {
        return py::dict();
    }

    const BaritoneProcessStatus &status = botInst->baritoneProcessStatus;

    py::dict result;
    result["is_pathing"] = status.isPathing;
    result["event_type"] = static_cast<int>(status.eventType);

    if (!status.goalDescription.isEmpty()) {
        result["goal_description"] = status.goalDescription.toStdString();
    }

    if (status.hasActiveProcess) {
        py::dict procInfo;
        procInfo["process_name"] = status.activeProcess.processName.toStdString();
        procInfo["display_name"] = status.activeProcess.displayName.toStdString();
        procInfo["priority"] = status.activeProcess.priority;
        procInfo["is_active"] = status.activeProcess.isActive;
        procInfo["is_temporary"] = status.activeProcess.isTemporary;
        result["active_process"] = procInfo;
    }

    if (status.hasEstimatedTicks) {
        result["estimated_ticks_to_goal"] = status.estimatedTicksToGoal;
    }

    if (status.hasTicksRemaining) {
        result["ticks_remaining_in_segment"] = status.ticksRemainingInSegment;
    }

    return result;
}

void PythonAPI::meteorToggle(const std::string &module, const std::string &bot)
{
    QString name = resolveBotName(bot);
    BotInstance *botInst = ensureBotOnline(name);

    QString qModule = QString::fromStdString(module);
    if (botInst->meteorModules.contains(qModule)) {
        bool currentState = botInst->meteorModules[qModule].enabled;
        BotManager::sendCommand(name, QString("meteor set %1 enabled %2").arg(qModule, !currentState ? "true" : "false"), true);
    } else {
        throw py::value_error("Module not found");
    }
}

void PythonAPI::meteorEnable(const std::string &module, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    BotManager::sendCommand(name, QString("meteor set %1 enabled true").arg(QString::fromStdString(module)), true);
}

void PythonAPI::meteorDisable(const std::string &module, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    BotManager::sendCommand(name, QString("meteor set %1 enabled false").arg(QString::fromStdString(module)), true);
}

void PythonAPI::meteorSetSetting(const std::string &module, const std::string &setting,
                                 const py::object &value, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotOnline(name);

    QVariant qValue = pyObjectToQVariant(value);
    BotManager::sendMeteorSettingChange(name, QString::fromStdString(module), QString::fromStdString(setting), qValue);
}

py::object PythonAPI::meteorGetSetting(const std::string &module, const std::string &setting,
                                       const std::string &bot)
{
    QString name = resolveBotName(bot);

    BotInstance *botInst = BotManager::getBotByName(name);
    if (!botInst) {
        return py::none();
    }

    QString qModule = QString::fromStdString(module);
    QString qSetting = QString::fromStdString(setting);
    if (botInst->meteorModules.contains(qModule)) {
        const MeteorModuleData &moduleData = botInst->meteorModules[qModule];
        if (moduleData.settings.contains(qSetting)) {
            const MeteorSettingData &settingData = moduleData.settings[qSetting];
            return qVariantToPyObject(settingData.currentValue);
        }
    }

    return py::none();
}

py::dict PythonAPI::meteorGetModule(const std::string &module, const std::string &bot)
{
    QString name = resolveBotName(bot);
    py::dict result;

    BotInstance *botInst = BotManager::getBotByName(name);
    if (!botInst) {
        return result;
    }

    QString qModule = QString::fromStdString(module);
    if (botInst->meteorModules.contains(qModule)) {
        const MeteorModuleData &moduleData = botInst->meteorModules[qModule];
        result["name"] = moduleData.name.toStdString();
        result["category"] = moduleData.category.toStdString();
        result["description"] = moduleData.description.toStdString();
        result["enabled"] = moduleData.enabled;

        py::dict settings;
        for (auto it = moduleData.settings.begin(); it != moduleData.settings.end(); ++it) {
            const QString &settingName = it.key();
            const MeteorSettingData &settingData = it.value();
            settings[settingName.toStdString().c_str()] = qVariantToPyObject(settingData.currentValue);
        }
        result["settings"] = settings;
    }

    return result;
}

py::list PythonAPI::meteorListModules(const std::string &bot)
{
    QString name = resolveBotName(bot);

    py::gil_scoped_release release;

    BotInstance *botInst = BotManager::getBotByName(name);
    if (!botInst) {
        py::gil_scoped_acquire acquire;
        return py::list();
    }

    QMutexLocker locker(botInst->dataMutex.get());
    QStringList moduleNames = botInst->meteorModules.keys();
    locker.unlock();

    py::gil_scoped_acquire acquire;
    py::list result;
    for (const QString &moduleName : std::as_const(moduleNames)) {
        result.append(moduleName.toStdString());
    }

    return result;
}

void PythonAPI::log(const std::string &message)
{
    QString qMessage = QString::fromStdString(message);
    QString botName = currentBot;
    QString scriptName = currentScript.isEmpty() ? "Script" : currentScript;

    if (!botName.isEmpty()) {
        BotInstance *bot = BotManager::getBotByName(botName);
        if (bot && bot->consoleWidget) {
            QString formattedMsg = QString("[%1] %2").arg(scriptName, qMessage);
            QMetaObject::invokeMethod(bot->consoleWidget, [widget = bot->consoleWidget, msg = formattedMsg]() {
                widget->appendOutput(msg, Qt::darkGreen);
            }, Qt::QueuedConnection);
        }
    }
}

void PythonAPI::error(const std::string &message)
{
    QString qMessage = QString::fromStdString(message);
    QString botName = currentBot;
    QString scriptName = currentScript.isEmpty() ? "Script" : currentScript;

    if (!botName.isEmpty()) {
        BotInstance *bot = BotManager::getBotByName(botName);
        if (bot && bot->consoleWidget) {
            QString formattedMsg = QString("[%1 Error] %2").arg(scriptName, qMessage);
            QMetaObject::invokeMethod(bot->consoleWidget, [widget = bot->consoleWidget, msg = formattedMsg]() {
                widget->appendOutput(msg, Qt::red);
            }, Qt::QueuedConnection);
        }
    }
}

// ============================================================================
// World Data API
// ============================================================================

py::object PythonAPI::getBlock(double x, double y, double z, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    // Convert to integers
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    int iz = static_cast<int>(std::floor(z));

    std::optional<QString> blockOpt;
    {
        QReadLocker locker(botInstance->worldDataLock.get());
        blockOpt = botInstance->worldData.getBlock(ix, iy, iz);
    }

    if (blockOpt.has_value()) {
        return py::str(blockOpt.value().toStdString());
    }
    return py::none();
}

py::list PythonAPI::findBlocks(const std::string &blockType, double centerX, double centerY, double centerZ,
                                int radius, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    QVector3D center(centerX, centerY, centerZ);
    QString blockTypeQ = QString::fromStdString(blockType);

    // Extract search ID once (part before '[' for block states)
    QString searchId = blockTypeQ.contains('[') ? blockTypeQ.left(blockTypeQ.indexOf('[')) : blockTypeQ;

    QVector<QVector3D> results;

    // Release GIL for the entire search operation to avoid blocking main thread
    {
        py::gil_scoped_release release;

        // Calculate chunk bounds
        int minChunkX = static_cast<int>(qFloor((centerX - radius) / 16.0));
        int maxChunkX = static_cast<int>(qFloor((centerX + radius) / 16.0));
        int minChunkZ = static_cast<int>(qFloor((centerZ - radius) / 16.0));
        int maxChunkZ = static_cast<int>(qFloor((centerZ + radius) / 16.0));

        // Get list of chunks to search (brief lock)
        QVector<ChunkPos> chunksToSearch;
        {
            QReadLocker locker(botInstance->worldDataLock.get());
            for (int cx = minChunkX; cx <= maxChunkX; ++cx) {
                for (int cz = minChunkZ; cz <= maxChunkZ; ++cz) {
                    if (botInstance->worldData.isChunkLoaded(cx, cz)) {
                        chunksToSearch.append(ChunkPos{cx, cz});
                    }
                }
            }
        }

    // Now search each chunk with fine-grained locking
    for (const ChunkPos &chunkPos : chunksToSearch) {
        // Copy chunk data under lock
        ChunkData chunkCopy;
        {
            QReadLocker locker(botInstance->worldDataLock.get());
            const ChunkData* chunk = botInstance->worldData.getChunk(chunkPos.x, chunkPos.z);
            if (chunk) {
                chunkCopy = *chunk;
            } else {
                continue;  // Chunk unloaded between checks
            }
        }
        // Lock released - now search the copy without holding lock

        // Search this chunk
        int minY = qMax(static_cast<int>(centerY - radius), -64);
        int maxY = qMin(static_cast<int>(centerY + radius), 320);
        double radiusSq = static_cast<double>(radius) * radius;

        for (int x = 0; x < 16; ++x) {
            int worldX = chunkPos.x * 16 + x;
            double dx = worldX - centerX;
            double dxSq = dx * dx;

            for (int z = 0; z < 16; ++z) {
                int worldZ = chunkPos.z * 16 + z;
                double dz = worldZ - centerZ;
                double dzSq = dz * dz;
                double horizDistSq = dxSq + dzSq;

                if (horizDistSq > radiusSq) continue;

                for (int y = minY; y <= maxY; ++y) {
                    double dy = y - centerY;
                    double distSq = horizDistSq + dy*dy;

                    if (distSq > radiusSq) continue;

                    auto block = chunkCopy.getBlock(x, y, z);
                    if (block) {
                        // Extract block ID (part before '[' for block states)
                        QString blockId = block->contains('[') ? block->left(block->indexOf('[')) : *block;
                        if (blockId == searchId) {
                            results.append(QVector3D(worldX, y, worldZ));
                        }
                    }
                }
            }
        }
    }
    } // Release GIL scope ends - reacquire for Python object creation

    py::list positions;
    for (const QVector3D &pos : results) {
        py::tuple coord = py::make_tuple(pos.x(), pos.y(), pos.z());
        positions.append(coord);
    }

    return positions;
}

py::object PythonAPI::findNearestBlock(const py::list &blockTypes, int maxDistance, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    // Convert Python list to QStringList and extract block IDs
    QStringList types;
    QStringList searchIds;
    for (const auto &item : blockTypes) {
        QString type = QString::fromStdString(item.cast<std::string>());
        types.append(type);
        // Extract ID (part before '[' for block states)
        searchIds.append(type.contains('[') ? type.left(type.indexOf('[')) : type);
    }

    std::optional<QVector3D> nearest;

    // Release GIL for the entire search operation to avoid blocking main thread
    {
        py::gil_scoped_release release;

        // Get bot position (brief lock)
        QVector3D start;
        {
            QReadLocker locker(botInstance->worldDataLock.get());
            start = botInstance->position;
        }

    // Calculate chunk bounds
    int minChunkX = static_cast<int>(qFloor((start.x() - maxDistance) / 16.0));
    int maxChunkX = static_cast<int>(qFloor((start.x() + maxDistance) / 16.0));
    int minChunkZ = static_cast<int>(qFloor((start.z() - maxDistance) / 16.0));
    int maxChunkZ = static_cast<int>(qFloor((start.z() + maxDistance) / 16.0));

    // Get list of chunks to search (brief lock)
    QVector<ChunkPos> chunksToSearch;
    {
        QReadLocker locker(botInstance->worldDataLock.get());
        for (int cx = minChunkX; cx <= maxChunkX; ++cx) {
            for (int cz = minChunkZ; cz <= maxChunkZ; ++cz) {
                if (botInstance->worldData.isChunkLoaded(cx, cz)) {
                    chunksToSearch.append(ChunkPos{cx, cz});
                }
            }
        }
    }

    // Search each chunk with fine-grained locking
    double nearestDistSq = static_cast<double>(maxDistance) * maxDistance;

    for (const ChunkPos &chunkPos : chunksToSearch) {
        // Copy chunk data under lock
        ChunkData chunkCopy;
        {
            QReadLocker locker(botInstance->worldDataLock.get());
            const ChunkData* chunk = botInstance->worldData.getChunk(chunkPos.x, chunkPos.z);
            if (chunk) {
                chunkCopy = *chunk;
            } else {
                continue;
            }
        }
        // Lock released

        // Search this chunk
        int minY = qMax(static_cast<int>(start.y() - maxDistance), -64);
        int maxY = qMin(static_cast<int>(start.y() + maxDistance), 320);

        for (int x = 0; x < 16; ++x) {
            int worldX = chunkPos.x * 16 + x;
            double dx = worldX - start.x();
            double dxSq = dx * dx;

            for (int z = 0; z < 16; ++z) {
                int worldZ = chunkPos.z * 16 + z;
                double dz = worldZ - start.z();
                double dzSq = dz * dz;
                double horizDistSq = dxSq + dzSq;

                if (horizDistSq >= nearestDistSq) continue;

                for (int y = minY; y <= maxY; ++y) {
                    double dy = y - start.y();
                    double distSq = horizDistSq + dy*dy;

                    if (distSq >= nearestDistSq) continue;

                    auto block = chunkCopy.getBlock(x, y, z);
                    if (block) {
                        // Extract block ID (part before '[' for block states)
                        QString blockId = block->contains('[') ? block->left(block->indexOf('[')) : *block;
                        for (const QString& searchId : searchIds) {
                            if (blockId == searchId) {
                                nearest = QVector3D(worldX, y, worldZ);
                                nearestDistSq = distSq;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    } // Release GIL scope ends - reacquire for Python object creation

    if (nearest.has_value()) {
        return py::make_tuple(nearest.value().x(), nearest.value().y(), nearest.value().z());
    }

    return py::none();
}

int PythonAPI::getLoadedChunkCount(const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    QReadLocker locker(botInstance->worldDataLock.get());
    return botInstance->worldData.chunkCount();
}

size_t PythonAPI::getWorldMemoryUsage(const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    QReadLocker locker(botInstance->worldDataLock.get());
    return botInstance->worldData.totalMemoryUsage();
}

py::list PythonAPI::getLoadedChunks(const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    QVector<ChunkPos> chunks;
    {
        QReadLocker locker(botInstance->worldDataLock.get());
        chunks = botInstance->worldData.getLoadedChunks();
    }

    py::list chunkList;
    for (const ChunkPos &pos : chunks) {
        py::tuple coord = py::make_tuple(pos.x, pos.z);
        chunkList.append(coord);
    }

    return chunkList;
}

void PythonAPI::interactBlock(double x, double y, double z, bool sneak, bool lookAtBlock, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    int blockX = static_cast<int>(std::floor(x));
    int blockY = static_cast<int>(std::floor(y));
    int blockZ = static_cast<int>(std::floor(z));

    BotManager::sendInteractWithBlock(botName, blockX, blockY, blockZ,
                                      mankool::mcbot::protocol::HandGadget::Hand::MAIN_HAND, sneak, lookAtBlock);
}

void PythonAPI::clickContainerSlot(int slotIndex, MouseButton button, ContainerClickType clickType, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    BotManager::sendClickContainerSlot(botName, slotIndex, static_cast<int>(button), static_cast<int>(clickType));
}

void PythonAPI::closeContainer(const std::string &bot)
{
    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    BotManager::sendCloseContainer(botName);
}

void PythonAPI::openInventory(const std::string &bot)
{
    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    BotManager::sendOpenInventory(botName);
}

py::object PythonAPI::getContainer(const std::string &bot)
{
    QString botName = resolveBotName(bot);

    BotInstance *botInstance = BotManager::getBotByName(botName);
    if (!botInstance || botInstance->status != BotStatus::Online) {
        return py::none();
    }

    QMutexLocker locker(botInstance->dataMutex.get());

    if (!botInstance->containerState.isOpen) {
        return py::none();
    }

    py::dict result;
    result["id"] = botInstance->containerState.containerId;
    result["type"] = botInstance->containerState.containerType;

    py::list items;
    for (const auto &item : botInstance->containerState.items) {
        py::dict itemDict;
        itemDict["slot"] = static_cast<int>(item.slot());
        itemDict["item_id"] = item.itemId().toStdString();
        itemDict["count"] = static_cast<int>(item.count());
        itemDict["display_name"] = item.displayName().toStdString();
        items.append(itemDict);
    }
    result["items"] = items;

    return result;
}

py::object PythonAPI::getItemInfo(const std::string &itemId, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = BotManager::getBotByName(botName);
    
    if (!botInstance || botInstance->status != BotStatus::Online) {
        return py::none();
    }
    
    if (botInstance->itemRegistry) {
        auto itemInfo = botInstance->itemRegistry->getItem(QString::fromStdString(itemId));
        if (itemInfo) {
            py::dict result;
            result["item_id"] = itemInfo->itemId.toStdString();
            result["max_stack_size"] = itemInfo->maxStackSize;
            result["max_damage"] = itemInfo->maxDamage;
            return result;
        }
    }
    
    return py::none();
}

py::object PythonAPI::getRecipe(const std::string &recipeId, const std::string &bot)
{
    QString botName = resolveBotName(bot);

    BotInstance *botInstance = BotManager::getBotByName(botName);
    if (!botInstance || botInstance->status != BotStatus::Online) {
        return py::none();
    }

    QString qRecipeId = QString::fromStdString(recipeId);

    const Recipe *recipe = botInstance->recipeRegistry.getRecipe(qRecipeId);
    if (!recipe) {
        return py::none();
    }

    py::dict result;
    result["recipe_id"] = recipe->recipeId.toStdString();
    result["type"] = recipe->type.toStdString();
    result["result_item"] = recipe->resultItem.toStdString();
    result["result_count"] = recipe->resultCount;
    result["is_shapeless"] = recipe->isShapeless;

    if (recipe->experience > 0) {
        result["experience"] = recipe->experience;
    }
    if (recipe->cookingTime > 0) {
        result["cooking_time"] = recipe->cookingTime;
    }

    py::list ingredients;
    for (const auto &ingredient : recipe->ingredients) {
        py::dict ingredientDict;
        ingredientDict["slot"] = ingredient.slot;
        ingredientDict["count"] = ingredient.count;

        py::list itemsList;
        for (const auto &item : ingredient.items) {
            itemsList.append(item.toStdString());
        }
        ingredientDict["items"] = itemsList;

        ingredients.append(ingredientDict);
    }
    result["ingredients"] = ingredients;

    return result;
}

py::list PythonAPI::getAllRecipes(const std::string &bot)
{
    QString botName = resolveBotName(bot);

    BotInstance *botInstance = BotManager::getBotByName(botName);
    if (!botInstance || botInstance->status != BotStatus::Online) {
        return py::list();
    }

    py::list result;
    QStringList recipeIds = botInstance->recipeRegistry.getAllRecipeIds();
    for (const QString &id : recipeIds) {
        result.append(id.toStdString());
    }

    return result;
}

