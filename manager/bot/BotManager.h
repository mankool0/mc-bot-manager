#ifndef BOTMANAGER_H
#define BOTMANAGER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QVector3D>
#include <QProcess>
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QMutex>
#include <QReadWriteLock>
#include <QPointer>
#include <memory>
#include "protocol.qpb.h"
#include "connection.qpb.h"
#include "player.qpb.h"
#include "inventory.qpb.h"
#include "chat.qpb.h"
#include "commands.qpb.h"
#include "common.qpb.h"
#include "meteor.qpb.h"
#include "baritone.qpb.h"
#include "world.qpb.h"
#include "screen.qpb.h"
#include "registry.qpb.h"
#include "WorldData.h"
#include "world/BlockRegistry.h"
#include "world/ItemRegistry.h"
#include "saving/WorldAutoSaver.h"
#include "crafting/RecipeRegistry.h"

using SettingType = mankool::mcbot::protocol::SettingInfo::SettingType;
using BaritoneSettingType = mankool::mcbot::protocol::BaritoneSettingInfo::SettingType;

class LogManager;
class BotConsoleWidget;
class MeteorModulesWidget;
class BaritoneWidget;
class ScriptEngine;
class ScriptsWidget;

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

struct MapMetadata {
    QStringList possibleKeys;
    QStringList possibleValues;
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
using StringListMap = QMap<QString, QStringList>;
using ESPBlockDataMap = QMap<QString, ESPBlockData>;

// Declare metatypes for QVariant
Q_DECLARE_METATYPE(RGBColor)
Q_DECLARE_METATYPE(Vec3i)
Q_DECLARE_METATYPE(BlockRotation)
Q_DECLARE_METATYPE(BlockMirror)
Q_DECLARE_METATYPE(MapMetadata)
Q_DECLARE_METATYPE(RGBAColor)
Q_DECLARE_METATYPE(Vector3d)
Q_DECLARE_METATYPE(Keybind)
Q_DECLARE_METATYPE(ESPBlockData)
Q_DECLARE_METATYPE(StringMap)
Q_DECLARE_METATYPE(StringListMap)
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
    QStringList possibleValues;
    MapMetadata mapMetadata;
};

struct BaritoneCommandData {
    QString name;
    QStringList aliases;
    QString shortDesc;
    QStringList longDesc;
};

struct BaritoneProcessInfo {
    QString processName;
    QString displayName;
    double priority = 0.0;
    bool isActive = false;
    bool isTemporary = false;
};

struct BaritoneProcessStatus {
    mankool::mcbot::protocol::PathEventTypeGadget::PathEventType eventType = mankool::mcbot::protocol::PathEventTypeGadget::PathEventType::PATH_EVENT_CALC_STARTED;
    bool isPathing = false;
    QString goalDescription;
    BaritoneProcessInfo activeProcess;
    bool hasActiveProcess = false;
    double estimatedTicksToGoal = 0.0;
    bool hasEstimatedTicks = false;
    double ticksRemainingInSegment = 0.0;
    bool hasTicksRemaining = false;
};

struct BotInstance {
    QString name;
    BotStatus status = BotStatus::Offline;
    QString instance;
    QString account;
    QString accountId;
    QString server;
    mankool::mcbot::protocol::ServerConnectionStatus_QtProtobufNested::Status serverConnectionStatus = mankool::mcbot::protocol::ServerConnectionStatus_QtProtobufNested::Status::INITIAL;
    int connectionId = -1;
    int maxMemory;
    int currentMemory;
    double restartThreshold;
    bool autoRestart;
    bool tokenRefresh;
    bool debugLogging;
    bool saveWorldToDisk = true;
    bool manualStop = false;
    QDateTime startTime;

    QVector3D position;
    QString dimension;

    float health = 0.0f;
    int foodLevel = 0;
    float saturation = 0.0f;
    int air = 0;
    int experienceLevel = 0;
    float experienceProgress = 0.0f;

    QVector<mankool::mcbot::protocol::ItemStack> inventory;
    int selectedSlot = 0;

    // Container state
    struct {
        bool isOpen = false;
        int containerId = -1;
        mankool::mcbot::protocol::ContainerUpdate::ContainerType containerType = mankool::mcbot::protocol::ContainerUpdate::ContainerType::OTHER;
        QVector<mankool::mcbot::protocol::ItemStack> items;
    } containerState;

    QProcess* process = nullptr;
    qint64 minecraftPid = 0;

    // Network statistics
    qint64 bytesReceived = 0;
    qint64 bytesSent = 0;
    double dataRateIn = 0.0;   // bytes/sec
    double dataRateOut = 0.0;  // bytes/sec

    QPointer<BotConsoleWidget> consoleWidget;
    QPointer<MeteorModulesWidget> meteorWidget;
    QPointer<BaritoneWidget> baritoneWidget;
    ScriptEngine* scriptEngine = nullptr;
    QPointer<ScriptsWidget> scriptsWidget;

    // Meteor modules data
    QMap<QString, MeteorModuleData> meteorModules;

    // Baritone data
    QMap<QString, BaritoneSettingData> baritoneSettings;
    QMap<QString, BaritoneCommandData> baritoneCommands;
    BaritoneProcessStatus baritoneProcessStatus;

    QString currentScreenClass;

    // World data
    BotWorldData worldData;
    std::shared_ptr<BlockRegistry> blockRegistry;
    std::shared_ptr<ItemRegistry> itemRegistry;
    int dataVersion = 0;
    QString versionName;
    QString versionSeries = "main";
    bool versionIsSnapshot = false;
    std::shared_ptr<WorldAutoSaver> worldAutoSaver;
    QString worldAutoSaverServerIp;
    QVector<ChunkData> earlyChunkQueue;

    // Recipe registry
    RecipeRegistry recipeRegistry;

    std::shared_ptr<QMutex> dataMutex = std::make_shared<QMutex>();
    std::shared_ptr<QReadWriteLock> worldDataLock = std::make_shared<QReadWriteLock>();
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
    static void handleBaritoneProcessStatus(int connectionId, const mankool::mcbot::protocol::BaritoneProcessStatusUpdate &status);

    // Block registry handlers
    static void handleQueryRegistry(int connectionId, const mankool::mcbot::protocol::QueryBlockRegistryMessage &query);
    static void handleBlockRegistry(int connectionId, const mankool::mcbot::protocol::BlockRegistryMessage &registry);

    // Item registry handlers
    static void handleQueryItemRegistry(int connectionId, const mankool::mcbot::protocol::QueryItemRegistryMessage &query);
    static void handleItemRegistry(int connectionId, const mankool::mcbot::protocol::ItemRegistryMessage &registry);

    // World data handlers
    static void handleChunkData(int connectionId, const mankool::mcbot::protocol::ChunkDataMessage &chunkData);
    static void handleBlockUpdate(int connectionId, const mankool::mcbot::protocol::BlockUpdateMessage &blockUpdate);
    static void handleMultiBlockUpdate(int connectionId, const mankool::mcbot::protocol::MultiBlockUpdateMessage &multiBlockUpdate);
    static void handleChunkUnload(int connectionId, const mankool::mcbot::protocol::ChunkUnloadMessage &chunkUnload);
    static void handleContainerUpdate(int connectionId, const mankool::mcbot::protocol::ContainerUpdate &containerUpdate);
    static void handleScreenUpdate(int connectionId, const mankool::mcbot::protocol::ScreenUpdate &screen);

    // World interaction commands
    static void sendInteractWithBlock(const QString &botName, int x, int y, int z,
                                      mankool::mcbot::protocol::HandGadget::Hand hand = mankool::mcbot::protocol::HandGadget::Hand::MAIN_HAND,
                                      bool sneak = false,
                                      bool lookAtBlock = true);

    // Container interaction commands
    static void sendClickContainerSlot(const QString &botName, int slotIndex, int button, int clickType);
    static void sendCloseContainer(const QString &botName);
    static void sendOpenInventory(const QString &botName);

    static void sendCommand(const QString &botName, const QString &commandText, bool silent = false);
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
    void baritoneProcessStatusUpdated(const QString &botName);

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
    void handleBaritoneProcessStatusImpl(int connectionId, const mankool::mcbot::protocol::BaritoneProcessStatusUpdate &status);
    void handleQueryRegistryImpl(int connectionId, const mankool::mcbot::protocol::QueryBlockRegistryMessage &query);
    void handleBlockRegistryImpl(int connectionId, const mankool::mcbot::protocol::BlockRegistryMessage &registry);
    void handleQueryItemRegistryImpl(int connectionId, const mankool::mcbot::protocol::QueryItemRegistryMessage &query);
    void handleItemRegistryImpl(int connectionId, const mankool::mcbot::protocol::ItemRegistryMessage &registry);
    void handleChunkDataImpl(int connectionId, const mankool::mcbot::protocol::ChunkDataMessage &chunkData);
    void handleBlockUpdateImpl(int connectionId, const mankool::mcbot::protocol::BlockUpdateMessage &blockUpdate);
    void handleMultiBlockUpdateImpl(int connectionId, const mankool::mcbot::protocol::MultiBlockUpdateMessage &multiBlockUpdate);
    void handleChunkUnloadImpl(int connectionId, const mankool::mcbot::protocol::ChunkUnloadMessage &chunkUnload);
    void handleContainerUpdateImpl(int connectionId, const mankool::mcbot::protocol::ContainerUpdate &containerUpdate);
    void handleScreenUpdateImpl(int connectionId, const mankool::mcbot::protocol::ScreenUpdate &screen);
    void sendInteractWithBlockImpl(const QString &botName, int x, int y, int z,
                                   mankool::mcbot::protocol::HandGadget::Hand hand, bool sneak, bool lookAtBlock);
    void sendClickContainerSlotImpl(const QString &botName, int slotIndex, int button, int clickType);
    void sendCloseContainerImpl(const QString &botName);
    void sendOpenInventoryImpl(const QString &botName);
    void sendCommandImpl(const QString &botName, const QString &commandText, bool silent);
    void sendShutdownCommandImpl(const QString &botName, const QString &reason);
    void requestBaritoneSettingsImpl(const QString &botName);
    void requestBaritoneCommandsImpl(const QString &botName);
    void sendBaritoneCommandImpl(const QString &botName, const QString &commandText);
    void sendBaritoneSettingChangeImpl(const QString &botName, const QString &settingName, const QVariant &value);
    void sendMeteorSettingChangeImpl(const QString &botName, const QString &moduleName, const QString &settingPath, const QVariant &value);

    // Helper to initialize WorldAutoSaver when both server and dataVersion are available
    void tryInitializeWorldAutoSaver(BotInstance* bot);

    QVector<BotInstance> botInstances;
    QSet<QString> silentMessageIds;

    // Block state registry cache: data_version -> (state_id -> block_state_string)
    QMap<int, QMap<quint32, QString>> blockRegistryCache;
};

#endif // BOTMANAGER_H
