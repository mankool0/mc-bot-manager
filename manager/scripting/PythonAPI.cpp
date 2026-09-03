#include "PythonAPI.h"
#include "bot/BotManager.h"
#include "ui/BotConsoleWidget.h"
#include "ui/AppColors.h"
#include "CustomColumnManager.h"
#include "ScriptContext.h"
#include "ScriptEngine.h"
#include "ScriptMessageBus.h"
#include "prism/PrismLauncherManager.h"
#include "crafting/CraftingPlanner.h"
#include "world/ItemRegistry.h"
#include "world/NBTSerializer.h"
#include "world/RegionFile.h"
#include "world/WorldExporter.h"
#include "world/SectionCodec.h"
#include <QDebug>
#include <QFile>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QThread>
#include <QReadWriteLock>
#include <QDateTime>
#include <pybind11/stl.h>
#include <io/stream_reader.h>
#include <sstream>
#include <cmath>
#include <limits>

thread_local QString PythonAPI::currentBot;
thread_local QString PythonAPI::currentScript;
thread_local bool PythonAPI::forceGlobalConsole = false;
thread_local std::atomic<bool> *PythonAPI::currentStopFlag = nullptr;
QPointer<BotConsoleWidget> PythonAPI::globalConsole;

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

BotInstance* PythonAPI::ensureBotCapability(const QString &botName, const char *capability)
{
    BotInstance *bot = ensureBotOnline(botName);
    if (!bot->hasCapability(QString::fromLatin1(capability))) {
        throw std::runtime_error(std::string("Bot does not support ") + capability);
    }
    return bot;
}

BotInstance* PythonAPI::botIfCapable(const QString &botName, const char *capability)
{
    BotInstance *bot = BotManager::getBotByName(botName);
    if (bot && bot->status == BotStatus::Online
        && !bot->hasCapability(QString::fromLatin1(capability))) {
        throw std::runtime_error(std::string("Bot does not support ") + capability);
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
                    for (const QString &str : std::as_const(it.value())) {
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

py::object PythonAPI::getWeather(const std::string &bot)
{
    QString name = resolveBotName(bot);

    BotInstance *botInstance = BotManager::getBotByName(name);
    if (!botInstance || botInstance->status != BotStatus::Online) {
        return py::none();
    }

    QMutexLocker locker(botInstance->dataMutex.get());
    py::dict result;
    result["is_raining"] = botInstance->isRaining;
    result["is_thundering"] = botInstance->isThundering;
    result["rain_level"] = botInstance->rainLevel;
    result["thunder_level"] = botInstance->thunderLevel;
    return result;
}

py::object PythonAPI::getServerInfo(const std::string &botName)
{
    QString name = resolveBotName(botName);
    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }
    QMutexLocker locker(bot->dataMutex.get());
    PyServerInfo info;
    info.address = bot->server.toStdString();
    info.motd = bot->serverMotd.toStdString();
    info.ping = bot->serverPing;
    info.version = bot->serverVersionName.toStdString();
    info.players_online = bot->serverPlayersOnline;
    info.players_max = bot->serverPlayersMax;
    locker.unlock();
    return py::cast(info);
}

py::list PythonAPI::getPlayerList(const std::string &botName)
{
    QString name = resolveBotName(botName);
    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::list();
    }
    QMap<QString, TabListPlayerData> snapshot;
    {
        QMutexLocker locker(bot->dataMutex.get());
        snapshot = bot->tabList;
    }
    py::list result;
    for (const auto &p : std::as_const(snapshot)) {
        PyTabListPlayer e;
        e.name = p.name.toStdString();
        e.uuid = p.uuid.toStdString();
        e.ping = p.ping;
        e.gamemode = static_cast<Gamemode>(p.gamemode);
        e.display_name = p.displayName.toStdString();
        result.append(py::cast(e));
    }
    return result;
}

py::object PythonAPI::getServerStats(const std::string &botName)
{
    QString name = resolveBotName(botName);
    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    // Blocks while the client asks the server and forwards the reply; release the GIL
    // so other Python threads keep running during the round trip.
    std::optional<QMap<QString, QMap<QString, qint64>>> stats;
    {
        py::gil_scoped_release release;
        stats = BotManager::getStatistics(name);
    }
    if (!stats.has_value()) {
        return py::none();
    }

    py::dict result;
    for (auto catIt = stats->constBegin(); catIt != stats->constEnd(); ++catIt) {
        py::dict inner;
        const QMap<QString, qint64> &values = catIt.value();
        for (auto valIt = values.constBegin(); valIt != values.constEnd(); ++valIt) {
            inner[py::str(valIt.key().toStdString())] = static_cast<long long>(valIt.value());
        }
        result[py::str(catIt.key().toStdString())] = inner;
    }
    return result;
}

static PyWindowState windowStateToPy(const mankool::mcbot::protocol::WindowStateResponse &state)
{
    PyWindowState result;
    result.platform = state.platform().toStdString();
    result.can_move = state.canMove();
    result.monitor = state.monitor().toStdString();
    result.x = state.x();
    result.y = state.y();
    result.width = state.width();
    result.height = state.height();
    result.minimized = state.minimized();
    result.focused = state.focused();
    result.visible = state.visible();
    const auto monitors = state.monitors();
    for (const auto &m : monitors) {
        PyMonitor pm;
        pm.name = m.name().toStdString();
        pm.primary = m.primary();
        pm.x = m.x();
        pm.y = m.y();
        pm.width = m.width();
        pm.height = m.height();
        pm.work_x = m.workX();
        pm.work_y = m.workY();
        pm.work_width = m.workWidth();
        pm.work_height = m.workHeight();
        result.monitors.push_back(pm);
    }
    return result;
}

py::object PythonAPI::getWindow(const std::string &botName)
{
    QString name = resolveBotName(botName);
    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    std::optional<mankool::mcbot::protocol::WindowStateResponse> state;
    {
        py::gil_scoped_release release;
        state = BotManager::getWindowState(name);
    }
    if (!state.has_value()) {
        return py::none();
    }
    return py::cast(windowStateToPy(*state));
}

py::object PythonAPI::setWindow(const py::object &x, const py::object &y, const py::object &width, const py::object &height,
                                const std::string &monitor, const py::object &minimized, const py::object &visible,
                                const std::string &botName)
{
    QString name = resolveBotName(botName);
    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        throw std::runtime_error("Bot '" + name.toStdString() + "' is not online");
    }

    mankool::mcbot::protocol::SetWindowCommand cmd;
    cmd.setMonitor(QString::fromStdString(monitor));
    if (!x.is_none()) cmd.setX(x.cast<int>());
    if (!y.is_none()) cmd.setY(y.cast<int>());
    if (!width.is_none()) cmd.setWidth(width.cast<int>());
    if (!height.is_none()) cmd.setHeight(height.cast<int>());
    if (!minimized.is_none()) cmd.setMinimized(minimized.cast<bool>());
    if (!visible.is_none()) cmd.setVisible(visible.cast<bool>());

    std::optional<mankool::mcbot::protocol::WindowStateResponse> state;
    {
        py::gil_scoped_release release;
        state = BotManager::setWindow(name, cmd);
    }
    if (!state.has_value()) {
        return py::none();
    }
    return py::cast(windowStateToPy(*state));
}

std::optional<float> PythonAPI::getHealth(const std::string &botName)
{
    QString name = resolveBotName(botName);

    py::gil_scoped_release release;

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online)
        return std::nullopt;

    QMutexLocker locker(bot->dataMutex.get());
    return bot->health;
}

std::optional<int> PythonAPI::getHunger(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online)
        return std::nullopt;

    return bot->foodLevel;
}

std::optional<float> PythonAPI::getSaturation(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online)
        return std::nullopt;

    return bot->saturation;
}

std::optional<int> PythonAPI::getAir(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online)
        return std::nullopt;

    return bot->air;
}

std::optional<int> PythonAPI::getExperienceLevel(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online)
        return std::nullopt;

    return bot->experienceLevel;
}

std::optional<float> PythonAPI::getExperienceProgress(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online)
        return std::nullopt;

    return bot->experienceProgress;
}

std::optional<int> PythonAPI::getSelectedSlot(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online)
        return std::nullopt;

    return bot->selectedSlot;
}

void PythonAPI::selectSlot(int slot, const std::string &botName)
{
    if (slot < 0 || slot > 8) {
        throw std::runtime_error("Slot must be 0-8");
    }
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendSwitchHotbarSlot(name, slot);
}

py::object PythonAPI::getRotation(const std::string &botName)
{
    QString name = resolveBotName(botName);

    py::gil_scoped_release release;

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        py::gil_scoped_acquire acquire;
        return py::none();
    }

    QMutexLocker locker(bot->dataMutex.get());
    float yaw = bot->yaw;
    float pitch = bot->pitch;
    locker.unlock();

    py::gil_scoped_acquire acquire;
    py::dict result;
    result["yaw"] = yaw;
    result["pitch"] = pitch;
    return result;
}

void PythonAPI::rotate(float yaw, float pitch, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendSetRotation(name, yaw, pitch);
}

void PythonAPI::useItem(Hand hand, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendUseItem(name, hand == Hand::OFF
                                      ? mankool::mcbot::protocol::HandGadget::Hand::OFF_HAND
                                      : mankool::mcbot::protocol::HandGadget::Hand::MAIN_HAND);
}

void PythonAPI::dropItem(bool dropAll, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendDropItem(name, dropAll);
}

std::optional<std::string> PythonAPI::getServer(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->server.isEmpty())
        return std::nullopt;

    return bot->server.toStdString();
}

std::optional<std::string> PythonAPI::getSingleplayerWorld(const std::string &botName)
{
    QString name = resolveBotName(botName);
    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || !bot->isSingleplayer || bot->singleplayerWorld.isEmpty())
        return std::nullopt;
    return bot->singleplayerWorld.toStdString();
}

bool PythonAPI::getIsSingleplayer(const std::string &botName)
{
    QString name = resolveBotName(botName);
    BotInstance *bot = BotManager::getBotByName(name);
    return bot && bot->isSingleplayer;
}

std::optional<std::string> PythonAPI::getAccount(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->account.isEmpty())
        return std::nullopt;

    return bot->account.toStdString();
}

std::optional<int> PythonAPI::getDataVersion(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    // 0 is the unset sentinel: the mod reports this in its handshake
    if (!bot || bot->dataVersion <= 0)
        return std::nullopt;

    return bot->dataVersion;
}

py::object PythonAPI::getProxy(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->proxySettings.host.isEmpty()) {
        return py::none();
    }

    QString healthStr;
    switch (bot->proxyHealth) {
        case BotInstance::ProxyHealth::Alive:   healthStr = "Alive"; break;
        case BotInstance::ProxyHealth::Dead:    healthStr = "Dead"; break;
        default:                                healthStr = "Unknown"; break;
    }

    py::dict d;
    d["enabled"]  = bot->proxySettings.enabled;
    d["host"]     = bot->proxySettings.host.toStdString();
    d["port"]     = bot->proxySettings.port;
    d["type"]     = bot->proxySettings.type.toStdString();
    d["username"] = bot->proxySettings.username.toStdString();
    d["password"] = bot->proxySettings.password.toStdString();
    d["health"]   = healthStr.toStdString();
    return d;
}

std::optional<int64_t> PythonAPI::getUptime(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || !bot->startTime.isValid())
        return std::nullopt;

    return bot->startTime.secsTo(QDateTime::currentDateTime());
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

static py::dict buildItemDict(const mankool::mcbot::protocol::ItemStack &item)
{
    py::dict itemDict;
    itemDict["slot"] = static_cast<int>(item.slot());
    itemDict["item_id"] = item.itemId().toStdString();
    itemDict["count"] = static_cast<int>(item.count());
    if (item.damage() > 0)    itemDict["damage"]      = static_cast<int>(item.damage());
    if (item.maxDamage() > 0) itemDict["max_damage"]  = static_cast<int>(item.maxDamage());
    if (!item.displayName().isEmpty()) itemDict["display_name"] = item.displayName().toStdString();
    if (item.repairCost() > 0) itemDict["repair_cost"] = static_cast<int>(item.repairCost());
    py::dict enchantDict;
    for (const auto &[name, level] : item.enchantments().asKeyValueRange())
        enchantDict[name.toStdString().c_str()] = static_cast<int>(level);
    if (!enchantDict.empty()) itemDict["enchantments"] = enchantDict;
    const auto &containerItems = item.containerItems();
    if (!containerItems.isEmpty()) {
        py::list containerList;
        for (const auto &ci : containerItems) {
            containerList.append(buildItemDict(ci));
        }
        itemDict["container_items"] = containerList;
    }
    return itemDict;
}

py::object PythonAPI::getInventory(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    QVector<mankool::mcbot::protocol::ItemStack> inventoryCopy;
    {
        QMutexLocker locker(bot->dataMutex.get());
        inventoryCopy = bot->inventory;
    }

    py::list result;
    for (const auto &item : std::as_const(inventoryCopy)) {
        if (!item.itemId().isEmpty()) {
            result.append(buildItemDict(item));
        }
    }

    return result;
}

py::object PythonAPI::getCursorItem(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    mankool::mcbot::protocol::ItemStack cursorCopy;
    {
        QMutexLocker locker(bot->dataMutex.get());
        cursorCopy = bot->cursorItem;
    }

    py::dict itemDict = buildItemDict(cursorCopy);
    if (cursorCopy.itemId().isEmpty()) {
        itemDict["item_id"] = std::string("minecraft:air");
    }
    itemDict["slot"] = -1;
    return itemDict;
}

py::object PythonAPI::getScreen(const std::string &botName)
{
    QString name = resolveBotName(botName);

    BotInstance *bot = BotManager::getBotByName(name);
    if (!bot || bot->status != BotStatus::Online) {
        return py::none();
    }

    QMutexLocker locker(bot->dataMutex.get());

    if (bot->screenState.screenClass.isEmpty()) {
        return py::none();
    }

    PyScreenState result;
    result.screenId = bot->screenState.screenId.toStdString();
    result.screenClass = bot->screenState.screenClass.toStdString();
    result.title = bot->screenState.title.toStdString();
    result.width = bot->screenState.width;
    result.height = bot->screenState.height;

    for (const auto &w : std::as_const(bot->screenState.widgets)) {
        PyGuiWidget pw;
        pw.index = w.index;
        pw.widgetType = w.widgetType.toStdString();
        pw.className = w.className.toStdString();
        pw.x = w.x;
        pw.y = w.y;
        pw.width = w.width;
        pw.height = w.height;
        pw.active = w.active;
        pw.visible = w.visible;
        pw.text = w.text.toStdString();
        pw.editValue = w.editValue.toStdString();
        pw.editEditable = w.editEditable;
        pw.selected = w.selected;
        result.widgets.push_back(pw);
    }

    for (const auto &s : std::as_const(bot->screenState.guiSlots)) {
        PyGuiSlot ps;
        ps.index = s.index;
        ps.x = s.x;
        ps.y = s.y;
        ps.active = s.active;
        ps.itemId = s.item.itemId().toStdString();
        ps.count = s.item.count();
        ps.displayName = s.item.displayName().toStdString();
        ps.damage = s.item.damage();
        ps.maxDamage = s.item.maxDamage();
        ps.repairCost = s.item.repairCost();
        for (const auto &[name, level] : s.item.enchantments().asKeyValueRange())
            ps.enchantments[name.toStdString()] = static_cast<int>(level);
        result.guiSlots.push_back(ps);
    }

    return py::cast(std::move(result));
}

void PythonAPI::typeText(const std::string &screenId, const std::string &text, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendTypeText(name, QString::fromStdString(screenId), QString::fromStdString(text));
}

void PythonAPI::pressKey(const std::string &screenId, int keyCode, int modifiers, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendPressKey(name, QString::fromStdString(screenId), keyCode, modifiers);
}

void PythonAPI::clickScreenPosition(const std::string &screenId, double x, double y, MouseButton button, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendClickScreenPosition(name, QString::fromStdString(screenId), x, y, static_cast<int>(button));
}

void PythonAPI::openGameMenu(const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendOpenGameMenu(name);
}

void PythonAPI::clickScreenWidget(const std::string &screenId, int widgetIndex, MouseButton button, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    BotManager::sendClickScreenWidget(botName, QString::fromStdString(screenId), widgetIndex, static_cast<int>(button));
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

    const QVector<BotInstance*> &bots = BotManager::getBots();
    for (const auto *bot : std::as_const(bots)) {
        py::dict botDict;
        botDict["name"] = bot->name.toStdString();

        QString statusStr;
        switch (bot->status) {
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

    BotManager::sendChat(name, QString::fromStdString(message), true);
}

void PythonAPI::sendCommand(const std::string &command, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);

    BotManager::sendCommand(name, QString::fromStdString(command), true);
}

void PythonAPI::connectServer(const std::string &address, const std::string &botName)
{
    if (address.empty()) {
        throw std::runtime_error("Server address must not be empty");
    }
    QString name = resolveBotName(botName);
    ensureBotOnline(name);

    if (!BotManager::sendConnectToServer(name, QString::fromStdString(address))) {
        throw std::runtime_error("Cannot connect: proxy is unreachable");
    }
}

void PythonAPI::disconnectServer(const std::string &reason, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);

    BotManager::sendDisconnect(name, QString::fromStdString(reason));
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

    // Queued behind any launch still in flight; the startup timeout is armed there
    PrismLauncherManager::launchBot(bot);
}

bool PythonAPI::waitForOnline(double timeout, const std::string &botName)
{
    QString name = resolveBotName(botName);

    py::gil_scoped_release release;

    QDeadlineTimer deadline(static_cast<qint64>(timeout * 1000.0));
    for (;;) {
        BotInstance *bot = BotManager::getBotByName(name);
        if (!bot) {
            throw std::runtime_error("Bot not found");
        }
        BotStatus status = bot->status;
        if (status == BotStatus::Online) {
            return true;
        }
        if (status == BotStatus::Error) {
            return false;
        }
        if (currentStopFlag && currentStopFlag->load()) {
            return false;
        }
        if (deadline.hasExpired()) {
            return false;
        }
        QThread::msleep(100);
    }
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
    ensureBotCapability(name, "baritone");

    BotManager::sendBaritoneCommand(name, QString("goto %1 %2 %3").arg(x).arg(y).arg(z));
}

void PythonAPI::baritoneGoto(double x, double z, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotCapability(name, "baritone");

    BotManager::sendBaritoneCommand(name, QString("goto %1 %2").arg(x).arg(z));
}

void PythonAPI::baritoneFollow(const std::string &player, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotCapability(name, "baritone");

    BotManager::sendBaritoneCommand(name, QString("follow player %1").arg(QString::fromStdString(player)));
}

void PythonAPI::baritoneCancel(const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotCapability(name, "baritone");

    BotManager::sendBaritoneCommand(name, "cancel");
}

void PythonAPI::baritoneMine(const std::string &blockType, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotCapability(name, "baritone");

    BotManager::sendBaritoneCommand(name, QString("mine %1").arg(QString::fromStdString(blockType)));
}

void PythonAPI::baritoneFarm(const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotCapability(name, "baritone");

    BotManager::sendBaritoneCommand(name, "farm");
}

void PythonAPI::baritoneCommand(const std::string &command, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotCapability(name, "baritone");

    BotManager::sendBaritoneCommand(name, QString::fromStdString(command));
}

void PythonAPI::baritoneSetSetting(const std::string &setting, const py::object &value, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotCapability(name, "baritone");

    QVariant qValue = pyObjectToQVariant(value);
    BotManager::sendBaritoneSettingChange(name, QString::fromStdString(setting), qValue);
}

py::object PythonAPI::baritoneGetSetting(const std::string &setting, const std::string &bot)
{
    QString name = resolveBotName(bot);

    BotInstance *botInst = botIfCapable(name, "baritone");
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

    BotInstance *botInst = botIfCapable(name, "baritone");
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
    BotInstance *botInst = ensureBotCapability(name, "meteor");

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
    ensureBotCapability(name, "meteor");

    BotManager::sendCommand(name, QString("meteor set %1 enabled true").arg(QString::fromStdString(module)), true);
}

void PythonAPI::meteorDisable(const std::string &module, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotCapability(name, "meteor");

    BotManager::sendCommand(name, QString("meteor set %1 enabled false").arg(QString::fromStdString(module)), true);
}

void PythonAPI::meteorSetSetting(const std::string &module, const std::string &setting,
                                 const py::object &value, const std::string &bot)
{
    QString name = resolveBotName(bot);
    ensureBotCapability(name, "meteor");

    QVariant qValue = pyObjectToQVariant(value);
    BotManager::sendMeteorSettingChange(name, QString::fromStdString(module), QString::fromStdString(setting), qValue);
}

py::object PythonAPI::meteorGetSetting(const std::string &module, const std::string &setting,
                                       const std::string &bot)
{
    QString name = resolveBotName(bot);

    BotInstance *botInst = botIfCapable(name, "meteor");
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

    BotInstance *botInst = botIfCapable(name, "meteor");
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

    // Check the capability before dropping the GIL, so the exception propagates from a normal
    // GIL-held frame rather than unwinding through the release guard.
    botIfCapable(name, "meteor");

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

void PythonAPI::setCurrentStopFlag(std::atomic<bool> *flag)
{
    currentStopFlag = flag;
}

QString PythonAPI::currentScope()
{
    return currentBot.isEmpty() ? QStringLiteral("_global") : currentBot;
}

QVariant PythonAPI::pyObjectToPlainVariant(const py::object &value)
{
    if (value.is_none()) {
        return QVariant();
    } else if (py::isinstance<py::bool_>(value)) {
        return value.cast<bool>();
    } else if (py::isinstance<py::int_>(value)) {
        return QVariant(static_cast<qlonglong>(value.cast<int64_t>()));
    } else if (py::isinstance<py::float_>(value)) {
        return value.cast<double>();
    } else if (py::isinstance<py::str>(value)) {
        return QString::fromStdString(value.cast<std::string>());
    } else if (py::isinstance<py::list>(value) || py::isinstance<py::tuple>(value)) {
        QVariantList list;
        for (const auto &item : value.cast<py::sequence>()) {
            list.append(pyObjectToPlainVariant(py::cast<py::object>(item)));
        }
        return list;
    } else if (py::isinstance<py::dict>(value)) {
        QVariantMap map;
        for (const auto &item : value.cast<py::dict>()) {
            if (!py::isinstance<py::str>(item.first)) {
                throw py::type_error("comms payload dict keys must be strings");
            }
            map[QString::fromStdString(item.first.cast<std::string>())] =
                pyObjectToPlainVariant(py::cast<py::object>(item.second));
        }
        return map;
    }
    throw py::type_error("comms payloads must be plain data: None/bool/int/float/str/list/dict");
}

int PythonAPI::commsEmit(const std::string &topic, const py::object &data)
{
    if (topic.empty()) {
        throw py::value_error("emit() requires a non-empty topic");
    }

    QVariant payload = pyObjectToPlainVariant(data);
    QString senderScope = currentScope();
    QString senderScript = currentScript;
    QString qTopic = QString::fromStdString(topic);

    py::gil_scoped_release release;
    return ScriptMessageBus::instance().publish(qTopic, payload, senderScope, senderScript);
}

int PythonAPI::commsSend(const std::string &botName, const py::object &data, const std::string &topic)
{
    if (botName.empty()) {
        throw py::value_error("send() requires a bot name (or \"_global\")");
    }

    QVariant payload = pyObjectToPlainVariant(data);
    QString senderScope = currentScope();
    QString senderScript = currentScript;
    QString target = QString::fromStdString(botName);
    QString qTopic = QString::fromStdString(topic);

    py::gil_scoped_release release;
    return ScriptMessageBus::instance().sendTo(target, qTopic, payload, senderScope, senderScript);
}

void PythonAPI::commsSubscribe(const std::string &topic)
{
    QString scope = currentScope();
    QString script = currentScript;
    QString qTopic = QString::fromStdString(topic);

    py::gil_scoped_release release;
    if (!ScriptMessageBus::instance().subscribe(scope, script, qTopic)) {
        throw std::runtime_error("this script is not registered with the message bus");
    }
}

void PythonAPI::commsUnsubscribe(const std::string &topic)
{
    QString scope = currentScope();
    QString script = currentScript;
    QString qTopic = QString::fromStdString(topic);

    py::gil_scoped_release release;
    ScriptMessageBus::instance().unsubscribe(scope, script, qTopic);
}

py::object PythonAPI::commsReceive(double timeout)
{
    QString scope = currentScope();
    QString script = currentScript;

    QVariantMap message;
    bool got = false;
    {
        py::gil_scoped_release release;

        ScriptContext *ctx = ScriptMessageBus::instance().openMailbox(scope, script);
        if (ctx) {
            QDeadlineTimer deadline = timeout < 0
                ? QDeadlineTimer(QDeadlineTimer::Forever)
                : QDeadlineTimer(static_cast<qint64>(timeout * 1000.0));

            QMutexLocker locker(&ctx->inboxMutex);
            for (;;) {
                if (!ctx->inbox.isEmpty()) {
                    message = ctx->inbox.dequeue();
                    ctx->droppedMessages = 0;
                    got = true;
                    break;
                }
                if (currentStopFlag && currentStopFlag->load())
                    break;
                if (!ctx->running)
                    break;
                if (deadline.hasExpired())
                    break;
                // Wake at least every 100ms so a stop request is noticed even
                // if the wakeAll from stopScript races the wait.
                qint64 remaining = deadline.remainingTime();
                unsigned long slice = remaining < 0 ? 100
                    : static_cast<unsigned long>(qMin<qint64>(100, remaining));
                ctx->inboxCond.wait(&ctx->inboxMutex, slice);
            }
        }
    }

    if (!got) {
        return py::none();
    }
    return qVariantToPyObject(message);
}

int PythonAPI::commsPending()
{
    QString scope = currentScope();
    QString script = currentScript;

    py::gil_scoped_release release;
    return ScriptMessageBus::instance().pendingCount(scope, script);
}

// Run one operation against a scope's ScriptEngine on the main thread and wait
// (bounded) for the result. Engines are main-thread objects; script threads
// must not touch them directly. Deliberately a queued call plus semaphore, not
// BlockingQueuedConnection: if the main thread is busy joining script threads
// at shutdown this times out instead of deadlocking, and the shared_ptr keeps
// the result slot alive if the lambda runs after the timeout.
static QVariant runEngineOp(const QString &scope, const char *what,
                            const std::function<QVariant(ScriptEngine*, QString*)> &op)
{
    auto pending = std::make_shared<PendingScriptOp>();
    ScriptMessageBus *bus = &ScriptMessageBus::instance();

    QMetaObject::invokeMethod(bus, [bus, pending, scope, op]() {
        ScriptEngine *engine = bus->engineForScope(scope);
        if (!engine) {
            pending->error = QString("no script engine for '%1' (unknown bot?)").arg(scope);
        } else {
            pending->result = op(engine, &pending->error);
        }
        pending->ok = pending->error.isEmpty();
        pending->sem.release();
    }, Qt::QueuedConnection);

    {
        py::gil_scoped_release release;
        if (!pending->sem.tryAcquire(1, 5000)) {
            throw std::runtime_error(std::string(what)
                + ": manager did not respond (busy or shutting down)");
        }
    }
    if (!pending->ok) {
        throw std::runtime_error(pending->error.toStdString());
    }
    return pending->result;
}

bool PythonAPI::runScriptApi(const std::string &script, const std::string &botName)
{
    QString scope = botName.empty() ? currentScope() : QString::fromStdString(botName);
    QString filename = QString::fromStdString(script);

    QVariant result = runEngineOp(scope, "run_script",
        [filename](ScriptEngine *engine, QString *error) -> QVariant {
            if (!engine->getScriptNames().contains(filename)) {
                *error = QString("unknown script '%1' in '%2'")
                             .arg(filename, engine->getScopeName());
                return {};
            }
            // False here means the script is already running.
            return engine->runScript(filename);
        });
    return result.toBool();
}

void PythonAPI::stopScriptApi(const std::string &script, const std::string &botName)
{
    QString scope = botName.empty() ? currentScope() : QString::fromStdString(botName);
    QString filename = QString::fromStdString(script);

    runEngineOp(scope, "stop_script",
        [filename](ScriptEngine *engine, QString *error) -> QVariant {
            if (!engine->getScriptNames().contains(filename)) {
                *error = QString("unknown script '%1' in '%2'")
                             .arg(filename, engine->getScopeName());
                return {};
            }
            engine->stopScript(filename);
            return {};
        });
}

py::list PythonAPI::listScriptsApi(const std::string &botName)
{
    QString scope = botName.empty() ? currentScope() : QString::fromStdString(botName);

    QVariant result = runEngineOp(scope, "list_scripts",
        [](ScriptEngine *engine, QString *) -> QVariant {
            QVariantList list;
            const QStringList names = engine->getScriptNames();
            for (const QString &name : names) {
                QVariantMap entry;
                entry[QStringLiteral("name")] = name;
                entry[QStringLiteral("running")] = engine->isScriptRunning(name);
                entry[QStringLiteral("enabled")] = engine->isScriptEnabled(name);
                list.append(entry);
            }
            return list;
        });
    return qVariantToPyObject(result).cast<py::list>();
}

void PythonAPI::log(const std::string &message)
{
    QString qMessage = QString::fromStdString(message);
    QString botName = currentBot;
    QString scriptName = currentScript.isEmpty() ? "Script" : currentScript;
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString formattedMsg = QString("[%1] [%2] %3").arg(ts, scriptName, qMessage);

    if (!forceGlobalConsole && !botName.isEmpty()) {
        BotInstance *bot = BotManager::getBotByName(botName);
        if (bot && bot->consoleWidget) {
            bot->consoleWidget->pushLogLine(formattedMsg, AppColors::scriptLog());
        }
    } else if (globalConsole) {
        // Global scripts (and the column compute worker) route to the global console.
        globalConsole->pushLogLine(formattedMsg, AppColors::scriptLog());
    }
}

void PythonAPI::error(const std::string &message)
{
    QString qMessage = QString::fromStdString(message);
    QString botName = currentBot;
    QString scriptName = currentScript.isEmpty() ? "Script" : currentScript;
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString formattedMsg = QString("[%1] [%2 Error] %3").arg(ts, scriptName, qMessage);

    if (!forceGlobalConsole && !botName.isEmpty()) {
        BotInstance *bot = BotManager::getBotByName(botName);
        if (bot && bot->consoleWidget) {
            bot->consoleWidget->pushLogLine(formattedMsg, AppColors::scriptError());
        }
    } else if (globalConsole) {
        globalConsole->pushLogLine(formattedMsg, AppColors::scriptError());
    }
}

void PythonAPI::setGlobalConsole(BotConsoleWidget *console)
{
    globalConsole = console;
}

BotConsoleWidget* PythonAPI::getGlobalConsole()
{
    return globalConsole.data();
}

void PythonAPI::setForceGlobalConsole(bool enabled)
{
    forceGlobalConsole = enabled;
}

bool PythonAPI::isForceGlobalConsole()
{
    return forceGlobalConsole;
}

// Columns are cleaned up per script file in the global engine only, so a
// bot-bound script must not register them. The compute worker also sets a
// current bot but is allowed via its forced-global-console flag.
static void ensureGlobalScriptContext(const char *what)
{
    if (!PythonAPI::getCurrentBot().isEmpty() && !PythonAPI::isForceGlobalConsole())
        throw std::runtime_error(std::string(what)
            + " is only available from global scripts (Global Scripts tab)");
}

// Seconds -> clamped milliseconds. Rejects NaN/inf/non-positive rather than
// silently mangling them.
static int columnIntervalToMs(double intervalSeconds)
{
    if (!std::isfinite(intervalSeconds) || intervalSeconds <= 0)
        throw py::value_error("interval must be a positive number of seconds");
    double ms = intervalSeconds * 1000.0;
    if (ms >= std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    return qMax(static_cast<int>(ms), CustomColumnManager::kMinIntervalMs);
}

void PythonAPI::addColumn(const std::string &name, const py::function &provider, double interval)
{
    ensureGlobalScriptContext("manager.add_column");
    CustomColumnManager::instance().registerColumn(
        QString::fromStdString(name), provider, currentScript, columnIntervalToMs(interval));
}

void PythonAPI::removeColumn(const std::string &name)
{
    ensureGlobalScriptContext("manager.remove_column");
    CustomColumnManager::instance().unregisterColumn(QString::fromStdString(name));
}

py::object PythonAPI::column(const std::string &name, double interval)
{
    ensureGlobalScriptContext("manager.column");
    std::string colName = name;
    int intervalMs = columnIntervalToMs(interval);
    // Decorator: registers the wrapped function and returns it unchanged.
    return py::cpp_function([colName, intervalMs](py::function fn) {
        ensureGlobalScriptContext("manager.column");
        CustomColumnManager::instance().registerColumn(
            QString::fromStdString(colName), fn, currentScript, intervalMs);
        return fn;
    });
}

// ============================================================================
// World Data API
// ============================================================================

// ---------------------------------------------------------------------------
// Private disk-read helpers
// ---------------------------------------------------------------------------

// Reads a chunk from the saved world. The region directory depends on the save's data version
// (pre-26.1: region/, DIM-1/region, DIM1/region; 26.1+: dimensions/minecraft/<dim>/region), so
// the same path logic as the writer (WorldExporter::getDimensionPath) is used here.
static nbt::tag_compound readChunkNBT(const WorldAutoSaver& saver, int chunkX, int chunkZ,
                                      const QString& dimension)
{
    QString dim = dimension.isEmpty() ? QStringLiteral("minecraft:overworld") : dimension;
    if (dim != "minecraft:overworld" && dim != "minecraft:the_nether" && dim != "minecraft:the_end") {
        return {};
    }

    QString regionDir = WorldExporter::getDimensionPath(saver.getWorldPath(), dim, saver.getDataVersion()) + "/region";
    int regionX = chunkX >> 5;
    int regionZ = chunkZ >> 5;
    QString regionPath = QString("%1/r.%2.%3.mca").arg(regionDir).arg(regionX).arg(regionZ);

    // RegionFile creates a missing file on open; a read must not leave empty regions behind.
    if (!QFile::exists(regionPath)) return {};

    RegionFile regionFile(regionPath);
    if (!regionFile.isValid()) return {};

    return regionFile.readChunk(chunkX & 31, chunkZ & 31);
}

// ---------------------------------------------------------------------------
// Helpers for building Python block entity dicts
// ---------------------------------------------------------------------------

// Forward declaration
static py::object nbtValueToPy(const nbt::value& val);

static py::dict nbtCompoundToPy(const nbt::tag_compound& compound) {
    py::dict d;
    for (auto it = compound.begin(); it != compound.end(); ++it) {
        d[it->first.c_str()] = nbtValueToPy(it->second);
    }
    return d;
}

static py::object nbtValueToPy(const nbt::value& val) {
    switch (val.get_type()) {
    case nbt::tag_type::Byte:   return py::int_(static_cast<const nbt::tag_byte&>(val.get()).get());
    case nbt::tag_type::Short:  return py::int_(static_cast<const nbt::tag_short&>(val.get()).get());
    case nbt::tag_type::Int:    return py::int_(static_cast<const nbt::tag_int&>(val.get()).get());
    case nbt::tag_type::Long:   return py::int_(static_cast<const nbt::tag_long&>(val.get()).get());
    case nbt::tag_type::Float:  return py::float_(static_cast<const nbt::tag_float&>(val.get()).get());
    case nbt::tag_type::Double: return py::float_(static_cast<const nbt::tag_double&>(val.get()).get());
    case nbt::tag_type::String: return py::str(static_cast<const nbt::tag_string&>(val.get()).get());
    case nbt::tag_type::Compound:
        return nbtCompoundToPy(static_cast<const nbt::tag_compound&>(val.get()));
    case nbt::tag_type::List: {
        py::list lst;
        for (const auto& entry : static_cast<const nbt::tag_list&>(val.get()))
            lst.append(nbtValueToPy(entry));
        return lst;
    }
    case nbt::tag_type::Byte_Array: {
        py::list lst;
        for (int8_t b : static_cast<const nbt::tag_byte_array&>(val.get())) lst.append(py::int_(b));
        return lst;
    }
    case nbt::tag_type::Int_Array: {
        py::list lst;
        for (int32_t v : static_cast<const nbt::tag_int_array&>(val.get())) lst.append(py::int_(v));
        return lst;
    }
    case nbt::tag_type::Long_Array: {
        py::list lst;
        for (int64_t v : static_cast<const nbt::tag_long_array&>(val.get())) lst.append(py::int_(v));
        return lst;
    }
    default: return py::none();
    }
}

// explicitSlot >= 0 overrides reading Slot from the compound (used for minecraft:container entries)
static py::dict diskItemToDict(const nbt::tag_compound& itemTag,
                               const std::shared_ptr<ItemRegistry>& registry,
                               int explicitSlot = -1)
{
    py::dict d;

    int slot = explicitSlot;
    if (slot < 0 && itemTag.has_key("Slot"))
        slot = static_cast<int>(static_cast<const nbt::tag_byte&>(itemTag.at("Slot").get()).get());
    d["slot"] = slot >= 0 ? slot : 0;

    std::string itemId = "minecraft:air";
    if (itemTag.has_key("id"))
        itemId = static_cast<const nbt::tag_string&>(itemTag.at("id").get()).get();
    d["item_id"] = itemId;

    int count = 1;
    if (itemTag.has_key("count"))
        count = static_cast<const nbt::tag_int&>(itemTag.at("count").get()).get();
    else if (itemTag.has_key("Count"))
        count = static_cast<int>(static_cast<const nbt::tag_byte&>(itemTag.at("Count").get()).get());
    d["count"] = count;

    bool hasMaxDamage = false;

    if (itemTag.has_key("components")) {
        try {
            const auto& components = static_cast<const nbt::tag_compound&>(itemTag.at("components").get());

            // Merge all components into the top-level dict, stripping "minecraft:" prefix
            static const std::string mcPrefix = "minecraft:";
            for (auto it = components.begin(); it != components.end(); ++it) {
                const std::string& key = it->first;
                const char* dictKey = key.size() > mcPrefix.size() && key.compare(0, mcPrefix.size(), mcPrefix) == 0
                    ? key.c_str() + mcPrefix.size()
                    : key.c_str();
                d[dictKey] = nbtValueToPy(it->second);
                if (std::strcmp(dictKey, "max_damage") == 0)
                    hasMaxDamage = true;
            }
        } catch (...) {}
    }

    // max_damage needs registry fallback (not stored in NBT when it equals the item default)
    if (!hasMaxDamage && registry) {
        auto info = registry->getItem(QString::fromStdString(itemId));
        if (info.has_value() && info->maxDamage > 0)
            d["max_damage"] = info->maxDamage;
    }

    // Map custom_name -> display_name if no display_name set, fall back to registry
    if (!d.contains("display_name")) {
        if (d.contains("custom_name"))
            d["display_name"] = d["custom_name"];
        else if (registry) {
            auto info = registry->getItem(QString::fromStdString(itemId));
            if (info.has_value() && !info->displayName.isEmpty())
                d["display_name"] = info->displayName.toStdString();
        }
    }

    // Normalize minecraft:container component -> container_items list of proper item dicts
    if (d.contains("container") && itemTag.has_key("components")) {
        try {
            const auto& components = static_cast<const nbt::tag_compound&>(itemTag.at("components").get());
            if (components.has_key("minecraft:container")) {
                py::list containerItems;
                const auto& containerList = static_cast<const nbt::tag_list&>(components.at("minecraft:container").get());
                for (const nbt::value& entry : containerList) {
                    const auto& entryComp = static_cast<const nbt::tag_compound&>(entry.get());
                    if (!entryComp.has_key("item") || !entryComp.has_key("slot")) continue;
                    int containerSlot = static_cast<const nbt::tag_int&>(entryComp.at("slot").get()).get();
                    const auto& itemComp = static_cast<const nbt::tag_compound&>(entryComp.at("item").get());
                    containerItems.append(diskItemToDict(itemComp, registry, containerSlot));
                }
                d["container_items"] = containerItems;
                PyDict_DelItemString(d.ptr(), "container");
            }
        } catch (...) {}
    }

    return d;
}

// Sign lines live only in the block entity NBT, and two encodings are in the wild: 1.21.4 writes
// each line as a JSON string (ComponentSerialization.FLAT_CODEC), while 1.21.11 and 26.1 write the
// component natively - a bare string when plain, a compound when it carries style.
static std::string signJsonToText(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString().toStdString();
    }
    if (value.isArray()) {
        std::string out;
        for (const QJsonValue& element : value.toArray()) {
            out += signJsonToText(element);
        }
        return out;
    }
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        std::string out = obj.value("text").toString().toStdString();
        // A translatable component has no literal text; its key beats an empty line.
        if (out.empty()) {
            out = obj.value("translate").toString().toStdString();
        }
        if (obj.contains("extra")) {
            out += signJsonToText(obj.value("extra"));
        }
        return out;
    }
    return {};
}

static std::string signMessageToText(const nbt::value& message)
{
    if (message.get_type() == nbt::tag_type::String) {
        const std::string raw = static_cast<const nbt::tag_string&>(message.get()).get();
        // A 1.21.4 line is JSON, a newer one is literal text that parses as JSON only by accident.
        // Wrapped in an array because QJsonDocument rejects a bare string at top level.
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(
            "[" + QByteArray::fromStdString(raw) + "]", &err);
        if (err.error == QJsonParseError::NoError && doc.isArray()) {
            const QJsonValue inner = doc.array().at(0);
            if (inner.isString() || inner.isObject() || inner.isArray()) {
                return signJsonToText(inner);
            }
        }
        return raw;
    }
    if (message.get_type() == nbt::tag_type::Compound) {
        const auto& comp = static_cast<const nbt::tag_compound&>(message.get());
        std::string out;
        if (comp.has_key("text")) {
            out = static_cast<const nbt::tag_string&>(comp.at("text").get()).get();
        }
        if (out.empty() && comp.has_key("translate")) {
            out = static_cast<const nbt::tag_string&>(comp.at("translate").get()).get();
        }
        if (comp.has_key("extra")) {
            try {
                const auto& extra = static_cast<const nbt::tag_list&>(comp.at("extra").get());
                for (const nbt::value& element : extra) {
                    out += signMessageToText(element);
                }
            } catch (...) {}
        }
        return out;
    }
    return {};
}

// front_text/back_text/is_waxed, present only on the block entities that carry them (signs).
static void fillSignText(PyBlockEntity& e, const nbt::tag_compound& be)
{
    for (const char* key : {"front_text", "back_text"}) {
        if (!be.has_key(key)) continue;
        try {
            const auto& side = static_cast<const nbt::tag_compound&>(be.at(key).get());
            if (!side.has_key("messages")) continue;
            const auto& messages = static_cast<const nbt::tag_list&>(side.at("messages").get());
            auto& out = (std::string(key) == "front_text") ? e.frontText : e.backText;
            for (const nbt::value& message : messages) {
                out.push_back(signMessageToText(message));
            }
            e.isSign = true;
        } catch (...) {}
    }
    if (be.has_key("is_waxed")) {
        try {
            e.isWaxed = static_cast<const nbt::tag_byte&>(be.at("is_waxed").get()).get() != 0;
            e.isSign = true;
        } catch (...) {}
    }
}

// Converts a block_entity compound from a chunk's block_entities list read off disk. `root` owns the
// chunk compound this one lives in; `nbt` aliases into it rather than copying, so a chunk scan that
// never touches `nbt` pays nothing for it.
static PyBlockEntity diskBlockEntityToPy(const nbt::tag_compound& be,
                                         const std::shared_ptr<ItemRegistry>& registry,
                                         const std::shared_ptr<const nbt::tag_compound>& root)
{
    PyBlockEntity e;

    if (be.has_key("id")) {
        e.type = static_cast<const nbt::tag_string&>(be.at("id").get()).get();
    }
    e.x = be.has_key("x") ? static_cast<const nbt::tag_int&>(be.at("x").get()).get() : 0;
    e.y = be.has_key("y") ? static_cast<const nbt::tag_int&>(be.at("y").get()).get() : 0;
    e.z = be.has_key("z") ? static_cast<const nbt::tag_int&>(be.at("z").get()).get() : 0;

    if (be.has_key("Items")) {
        try {
            const auto& itemsList = static_cast<const nbt::tag_list&>(be.at("Items").get());
            py::list items;
            for (const nbt::value& entry : itemsList) {
                const auto& itemTag = static_cast<const nbt::tag_compound&>(entry.get());
                items.append(diskItemToDict(itemTag, registry));
            }
            e.items = items;
            e.hasItems = true;
        } catch (...) {}
    }

    fillSignText(e, be);

    if (root) {
        e.parsedNbt = std::shared_ptr<const nbt::tag_compound>(root, &be);
    }
    return e;
}

static PyBlockEntity buildBlockEntity(const BlockEntityData& be, bool includeItems)
{
    PyBlockEntity e;
    e.type = be.type.toStdString();
    e.x = be.x;
    e.y = be.y;
    e.z = be.z;
    if (includeItems && !be.items.isEmpty()) {
        py::list items;
        for (const auto& item : be.items) {
            items.append(buildItemDict(item));
        }
        e.items = items;
        e.hasItems = true;
    }
    e.rawNbt = std::string(be.rawNbt.constData(), static_cast<size_t>(be.rawNbt.size()));
    // Sign lines are the one thing parsed up front (see PyBlockEntity), and only for signs, so a
    // chunk scan does not read NBT for every chest it passes.
    if (!e.rawNbt.empty() && be.type.endsWith(QLatin1String("sign"))) {
        try {
            std::istringstream ss(e.rawNbt, std::ios::binary);
            nbt::io::stream_reader reader(ss);
            auto tagPtr = reader.read_payload(nbt::tag_type::Compound);
            fillSignText(e, static_cast<nbt::tag_compound&>(*tagPtr));
        } catch (...) {}
    }
    return e;
}

// ---------------------------------------------------------------------------
// BlockEntity accessors
// ---------------------------------------------------------------------------

py::object PythonAPI::blockEntityNbt(PyBlockEntity &be)
{
    if (be.nbtCache) return be.nbtCache;

    py::object result = py::none();
    if (be.parsedNbt) {
        result = nbtCompoundToPy(*be.parsedNbt);
    } else if (!be.rawNbt.empty()) {
        try {
            std::istringstream ss(be.rawNbt, std::ios::binary);
            nbt::io::stream_reader reader(ss);
            auto tagPtr = reader.read_payload(nbt::tag_type::Compound);
            result = nbtCompoundToPy(static_cast<nbt::tag_compound&>(*tagPtr));
        } catch (...) {}
    }
    be.nbtCache = result;
    return result;
}

bool PythonAPI::blockEntityContains(const PyBlockEntity &be, const std::string &key)
{
    if (key == "type" || key == "x" || key == "y" || key == "z") return true;
    if (key == "items") return be.hasItems;
    if (key == "front_text" || key == "back_text" || key == "is_waxed") return be.isSign;
    if (key == "nbt") return be.parsedNbt != nullptr || !be.rawNbt.empty();
    return false;
}

py::object PythonAPI::blockEntityGetItem(PyBlockEntity &be, const std::string &key)
{
    if (!blockEntityContains(be, key)) throw py::key_error(key);
    if (key == "type") return py::cast(be.type);
    if (key == "x") return py::cast(be.x);
    if (key == "y") return py::cast(be.y);
    if (key == "z") return py::cast(be.z);
    if (key == "items") return be.items;
    if (key == "front_text") return py::cast(be.frontText);
    if (key == "back_text") return py::cast(be.backText);
    if (key == "is_waxed") return py::cast(be.isWaxed);
    return blockEntityNbt(be);
}

py::object PythonAPI::blockEntityGet(PyBlockEntity &be, const std::string &key, py::object fallback)
{
    if (!blockEntityContains(be, key)) return fallback;
    return blockEntityGetItem(be, key);
}

py::list PythonAPI::blockEntityKeys(const PyBlockEntity &be)
{
    py::list keys;
    for (const char *key : {"type", "x", "y", "z", "items", "front_text", "back_text",
                            "is_waxed", "nbt"}) {
        if (blockEntityContains(be, key)) keys.append(key);
    }
    return keys;
}

std::string PythonAPI::blockEntityRepr(const PyBlockEntity &be)
{
    return "<BlockEntity " + (be.type.empty() ? std::string("?") : be.type) + " at "
           + std::to_string(be.x) + "," + std::to_string(be.y) + "," + std::to_string(be.z) + ">";
}


// ---------------------------------------------------------------------------
// getBlock / getLight
// ---------------------------------------------------------------------------

// Chunks are keyed by (x, z) only, but each carries the dimension it was loaded from, so an
// in-memory read is valid whenever the loaded column is the dimension being asked for. An empty
// `dim` means the caller named none and the bot has not reported one yet: take whatever is loaded.
static bool chunkIsDimension(const ChunkData *chunk, const QString &dim)
{
    return chunk && (dim.isEmpty() || chunk->dimension == dim);
}

py::object PythonAPI::getBlock(double x, double y, double z, bool useDisk, const std::string &dimension, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    int iz = static_cast<int>(std::floor(z));

    QString dim = dimension.empty() ? botInstance->dimension : QString::fromStdString(dimension);

    std::optional<QString> blockOpt;
    {
        QReadLocker locker(botInstance->worldDataLock.get());
        const ChunkData *chunk = botInstance->worldData.getChunk(ix >> 4, iz >> 4);
        if (chunkIsDimension(chunk, dim)) {
            blockOpt = chunk->getBlock(ix & 15, iy, iz & 15);
        }
    }
    if (blockOpt.has_value()) {
        return py::str(blockOpt.value().toStdString());
    }

    if (!useDisk || !botInstance->worldAutoSaver) {
        return py::none();
    }

    nbt::tag_compound chunkNbt;
    {
        py::gil_scoped_release gil;
        chunkNbt = readChunkNBT(*botInstance->worldAutoSaver, ix >> 4, iz >> 4, dim);
    }

    if (!chunkNbt.has_key("sections")) return py::none();

    ChunkData chunkData = NBTSerializer::nbtToChunk(chunkNbt);
    auto block = chunkData.getBlock(ix & 15, iy, iz & 15);
    if (block.has_value()) {
        return py::str(block.value().toStdString());
    }
    return py::none();
}

py::object PythonAPI::getLight(double x, double y, double z, bool useDisk, const std::string &dimension, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    int iz = static_cast<int>(std::floor(z));

    QString dim = dimension.empty() ? botInstance->dimension : QString::fromStdString(dimension);

    std::optional<ChunkSection::LightLevels> light;
    {
        QReadLocker locker(botInstance->worldDataLock.get());
        const ChunkData *chunk = botInstance->worldData.getChunk(ix >> 4, iz >> 4);
        if (chunkIsDimension(chunk, dim)) {
            light = chunk->getLight(ix & 15, iy, iz & 15);
        }
    }
    if (light.has_value()) {
        py::dict result;
        result["block"] = light->block;
        result["sky"] = light->sky;
        return result;
    }

    if (!useDisk || !botInstance->worldAutoSaver) {
        return py::none();
    }

    nbt::tag_compound chunkNbt;
    {
        py::gil_scoped_release gil;
        chunkNbt = readChunkNBT(*botInstance->worldAutoSaver, ix >> 4, iz >> 4, dim);
    }

    if (!chunkNbt.has_key("sections")) return py::none();

    ChunkData chunkData = NBTSerializer::nbtToChunk(chunkNbt);
    auto levels = chunkData.getLight(ix & 15, iy, iz & 15);
    py::dict result;
    result["block"] = levels.block;
    result["sky"] = levels.sky;
    return result;
}

// ---------------------------------------------------------------------------
// getBlockEntity
// ---------------------------------------------------------------------------

std::optional<PyBlockEntity> PythonAPI::getBlockEntity(double x, double y, double z, bool useDisk, const std::string &dimension, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    int iz = static_cast<int>(std::floor(z));

    QString dim = dimension.empty() ? botInstance->dimension : QString::fromStdString(dimension);
    std::optional<BlockEntityData> beOpt;
    {
        QReadLocker locker(botInstance->worldDataLock.get());
        beOpt = botInstance->worldData.getBlockEntity(ix, iy, iz, dim);
    }

    // Use memory if items are known; if use_disk and items are absent, fall through to disk
    if (beOpt.has_value() && (!useDisk || !beOpt->items.isEmpty())) {
        return buildBlockEntity(beOpt.value(), true);
    }

    if (!useDisk || !botInstance->worldAutoSaver) {
        if (beOpt.has_value()) return buildBlockEntity(beOpt.value(), true);
        return std::nullopt;
    }

    // Held by shared_ptr because the block entity handed back aliases into it for `nbt`.
    std::shared_ptr<const nbt::tag_compound> root;
    {
        py::gil_scoped_release gil;
        root = std::make_shared<const nbt::tag_compound>(readChunkNBT(*botInstance->worldAutoSaver, ix >> 4, iz >> 4, dim));
    }

    if (!root->has_key("block_entities")) {
        if (beOpt.has_value()) return buildBlockEntity(beOpt.value(), true);
        return std::nullopt;
    }

    try {
        const auto& beList = static_cast<const nbt::tag_list&>(root->at("block_entities").get());
        for (const nbt::value& entry : beList) {
            const auto& be = static_cast<const nbt::tag_compound&>(entry.get());
            int bex = be.has_key("x") ? static_cast<const nbt::tag_int&>(be.at("x").get()).get() : 0;
            int bey = be.has_key("y") ? static_cast<const nbt::tag_int&>(be.at("y").get()).get() : 0;
            int bez = be.has_key("z") ? static_cast<const nbt::tag_int&>(be.at("z").get()).get() : 0;
            if (bex == ix && bey == iy && bez == iz) {
                return diskBlockEntityToPy(be, botInstance->itemRegistry, root);
            }
        }
    } catch (...) {}

    // Disk didn't find it - fall back to memory data if available
    if (beOpt.has_value()) return buildBlockEntity(beOpt.value(), true);
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// getBlockEntitiesInChunk
// ---------------------------------------------------------------------------

std::vector<PyBlockEntity> PythonAPI::getBlockEntitiesInChunk(int chunkX, int chunkZ, bool useDisk,
                                                              const std::string &dimension, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    QString dim = dimension.empty() ? botInstance->dimension : QString::fromStdString(dimension);

    bool chunkLoaded;
    {
        QReadLocker locker(botInstance->worldDataLock.get());
        chunkLoaded = chunkIsDimension(botInstance->worldData.getChunk(chunkX, chunkZ), dim);
    }

    if (chunkLoaded) {
        QVector<BlockEntityData> bees;
        {
            QReadLocker locker(botInstance->worldDataLock.get());
            bees = botInstance->worldData.getBlockEntitiesInChunk(chunkX, chunkZ, dim);
        }
        std::vector<PyBlockEntity> result;
        for (const auto& be : std::as_const(bees)) {
            result.push_back(buildBlockEntity(be, true));
        }
        return result;
    }

    if (!useDisk || !botInstance->worldAutoSaver) {
        return {};
    }

    std::shared_ptr<const nbt::tag_compound> root;
    {
        py::gil_scoped_release gil;
        root = std::make_shared<const nbt::tag_compound>(readChunkNBT(*botInstance->worldAutoSaver, chunkX, chunkZ, dim));
    }

    if (!root->has_key("block_entities")) return {};

    std::vector<PyBlockEntity> result;
    try {
        const auto& beList = static_cast<const nbt::tag_list&>(root->at("block_entities").get());
        for (const nbt::value& entry : beList) {
            const auto& be = static_cast<const nbt::tag_compound&>(entry.get());
            result.push_back(diskBlockEntityToPy(be, botInstance->itemRegistry, root));
        }
    } catch (...) {}
    return result;
}

py::object PythonAPI::isBlockSolid(const std::string &blockState, BlockRegistry::Direction face, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);
    if (!botInstance->blockRegistry || !botInstance->blockRegistry->isLoaded())
        return py::none();
    auto stateId = botInstance->blockRegistry->getStateId(QString::fromStdString(blockState));
    if (!stateId.has_value())
        return py::bool_(false);
    return py::bool_(botInstance->blockRegistry->isFaceSolid(stateId.value(), face));
}

py::list PythonAPI::findBlocks(const std::string &blockType, double centerX, double centerY, double centerZ,
                                int radius,
                                int minBlockLight, int maxBlockLight,
                                int minSkyLight, int maxSkyLight,
                                const std::string &bot)
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
                            if (minBlockLight == 0 && maxBlockLight == 15 && minSkyLight == 0 && maxSkyLight == 15) {
                                results.append(QVector3D(worldX, y, worldZ));
                            } else {
                                auto light = chunkCopy.getLight(x, y, z);
                                if (light.block >= minBlockLight && light.block <= maxBlockLight &&
                                    light.sky   >= minSkyLight   && light.sky   <= maxSkyLight) {
                                    results.append(QVector3D(worldX, y, worldZ));
                                }
                            }
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
    for (const ChunkPos &pos : std::as_const(chunks)) {
        py::tuple coord = py::make_tuple(pos.x, pos.z);
        chunkList.append(coord);
    }

    return chunkList;
}

namespace {

// One section encodes to ~8.3 KB, so this caps a single export at roughly 34 MB. Without a
// bound, handing a full first snapshot (~100k sections at render distance 32) straight to
// export_sections builds a payload big enough to matter, twice over: once as per-section
// blobs and again as the concatenated result.
constexpr size_t kMaxExportSections = 4096;

struct PendingSection {
    SectionKey key;
    QByteArray dimensionUtf8;
    ChunkSection section;
};

// Shallow-copy the named sections under one read lock. Qt's implicit sharing makes each copy O(1);
// hashing and encoding then run without the lock, so world writes are never blocked behind BLAKE2b.
// Sections whose chunk unloaded (or that fail the dimension filter) are dropped, not reported empty.
QVector<PendingSection> snapshotSections(BotInstance *botInstance, const QVector<SectionKey> &keys,
                                         const QByteArray &dimensionFilter)
{
    QVector<PendingSection> pending;
    pending.reserve(keys.size());
    QReadLocker locker(botInstance->worldDataLock.get());
    for (const SectionKey &key : keys) {
        const ChunkData *chunk = botInstance->worldData.getChunk(key.chunkX, key.chunkZ);
        if (!chunk) {
            continue;
        }
        QByteArray dim = chunk->dimension.toUtf8();
        if (!dimensionFilter.isEmpty() && dim != dimensionFilter) {
            continue;
        }
        auto it = chunk->sections.constFind(key.sectionY);
        if (it == chunk->sections.constEnd()) {
            continue;
        }
        pending.append({key, dim, *it});
    }
    return pending;
}

std::string toStdBytes(const QByteArray &bytes)
{
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

}

PySectionChanges PythonAPI::changedSections(const std::string &bot, const py::object &since,
                                            const std::string &dimension, bool digest, int limit,
                                            const py::bytes &digestPrefix)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    std::optional<quint64> sinceSeq;
    if (!since.is_none()) {
        sinceSeq = since.cast<quint64>();
    }
    const QByteArray dimensionFilter = QByteArray::fromStdString(dimension);
    // Decoded while the GIL is still held; hashed ahead of each canonical blob below.
    const QByteArray digestPrefixBytes = QByteArray::fromStdString(digestPrefix.cast<std::string>());
    // Copied out so polling the tracker does not depend on the bot pointer once the GIL is gone.
    // The world read below still goes through botInstance, under the same lifetime assumption the
    // rest of PythonAPI makes.
    const std::shared_ptr<SectionDirtyTracker> tracker = botInstance->sectionDirty;

    PySectionChanges result;
    {
        py::gil_scoped_release release;

        QVector<SectionKey> keys;
        const SectionDirtyTracker::Snapshot snap = tracker->snapshot(sinceSeq, limit, keys);
        result.token = snap.token;
        result.truncated = snap.truncated;

        const QVector<PendingSection> pending = snapshotSections(botInstance, keys, dimensionFilter);
        result.sections.reserve(pending.size());
        for (const PendingSection &p : pending) {
            PySectionChange change;
            change.chunkX = p.key.chunkX;
            change.chunkZ = p.key.chunkZ;
            change.sectionY = p.key.sectionY;
            if (digest) {
                auto canon = SectionCodec::canonicalize(p.section);
                if (!canon) {
                    continue;
                }
                change.digestBytes = toStdBytes(SectionCodec::digest(*canon, digestPrefixBytes));
                change.hasDigest = true;
            }
            result.sections.push_back(std::move(change));
        }
    }

    return result;
}

std::optional<PySection> PythonAPI::getSection(int chunkX, int chunkZ, int sectionY,
                                               const std::string &bot, const std::string &dimension,
                                               const py::bytes &digestPrefix)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    const QByteArray dimensionFilter = QByteArray::fromStdString(dimension);
    const QByteArray digestPrefixBytes = QByteArray::fromStdString(digestPrefix.cast<std::string>());
    const QVector<SectionKey> keys{{chunkX, chunkZ, sectionY}};

    std::optional<PySection> result;
    {
        py::gil_scoped_release release;

        const QVector<PendingSection> pending = snapshotSections(botInstance, keys, dimensionFilter);
        if (pending.isEmpty()) {
            return std::nullopt;
        }
        const PendingSection &p = pending.first();
        auto canon = SectionCodec::canonicalize(p.section);
        if (!canon) {
            return std::nullopt;
        }

        PySection section;
        section.chunkX = p.key.chunkX;
        section.chunkZ = p.key.chunkZ;
        section.sectionY = p.key.sectionY;
        section.dimension = toStdBytes(p.dimensionUtf8);
        section.palette.reserve(canon->palette.size());
        for (const QByteArray &name : std::as_const(canon->palette)) {
            section.palette.push_back(toStdBytes(name));
        }
        section.indicesBytes.reserve(static_cast<size_t>(canon->indices.size()) * 2);
        for (quint16 idx : std::as_const(canon->indices)) {
            section.indicesBytes.push_back(static_cast<char>(idx & 0xff));
            section.indicesBytes.push_back(static_cast<char>((idx >> 8) & 0xff));
        }
        section.digestBytes = toStdBytes(SectionCodec::digest(*canon, digestPrefixBytes));
        result = std::move(section);
    }

    return result;
}

py::bytes PythonAPI::exportSections(const py::sequence &keys, const std::string &bot, const std::string &dimension)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    const size_t keyCount = py::len(keys);
    if (keyCount > kMaxExportSections) {
        throw std::runtime_error("export_sections: " + std::to_string(keyCount)
                                 + " keys exceeds the " + std::to_string(kMaxExportSections)
                                 + " per-call limit; batch the upload");
    }

    QVector<SectionKey> parsed;
    parsed.reserve(static_cast<int>(keyCount));
    for (const auto &item : keys) {
        auto entry = item.cast<py::sequence>();
        if (py::len(entry) != 3) {
            throw std::runtime_error("export_sections: each key must be (chunk_x, chunk_z, section_y)");
        }
        parsed.append({entry[0].cast<qint32>(), entry[1].cast<qint32>(), entry[2].cast<qint32>()});
    }
    const QByteArray dimensionFilter = QByteArray::fromStdString(dimension);

    QByteArray payload;
    {
        py::gil_scoped_release release;

        const QVector<PendingSection> pending = snapshotSections(botInstance, parsed, dimensionFilter);
        QVector<SectionCodec::SectionFrame> frames;
        frames.reserve(pending.size());
        for (const PendingSection &p : pending) {
            auto canon = SectionCodec::canonicalize(p.section);
            if (!canon) {
                continue;
            }
            frames.append({p.dimensionUtf8, p.key.chunkX, p.key.chunkZ, p.key.sectionY,
                           SectionCodec::encodeBlob(*canon)});
        }
        payload = SectionCodec::encodeExport(frames);
    }

    return py::bytes(payload.constData(), static_cast<size_t>(payload.size()));
}

static py::dict buildEntityDict(const EntityData &e)
{
    py::dict d;
    d["entity_id"]  = e.entityId;
    d["uuid"]       = e.uuid.toStdString();
    d["type"]       = e.type.toStdString();
    d["x"]          = e.x;
    d["y"]          = e.y;
    d["z"]          = e.z;
    d["yaw"]        = e.yaw;
    d["pitch"]      = e.pitch;
    d["vel_x"]      = e.velX;
    d["vel_y"]      = e.velY;
    d["vel_z"]      = e.velZ;
    if (e.isLiving) {
        d["health"]     = e.health;
        d["max_health"] = e.maxHealth;
    }
    if (e.isItem) {
        d["item"] = buildItemDict(e.itemStack);
    }
    if (e.isPlayer) {
        d["player_name"] = e.playerName.toStdString();
    }
    return d;
}

py::list PythonAPI::getEntities(const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    QVector<EntityData> ents;
    {
        QReadLocker locker(botInstance->worldDataLock.get());
        ents = botInstance->worldData.getAllEntities();
    }

    py::list result;
    for (const auto &e : std::as_const(ents)) {
        result.append(buildEntityDict(e));
    }
    return result;
}

py::list PythonAPI::findEntitiesNear(double x, double y, double z, double radius,
                                     const std::string &typeFilter, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    QVector<EntityData> ents;
    {
        QReadLocker locker(botInstance->worldDataLock.get());
        ents = botInstance->worldData.findEntitiesNear(x, y, z, radius,
                                                       QString::fromStdString(typeFilter));
    }

    py::list result;
    for (const auto &e : std::as_const(ents)) {
        result.append(buildEntityDict(e));
    }
    return result;
}

static void raiseOnReachFailure(const BotManager::ReachBatchResult &result, int total, double timeout)
{
    switch (result.status) {
    case BotManager::ReachBatchResult::Status::Ok:
        return;
    case BotManager::ReachBatchResult::Status::NotConnected:
        throw std::runtime_error("Bot went offline before the reach queries could be sent");
    case BotManager::ReachBatchResult::Status::SendFailed:
        throw std::runtime_error("Failed to send reach queries to the client");
    case BotManager::ReachBatchResult::Status::TimedOut: {
        std::string msg = "client did not answer " + std::to_string(total)
                          + (total == 1 ? " reach query within " : " reach queries within ")
                          + QString::number(timeout).toStdString() + "s";
        PyErr_SetString(PyExc_TimeoutError, msg.c_str());
        throw py::error_already_set();
    }
    case BotManager::ReachBatchResult::Status::Partial:
        if (result.evaluated == 0)
            throw std::runtime_error("Client could not evaluate the reach "
                                     + std::string(total == 1 ? "query" : "queries")
                                     + " (bot is not in a world)");
        throw std::runtime_error("Client evaluated only " + std::to_string(result.evaluated)
                                 + " of " + std::to_string(total) + " reach queries");
    case BotManager::ReachBatchResult::Status::TooLarge:
        // Not a policy limit on batch size, just the largest request that fits in one message.
        // The whole call must go in one message so every query sees the same player position.
        throw std::invalid_argument(std::to_string(total) + " queries exceeds what fits in a single "
                                    + "request to the client; at most " + std::to_string(result.maxQueries)
                                    + " per call. Split the work across calls, noting that each call "
                                    + "re-reads the bot's position.");
    }
}

static bool runSingleReachQuery(const QString &botName, const BotManager::ReachQuery &q, double timeout)
{
    BotManager::ReachBatchResult result;
    {
        py::gil_scoped_release release;
        result = BotManager::sendCanReachBlocks(botName, {q}, static_cast<int>(timeout * 1000.0));
    }
    raiseOnReachFailure(result, 1, timeout);
    return result.results.at(0);
}

bool PythonAPI::canReachBlock(int x, int y, int z, bool sneak, BlockFace face, double timeout, const std::string &bot)
{
    if (timeout <= 0.0)
        throw std::invalid_argument("timeout must be greater than 0");

    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    BotManager::ReachQuery q;
    q.x = x;
    q.y = y;
    q.z = z;
    q.sneak = sneak;
    q.face = static_cast<int>(face);
    return runSingleReachQuery(botName, q, timeout);
}

bool PythonAPI::canReachBlockFrom(int fromX, int fromY, int fromZ, int x, int y, int z, bool sneak, BlockFace face, double timeout, const std::string &bot)
{
    if (timeout <= 0.0)
        throw std::invalid_argument("timeout must be greater than 0");

    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    BotManager::ReachQuery q;
    q.x = x;
    q.y = y;
    q.z = z;
    q.sneak = sneak;
    q.face = static_cast<int>(face);
    q.hasFrom = true;
    q.fromX = fromX;
    q.fromY = fromY;
    q.fromZ = fromZ;
    return runSingleReachQuery(botName, q, timeout);
}

static BotManager::ReachQuery reachQueryFromItem(const py::handle &item, int index,
                                                 bool callSneak, PythonAPI::BlockFace callFace)
{
    BotManager::ReachQuery q;
    q.sneak = callSneak;
    q.face = static_cast<int>(callFace);

    if (py::isinstance<PyReachQuery>(item)) {
        const PyReachQuery &rq = item.cast<const PyReachQuery &>();
        q.x = rq.x;
        q.y = rq.y;
        q.z = rq.z;
        q.hasFrom = rq.hasFrom;
        q.fromX = rq.fromX;
        q.fromY = rq.fromY;
        q.fromZ = rq.fromZ;
        if (rq.sneak.has_value())
            q.sneak = rq.sneak.value();
        if (rq.face.has_value())
            q.face = static_cast<int>(rq.face.value());
        return q;
    }

    // Reject strings and dicts up front: both are sequences, so without this a typo like
    // "1,2,3" would fail with a confusing per-element message instead of a clear one.
    if (py::isinstance<py::str>(item) || py::isinstance<py::dict>(item) || !py::isinstance<py::sequence>(item)) {
        throw std::invalid_argument("queries[" + std::to_string(index)
                                    + "] must be an (x, y, z) tuple or a world.ReachQuery");
    }

    py::sequence seq = item.cast<py::sequence>();
    const int len = static_cast<int>(py::len(seq));
    if (len != 3) {
        throw std::invalid_argument("queries[" + std::to_string(index) + "] must have exactly 3 values (x, y, z), got "
                                    + std::to_string(len)
                                    + "; use world.ReachQuery(x, y, z, from_pos=(fx, fy, fz)) to trace from another position");
    }

    int v[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        try {
            v[i] = seq[i].cast<int>();
        } catch (const py::cast_error &) {
            throw std::invalid_argument("queries[" + std::to_string(index) + "][" + std::to_string(i)
                                        + "] is not a number");
        }
    }
    q.x = v[0];
    q.y = v[1];
    q.z = v[2];
    return q;
}

py::list PythonAPI::canReachBlocks(const py::sequence &queries, bool sneak, BlockFace face,
                                   double timeout, const std::string &bot)
{
    if (timeout <= 0.0)
        throw std::invalid_argument("timeout must be greater than 0");

    if (py::isinstance<py::str>(queries) || py::isinstance<py::bytes>(queries))
        throw std::invalid_argument("queries must be a list of (x, y, z) tuples or world.ReachQuery objects");

    const int total = static_cast<int>(py::len(queries));
    QList<BotManager::ReachQuery> resolved;
    resolved.reserve(total);
    int index = 0;
    for (const auto &item : queries)
        resolved.append(reachQueryFromItem(item, index++, sneak, face));

    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    if (resolved.isEmpty())
        return py::list();

    BotManager::ReachBatchResult result;
    {
        py::gil_scoped_release release;
        result = BotManager::sendCanReachBlocks(botName, resolved, static_cast<int>(timeout * 1000.0));
    }

    raiseOnReachFailure(result, total, timeout);

    py::list out;
    for (bool r : result.results)
        out.append(py::bool_(r));
    return out;
}

void PythonAPI::holdAttack(bool enabled, int durationTicks, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendHoldAttack(name, enabled, durationTicks);
}

bool PythonAPI::getHoldAttack(const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);

    bool result;
    {
        py::gil_scoped_release release;
        result = BotManager::getHoldAttackStatus(name);
    }
    return result;
}

void PythonAPI::holdUse(bool enabled, int durationTicks, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendHoldUse(name, enabled, durationTicks);
}

bool PythonAPI::getHoldUse(const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);

    bool result;
    {
        py::gil_scoped_release release;
        result = BotManager::getHoldUseStatus(name);
    }
    return result;
}

void PythonAPI::lookAt(double x, double y, double z, BlockFace face, bool sneak, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    auto protoFace = static_cast<mankool::mcbot::protocol::BlockFaceGadget::BlockFace>(static_cast<int>(face));
    BotManager::sendLookAt(name, x, y, z, protoFace, sneak);
}

void PythonAPI::lookAtEntity(int entityId, bool sneak, const std::string &botName)
{
    QString name = resolveBotName(botName);
    ensureBotOnline(name);
    BotManager::sendLookAtEntity(name, entityId, sneak);
}

void PythonAPI::interactBlock(double x, double y, double z, bool sneak, bool lookAtBlock, BlockFace face, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    int blockX = static_cast<int>(std::floor(x));
    int blockY = static_cast<int>(std::floor(y));
    int blockZ = static_cast<int>(std::floor(z));

    auto protoFace = static_cast<mankool::mcbot::protocol::BlockFaceGadget::BlockFace>(static_cast<int>(face));
    BotManager::sendInteractWithBlock(botName, blockX, blockY, blockZ,
                                      mankool::mcbot::protocol::HandGadget::Hand::MAIN_HAND, sneak, lookAtBlock, protoFace);
}

void PythonAPI::clickContainerSlot(int slotIndex, int button, ContainerClickType clickType, const std::string &bot, bool silent)
{
    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    BotManager::sendClickContainerSlot(botName, slotIndex, button, static_cast<int>(clickType), silent);
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

void PythonAPI::resyncInventory(const std::string &bot)
{
    QString botName = resolveBotName(bot);
    ensureBotOnline(botName);

    BotManager::sendRequestInventoryResync(botName);
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
    for (const auto &item : std::as_const(botInstance->containerState.items)) {
        items.append(buildItemDict(item));
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
            result["display_name"] = itemInfo->displayName.toStdString();
            result["max_stack_size"] = itemInfo->maxStackSize;
            result["max_damage"] = itemInfo->maxDamage;
            return result;
        }
    }
    
    return py::none();
}

static py::dict recipeToDict(const Recipe *recipe)
{
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

py::object PythonAPI::getRecipe(const std::string &recipeId, const std::string &bot)
{
    QString botName = resolveBotName(bot);

    BotInstance *botInstance = BotManager::getBotByName(botName);
    if (!botInstance || botInstance->status != BotStatus::Online) {
        return py::none();
    }

    const Recipe *recipe = botInstance->recipeRegistry.getRecipe(QString::fromStdString(recipeId));
    if (!recipe) {
        return py::none();
    }

    return recipeToDict(recipe);
}

py::list PythonAPI::getRecipesFor(const std::string &itemId, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    py::list result;

    BotInstance *botInstance = BotManager::getBotByName(botName);
    if (!botInstance || botInstance->status != BotStatus::Online) {
        return result;
    }

    const QVector<const Recipe*> recipes = botInstance->recipeRegistry.getRecipesByResult(QString::fromStdString(itemId));
    for (const Recipe *recipe : recipes) {
        result.append(recipeToDict(recipe));
    }

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
    for (const QString &id : std::as_const(recipeIds)) {
        result.append(id.toStdString());
    }

    return result;
}

py::dict PythonAPI::planRecursiveCraft(const std::string &itemId, int count, const std::string &bot)
{
    QString botName = resolveBotName(bot);
    BotInstance *botInstance = ensureBotOnline(botName);

    // Get bot's current inventory - totals for the DFS planner and per-slot stacks
    // for the space-aware scheduling post-pass.
    QMap<QString, int> available;
    QMap<QString, QList<int>> initialStacks;
    for (const auto &item : std::as_const(botInstance->inventory)) {
        int slotIdx = item.slot();
        if (slotIdx < 0 || slotIdx > 35) continue; // only hotbar (0-8) + main (9-35)
        if (item.itemId().isEmpty() || item.itemId() == "minecraft:air" || item.count() <= 0) continue;
        QString itemId2 = item.itemId();
        available[itemId2] += item.count();
        initialStacks[itemId2].append(item.count());
    }

    // Create planner and plan the craft
    CraftingPlanner planner(&botInstance->recipeRegistry, botInstance->itemRegistry.get());
    CraftingPlan plan = planner.planCrafting(
        QString::fromStdString(itemId), count, available,
        QSet<QString>(), true, initialStacks);

    // Convert CraftingPlan to Python dict
    py::dict result;
    result["success"] = plan.success;
    result["error"] = plan.error.toStdString();

    // Convert steps
    py::list steps;
    for (const CraftingStep &step : std::as_const(plan.steps)) {
        py::dict stepDict;
        stepDict["recipe_id"] = step.recipeId.toStdString();
        stepDict["times"] = step.times;
        stepDict["output_item"] = step.outputItem.toStdString();
        stepDict["output_count"] = step.outputCount;
        stepDict["is_consolidate"] = step.isConsolidate;

        // Convert inputs map
        py::dict inputs;
        for (auto it = step.inputs.constBegin(); it != step.inputs.constEnd(); ++it) {
            inputs[it.key().toStdString().c_str()] = it.value();
        }
        stepDict["inputs"] = inputs;

        steps.append(stepDict);
    }
    result["steps"] = steps;

    // Convert raw materials
    py::dict rawMaterials;
    for (auto it = plan.rawMaterials.constBegin(); it != plan.rawMaterials.constEnd(); ++it) {
        rawMaterials[it.key().toStdString().c_str()] = it.value();
    }
    result["raw_materials"] = rawMaterials;

    // Convert leftovers
    py::dict leftovers;
    for (auto it = plan.leftovers.constBegin(); it != plan.leftovers.constEnd(); ++it) {
        leftovers[it.key().toStdString().c_str()] = it.value();
    }
    result["leftovers"] = leftovers;

    return result;
}

