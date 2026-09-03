#ifndef PYTHONAPI_H
#define PYTHONAPI_H

#include <QString>
#include <QVariant>
#include <QPointer>
#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <memory>
#include "world/BlockRegistry.h"
#include <tag_compound.h>

#undef slots
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#define slots Q_SLOTS

namespace py = pybind11;

struct RGBAColor;
struct ESPBlockData;
class BotConsoleWidget;

struct PyGuiWidget {
    int index = 0;
    std::string widgetType;
    std::string className;
    int x = 0, y = 0, width = 0, height = 0;
    bool active = false, visible = false, selected = false;
    std::string text;
    std::string editValue;
    bool editEditable = false;
};

struct PyGuiSlot {
    int index = 0;
    int x = 0, y = 0;
    bool active = false;
    std::string itemId;
    int count = 0;
    std::string displayName;
    int damage = 0;
    int maxDamage = 0;
    std::map<std::string, int> enchantments;
    int repairCost = 0;
};

struct PyScreenState {
    std::string screenId;
    std::string screenClass;
    std::string title;
    int width = 0, height = 0;
    std::vector<PyGuiWidget> widgets;
    std::vector<PyGuiSlot> guiSlots;
};

// One chunk section reported by world.changed_sections(). hasDigest is false when the caller
// did not ask for digests, which surfaces in Python as .digest being None rather than bytes.
struct PySectionChange {
    int chunkX = 0;
    int chunkZ = 0;
    int sectionY = 0;
    std::string digestBytes;
    bool hasDigest = false;
};

struct PySectionChanges {
    unsigned long long token = 0;
    bool truncated = false;
    std::vector<PySectionChange> sections;
};

// Format-neutral view of one section's blocks, for scripts that want to work with the
// content themselves rather than hand an opaque payload to a server.
struct PySection {
    int chunkX = 0;
    int chunkZ = 0;
    int sectionY = 0;
    std::string dimension;
    std::vector<std::string> palette;
    std::string indicesBytes;  // 4096 u16 little-endian, YZX order (y*256 + z*16 + x)
    std::string digestBytes;
};

struct PyMonitor {
    std::string name;
    bool primary = false;
    int x = 0, y = 0, width = 0, height = 0;
    int work_x = 0, work_y = 0, work_width = 0, work_height = 0;
};

struct PyWindowState {
    std::string platform;
    bool can_move = false;
    std::string monitor;
    int x = 0, y = 0, width = 0, height = 0;
    bool minimized = false;
    bool focused = false;
    bool visible = true;
    std::vector<PyMonitor> monitors;
};

// Suppress visibility warning: pybind11 types have hidden visibility
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

struct PyBlockEntity {
    std::string type;
    int x = 0, y = 0, z = 0;
    py::list items;
    bool hasItems = false;
    std::vector<std::string> frontText;
    std::vector<std::string> backText;
    bool isSign = false;
    bool isWaxed = false;
    std::string rawNbt;
    std::shared_ptr<const nbt::tag_compound> parsedNbt;
    py::object nbtCache;
};

#pragma GCC diagnostic pop

struct PyServerInfo {
    std::string address;
    std::string motd;
    int64_t ping = 0;
    std::string version;
    int players_online = 0;
    int players_max = 0;
};

enum class Gamemode {
    SURVIVAL = 0,
    CREATIVE = 1,
    ADVENTURE = 2,
    SPECTATOR = 3,
};

struct PyTabListPlayer {
    std::string name;
    std::string uuid;
    int ping = 0;
    Gamemode gamemode = Gamemode::SURVIVAL;
    std::string display_name;
};

enum class BlockFace {
    AUTO = 0,
    DOWN = 1,
    UP = 2,
    NORTH = 3,
    SOUTH = 4,
    WEST = 5,
    EAST = 6
};

struct PyReachQuery {
    int x = 0;
    int y = 0;
    int z = 0;
    bool hasFrom = false;
    int fromX = 0;
    int fromY = 0;
    int fromZ = 0;
    std::optional<bool> sneak;
    std::optional<BlockFace> face;
};

class PythonAPI
{
public:
    using BlockFace = ::BlockFace;

    // Enums for container interaction
    enum class MouseButton {
        LEFT = 0,
        RIGHT = 1,
        MIDDLE = 2
    };

    enum class Hand {
        MAIN = 0,
        OFF = 1
    };

    enum class ContainerClickType {
        PICKUP = 0,
        QUICK_MOVE = 1,
        SWAP = 2,
        CLONE = 3,
        THROW = 4,
        QUICK_CRAFT = 5,
        PICKUP_ALL = 6
    };

    enum class PathEventType {
        CALC_STARTED = 0,
        CALC_FINISHED_NOW_EXECUTING = 1,
        CALC_FAILED = 2,
        NEXT_SEGMENT_CALC_STARTED = 3,
        NEXT_SEGMENT_CALC_FINISHED = 4,
        CONTINUING_ONTO_PLANNED_NEXT = 5,
        SPLICING_ONTO_NEXT_EARLY = 6,
        AT_GOAL = 7,
        PATH_FINISHED_NEXT_STILL_CALCULATING = 8,
        NEXT_CALC_FAILED = 9,
        DISCARD_NEXT = 10,
        CANCELED = 11
    };

    static void setCurrentBot(const QString &botName);
    static QString getCurrentBot();
    static void setCurrentScript(const QString &scriptName);
    static QString getCurrentScript();

    static py::object getPosition(const std::string &botName = "");
    static py::object getDimension(const std::string &botName = "");
    static std::optional<float> getHealth(const std::string &botName = "");
    static std::optional<int> getHunger(const std::string &botName = "");
    static std::optional<float> getSaturation(const std::string &botName = "");
    static std::optional<int> getAir(const std::string &botName = "");
    static std::optional<int> getExperienceLevel(const std::string &botName = "");
    static std::optional<float> getExperienceProgress(const std::string &botName = "");
    static std::optional<int> getSelectedSlot(const std::string &botName = "");
    static void selectSlot(int slot, const std::string &botName = "");
    static py::object getRotation(const std::string &botName = "");
    static void rotate(float yaw, float pitch, const std::string &botName = "");
    static void useItem(Hand hand = Hand::MAIN, const std::string &botName = "");
    static void dropItem(bool dropAll = false, const std::string &botName = "");
    static std::optional<std::string> getServer(const std::string &botName = "");
    static std::optional<std::string> getSingleplayerWorld(const std::string &botName = "");
    static bool getIsSingleplayer(const std::string &botName = "");
    static std::optional<std::string> getAccount(const std::string &botName = "");
    static std::optional<int> getDataVersion(const std::string &botName = "");
    static std::optional<int64_t> getUptime(const std::string &botName = "");
    static py::object getProxy(const std::string &botName = "");
    static bool isOnline(const std::string &botName = "");
    static std::string getStatus(const std::string &botName = "");
    static py::object getInventory(const std::string &botName = "");
    static py::object getCursorItem(const std::string &botName = "");
    static py::dict getNetworkStats(const std::string &botName = "");
    static void openGameMenu(const std::string &botName = "");
    static void clickScreenPosition(const std::string &screenId, double x, double y, MouseButton button, const std::string &botName);
    static void typeText(const std::string &screenId, const std::string &text, const std::string &botName);
    static void pressKey(const std::string &screenId, int keyCode, int modifiers, const std::string &botName);

    // GLFW key codes for world.Key.*
    enum class Key : int {
        SPACE = 32, APOSTROPHE = 39, COMMA = 44, MINUS = 45, PERIOD = 46, SLASH = 47,
        NUM_0 = 48, NUM_1 = 49, NUM_2 = 50, NUM_3 = 51, NUM_4 = 52,
        NUM_5 = 53, NUM_6 = 54, NUM_7 = 55, NUM_8 = 56, NUM_9 = 57,
        SEMICOLON = 59, EQUAL = 61,
        A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72, I = 73,
        J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80, Q = 81, R = 82,
        S = 83, T = 84, U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,
        LEFT_BRACKET = 91, BACKSLASH = 92, RIGHT_BRACKET = 93, GRAVE_ACCENT = 96,
        ESCAPE = 256, ENTER = 257, TAB = 258, BACKSPACE = 259,
        INSERT = 260, DELETE = 261,
        RIGHT = 262, LEFT = 263, DOWN = 264, UP = 265,
        PAGE_UP = 266, PAGE_DOWN = 267, HOME = 268, END = 269,
        CAPS_LOCK = 280, SCROLL_LOCK = 281, NUM_LOCK = 282, PRINT_SCREEN = 283, PAUSE = 284,
        F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295,
        F7 = 296, F8 = 297, F9 = 298, F10 = 299, F11 = 300, F12 = 301,
        KP_0 = 320, KP_1 = 321, KP_2 = 322, KP_3 = 323, KP_4 = 324,
        KP_5 = 325, KP_6 = 326, KP_7 = 327, KP_8 = 328, KP_9 = 329,
        KP_DECIMAL = 330, KP_DIVIDE = 331, KP_MULTIPLY = 332,
        KP_SUBTRACT = 333, KP_ADD = 334, KP_ENTER = 335, KP_EQUAL = 336,
        LEFT_SHIFT = 340, LEFT_CONTROL = 341, LEFT_ALT = 342, LEFT_SUPER = 343,
        RIGHT_SHIFT = 344, RIGHT_CONTROL = 345, RIGHT_ALT = 346, RIGHT_SUPER = 347,
        MENU = 348
    };

    // GLFW modifier flags for world.KeyMod.*
    enum class KeyMod : int {
        SHIFT = 1, CONTROL = 2, ALT = 4, SUPER = 8, CAPS_LOCK = 16, NUM_LOCK = 32
    };
    static py::list listAllBots();

    static void sendChat(const std::string &message, const std::string &botName = "");
    static void sendCommand(const std::string &command, const std::string &botName = "");
    static void connectServer(const std::string &address, const std::string &botName = "");
    static void disconnectServer(const std::string &reason = "", const std::string &botName = "");

    static void startBot(const std::string &botName = "");
    static void stopBot(const std::string &reason = "", const std::string &botName = "");
    static void restartBot(const std::string &reason = "", const std::string &botName = "");
    static bool waitForOnline(double timeout = 60.0, const std::string &botName = "");

    static void baritoneGoto(double x, double y, double z, const std::string &bot = "");
    static void baritoneGoto(double x, double z, const std::string &bot = "");
    static void baritoneFollow(const std::string &player, const std::string &bot = "");
    static void baritoneCancel(const std::string &bot = "");
    static void baritoneMine(const std::string &blockType, const std::string &bot = "");
    static void baritoneFarm(const std::string &bot = "");
    static void baritoneCommand(const std::string &command, const std::string &bot = "");
    static void baritoneSetSetting(const std::string &setting, const py::object &value, const std::string &bot = "");
    static py::object baritoneGetSetting(const std::string &setting, const std::string &bot = "");
    static py::dict baritoneGetProcessStatus(const std::string &bot = "");

    static void meteorToggle(const std::string &module, const std::string &bot = "");
    static void meteorEnable(const std::string &module, const std::string &bot = "");
    static void meteorDisable(const std::string &module, const std::string &bot = "");
    static void meteorSetSetting(const std::string &module, const std::string &setting,
                                  const py::object &value, const std::string &bot = "");
    static py::object meteorGetSetting(const std::string &module, const std::string &setting,
                                       const std::string &bot = "");
    static py::dict meteorGetModule(const std::string &module, const std::string &bot = "");
    static py::list meteorListModules(const std::string &bot = "");

    // Entity queries
    static py::list getEntities(const std::string &bot = "");
    static py::list findEntitiesNear(double x, double y, double z, double radius,
                                     const std::string &typeFilter = "",
                                     const std::string &bot = "");

    // World queries
    static py::object getWeather(const std::string &bot = "");
    static py::object getBlock(double x, double y, double z, bool useDisk = false, const std::string &dimension = "", const std::string &bot = "");
    static py::object getLight(double x, double y, double z, bool useDisk = false, const std::string &dimension = "", const std::string &bot = "");
    static std::optional<PyBlockEntity> getBlockEntity(double x, double y, double z, bool useDisk = false, const std::string &dimension = "", const std::string &bot = "");
    static std::vector<PyBlockEntity> getBlockEntitiesInChunk(int chunkX, int chunkZ, bool useDisk = false, const std::string &dimension = "", const std::string &bot = "");

    static py::object blockEntityNbt(PyBlockEntity &be);
    static py::object blockEntityGetItem(PyBlockEntity &be, const std::string &key);
    static py::object blockEntityGet(PyBlockEntity &be, const std::string &key, py::object fallback);
    static bool blockEntityContains(const PyBlockEntity &be, const std::string &key);
    static py::list blockEntityKeys(const PyBlockEntity &be);
    static std::string blockEntityRepr(const PyBlockEntity &be);
    static py::object isBlockSolid(const std::string &blockState, BlockRegistry::Direction face = BlockRegistry::Direction::UP, const std::string &bot = "");
    static py::list findBlocks(const std::string &blockType, double centerX, double centerY, double centerZ,
                                int radius,
                                int minBlockLight = 0, int maxBlockLight = 15,
                                int minSkyLight = 0, int maxSkyLight = 15,
                                const std::string &bot = "");
    static py::object findNearestBlock(const py::list &blockTypes, int maxDistance, const std::string &bot = "");
    static int getLoadedChunkCount(const std::string &bot = "");
    static size_t getWorldMemoryUsage(const std::string &bot = "");
    static py::list getLoadedChunks(const std::string &bot = "");

    static PySectionChanges changedSections(const std::string &bot, const py::object &since,
                                            const std::string &dimension, bool digest, int limit,
                                            const py::bytes &digestPrefix);
    static std::optional<PySection> getSection(int chunkX, int chunkZ, int sectionY,
                                               const std::string &bot, const std::string &dimension,
                                               const py::bytes &digestPrefix);
    static py::bytes exportSections(const py::sequence &keys, const std::string &bot = "", const std::string &dimension = "");

    // World interaction
    static void holdAttack(bool enabled, int durationTicks = 0, const std::string &botName = "");
    static bool getHoldAttack(const std::string &botName = "");
    static void holdUse(bool enabled, int durationTicks = 0, const std::string &botName = "");
    static bool getHoldUse(const std::string &botName = "");
    static void lookAt(double x, double y, double z, BlockFace face = BlockFace::AUTO, bool sneak = false, const std::string &botName = "");
    static void lookAtEntity(int entityId, bool sneak = false, const std::string &botName = "");
    static bool canReachBlock(int x, int y, int z, bool sneak = false, BlockFace face = BlockFace::AUTO, double timeout = 3.0, const std::string &bot = "");
    static bool canReachBlockFrom(int fromX, int fromY, int fromZ, int x, int y, int z, bool sneak = false, BlockFace face = BlockFace::AUTO, double timeout = 3.0, const std::string &bot = "");
    static py::list canReachBlocks(const py::sequence &queries, bool sneak = false, BlockFace face = BlockFace::AUTO,
                                   double timeout = 5.0, const std::string &bot = "");
    static void interactBlock(double x, double y, double z, bool sneak = false, bool lookAtBlock = true, BlockFace face = BlockFace::AUTO, const std::string &bot = "");

    // Container interaction
    static void clickContainerSlot(int slotIndex, int button, ContainerClickType clickType, const std::string &bot = "", bool silent = false);
    static void closeContainer(const std::string &bot = "");
    static void openInventory(const std::string &bot = "");
    static void resyncInventory(const std::string &bot = "");
    static py::object getContainer(const std::string &bot = "");

    // Screen interaction
    static py::object getScreen(const std::string &botName = "");
    static void clickScreenWidget(const std::string &screenId, int widgetIndex, MouseButton button, const std::string &bot = "");

    // Recipe registry
    static py::object getRecipe(const std::string &recipeId, const std::string &bot = "");
    static py::list getRecipesFor(const std::string &itemId, const std::string &bot = "");
    static py::object getItemInfo(const std::string &itemId, const std::string &bot = "");
    static py::list getAllRecipes(const std::string &bot = "");
    static py::dict planRecursiveCraft(const std::string &itemId, int count, const std::string &bot = "");

    // Game window. Optional ints/bools default to None in the module registration.
    static py::object getWindow(const std::string &botName = "");
    static py::object setWindow(const py::object &x, const py::object &y, const py::object &width, const py::object &height,
                                const std::string &monitor, const py::object &minimized, const py::object &visible,
                                const std::string &botName);

    // Server info and tab list
    static py::object getServerInfo(const std::string &botName = "");
    static py::list getPlayerList(const std::string &botName = "");
    static py::object getServerStats(const std::string &botName = "");

    static void log(const std::string &message);
    static void error(const std::string &message);

    // Inter-script messaging (comms module)
    static int commsEmit(const std::string &topic, const py::object &data);
    static int commsSend(const std::string &botName, const py::object &data,
                         const std::string &topic = "");
    static void commsSubscribe(const std::string &topic = "");
    static void commsUnsubscribe(const std::string &topic);
    static py::object commsReceive(double timeout = -1.0);
    static int commsPending();

    // Set by ScriptThread so blocking API calls on this thread can notice a
    // stop request without returning to Python first.
    static void setCurrentStopFlag(std::atomic<bool> *flag);

    // Script lifecycle (manager module)
    static bool runScriptApi(const std::string &script, const std::string &botName = "");
    static void stopScriptApi(const std::string &script, const std::string &botName = "");
    static py::list listScriptsApi(const std::string &botName = "");

    // Custom instance-table columns (global scripting). interval is the
    // per-column recompute period in seconds (floored at 50ms).
    static void addColumn(const std::string &name, const py::function &provider, double interval);
    static void removeColumn(const std::string &name);
    // Decorator form: @manager.column("Name", interval=0.5)
    static py::object column(const std::string &name, double interval);

    // Console that log()/error() route to when no bot is current (global scripts).
    static void setGlobalConsole(BotConsoleWidget *console);
    static BotConsoleWidget* getGlobalConsole();
    // When enabled on the calling thread, log()/error() always target the global
    // console regardless of the current bot (used by the column compute worker).
    static void setForceGlobalConsole(bool enabled);
    static bool isForceGlobalConsole();

    static py::object qVariantToPyObject(const QVariant &value);

private:
    static QString resolveBotName(const std::string &botName);
    static struct BotInstance* ensureBotOnline(const QString &botName);
    // Like ensureBotOnline, but also requires the bot to advertise the capability (e.g. "meteor").
    static struct BotInstance* ensureBotCapability(const QString &botName, const char *capability);
    // Read-side counterpart: throws only when the bot is Online and lacks the capability. An offline
    // or unknown bot is returned as-is, since it has not reported any capabilities to check against.
    static struct BotInstance* botIfCapable(const QString &botName, const char *capability);
    static QVariant pyObjectToQVariant(const py::object &value);
    static QVariant pyObjectToPlainVariant(const py::object &value);
    // The calling script's bus scope: its bot name, or "_global".
    static QString currentScope();
    static py::dict rgbaColorToDict(const RGBAColor &color);
    static py::dict espBlockDataToDict(const ESPBlockData &data);

    static thread_local QString currentBot;
    static thread_local QString currentScript;
    static thread_local bool forceGlobalConsole;
    static thread_local std::atomic<bool> *currentStopFlag;
    static QPointer<BotConsoleWidget> globalConsole;
};

#endif // PYTHONAPI_H
