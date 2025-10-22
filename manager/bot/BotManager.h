#ifndef BOTMANAGER_H
#define BOTMANAGER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QVector3D>
#include <QProcess>
#include <QDateTime>
#include <QMap>
#include "protocol.qpb.h"
#include "connection.qpb.h"
#include "player.qpb.h"
#include "inventory.qpb.h"
#include "chat.qpb.h"
#include "commands.qpb.h"
#include "common.qpb.h"
#include "meteor.qpb.h"
#include "baritone.qpb.h"

using SettingType = mankool::mcbot::protocol::SettingInfo::SettingType;
using BaritoneSettingType = mankool::mcbot::protocol::BaritoneSettingInfo::SettingType;

class LogManager;
class BotConsoleWidget;
class MeteorModulesWidget;
class BaritoneWidget;

// Custom types for Baritone settings
struct RGBColor {
    uint32_t red = 0;
    uint32_t green = 0;
    uint32_t blue = 0;
};

struct Vec3i {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

enum class BlockRotation {
    None = 0,
    Clockwise90 = 1,
    Clockwise180 = 2,
    CounterClockwise90 = 3
};

enum class BlockMirror {
    None = 0,
    LeftRight = 1,
    FrontBack = 2
};

// Custom types for Meteor settings
struct RGBAColor {
    uint32_t red = 0;
    uint32_t green = 0;
    uint32_t blue = 0;
    uint32_t alpha = 255;
};

struct Vector3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Keybind {
    QString keyName;
};

struct ESPBlockData {
    enum ShapeMode {
        Lines = 0,
        Sides = 1,
        Both = 2
    };
    ShapeMode shapeMode = Lines;
    RGBAColor lineColor;
    RGBAColor sideColor;
    bool tracer = false;
    RGBAColor tracerColor;
};

// Type aliases for QMap types (needed for Q_DECLARE_METATYPE)
using StringMap = QMap<QString, QString>;
using ESPBlockDataMap = QMap<QString, ESPBlockData>;

// Declare metatypes for QVariant
Q_DECLARE_METATYPE(RGBColor)
Q_DECLARE_METATYPE(Vec3i)
Q_DECLARE_METATYPE(BlockRotation)
Q_DECLARE_METATYPE(BlockMirror)
Q_DECLARE_METATYPE(RGBAColor)
Q_DECLARE_METATYPE(Vector3d)
Q_DECLARE_METATYPE(Keybind)
Q_DECLARE_METATYPE(ESPBlockData)
Q_DECLARE_METATYPE(StringMap)
Q_DECLARE_METATYPE(ESPBlockDataMap)

enum class BotStatus {
    Offline,
    Starting,
    Online,
    Stopping,
    Error
};

struct MeteorSettingData {
    QString name;
    QString groupName;
    QVariant currentValue;
    QString description;
    SettingType type;
    double minValue;
    double maxValue;
    bool hasMin;
    bool hasMax;
    QStringList possibleValues;  // For enum types
};

struct MeteorModuleData {
    QString name;
    QString category;
    QString description;
    bool enabled;
    QMap<QString, MeteorSettingData> settings;
};

struct BaritoneSettingData {
    QString name;
    BaritoneSettingType type;
    QVariant currentValue;
    QVariant defaultValue;
    QString description;
};

struct BaritoneCommandData {
    QString name;
    QStringList aliases;
    QString shortDesc;
    QStringList longDesc;
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
    double restartThreshold;
    bool autoRestart;
    bool tokenRefresh;
    bool debugLogging;
    bool manualStop = false;
    QDateTime startTime;

    QVector3D position;
    QString dimension;

    QProcess* process = nullptr;
    qint64 minecraftPid = 0;

    // Network statistics
    qint64 bytesReceived = 0;
    qint64 bytesSent = 0;
    double dataRateIn = 0.0;   // bytes/sec
    double dataRateOut = 0.0;  // bytes/sec

    BotConsoleWidget* consoleWidget = nullptr;
    MeteorModulesWidget* meteorWidget = nullptr;
    BaritoneWidget* baritoneWidget = nullptr;

    // Meteor modules data
    QMap<QString, MeteorModuleData> meteorModules;

    // Baritone data
    QMap<QString, BaritoneSettingData> baritoneSettings;
    QMap<QString, BaritoneCommandData> baritoneCommands;
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
    static void handleModulesResponse(int connectionId, const mankool::mcbot::protocol::GetModulesResponse &response);
    static void handleModuleConfigResponse(int connectionId, const mankool::mcbot::protocol::SetModuleConfigResponse &response);
    static void handleModuleStateChanged(int connectionId, const mankool::mcbot::protocol::ModuleStateChanged &stateChange);

    // Baritone handlers
    static void handleBaritoneSettingsResponse(int connectionId, const mankool::mcbot::protocol::GetBaritoneSettingsResponse &response);
    static void handleBaritoneCommandsResponse(int connectionId, const mankool::mcbot::protocol::GetBaritoneCommandsResponse &response);
    static void handleBaritoneSettingsSetResponse(int connectionId, const mankool::mcbot::protocol::SetBaritoneSettingsResponse &response);
    static void handleBaritoneCommandResponse(int connectionId, const mankool::mcbot::protocol::ExecuteBaritoneCommandResponse &response);
    static void handleBaritoneSettingUpdate(int connectionId, const mankool::mcbot::protocol::BaritoneSettingUpdate &update);

    static void sendCommand(const QString &botName, const QString &commandText);
    static void sendShutdownCommand(const QString &botName, const QString &reason = "");
    static void requestBaritoneSettings(const QString &botName);
    static void requestBaritoneCommands(const QString &botName);
    static void sendBaritoneCommand(const QString &botName, const QString &commandText);
    static void sendBaritoneSettingChange(const QString &botName, const QString &settingName, const QVariant &value);
    static void sendMeteorSettingChange(const QString &botName, const QString &moduleName, const QString &settingPath, const QVariant &value);

    static QString getSettingPath(const mankool::mcbot::protocol::SettingInfo &setting);

signals:
    void botAdded(const QString &name);
    void botRemoved(const QString &name);
    void botUpdated(const QString &name);
    void meteorModulesReceived(const QString &botName);
    void meteorSingleModuleUpdated(const QString &botName, const QString &moduleName);
    void baritoneSettingsReceived(const QString &botName);
    void baritoneCommandsReceived(const QString &botName);
    void baritoneSingleSettingUpdated(const QString &botName, const QString &settingName);

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
    void handleModulesResponseImpl(int connectionId, const mankool::mcbot::protocol::GetModulesResponse &response);
    void handleModuleConfigResponseImpl(int connectionId, const mankool::mcbot::protocol::SetModuleConfigResponse &response);
    void handleModuleStateChangedImpl(int connectionId, const mankool::mcbot::protocol::ModuleStateChanged &stateChange);
    void handleBaritoneSettingsResponseImpl(int connectionId, const mankool::mcbot::protocol::GetBaritoneSettingsResponse &response);
    void handleBaritoneCommandsResponseImpl(int connectionId, const mankool::mcbot::protocol::GetBaritoneCommandsResponse &response);
    void handleBaritoneSettingsSetResponseImpl(int connectionId, const mankool::mcbot::protocol::SetBaritoneSettingsResponse &response);
    void handleBaritoneCommandResponseImpl(int connectionId, const mankool::mcbot::protocol::ExecuteBaritoneCommandResponse &response);
    void handleBaritoneSettingUpdateImpl(int connectionId, const mankool::mcbot::protocol::BaritoneSettingUpdate &update);
    void sendCommandImpl(const QString &botName, const QString &commandText);
    void sendShutdownCommandImpl(const QString &botName, const QString &reason);
    void requestBaritoneSettingsImpl(const QString &botName);
    void requestBaritoneCommandsImpl(const QString &botName);
    void sendBaritoneCommandImpl(const QString &botName, const QString &commandText);
    void sendBaritoneSettingChangeImpl(const QString &botName, const QString &settingName, const QVariant &value);
    void sendMeteorSettingChangeImpl(const QString &botName, const QString &moduleName, const QString &settingPath, const QVariant &value);

    QVector<BotInstance> botInstances;
};

#endif // BOTMANAGER_H
