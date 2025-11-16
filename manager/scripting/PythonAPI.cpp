#include "PythonAPI.h"
#include "bot/BotManager.h"
#include "ui/BotConsoleWidget.h"
#include "prism/PrismLauncherManager.h"
#include <QDebug>
#include <QCoreApplication>
#include <QThread>
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

