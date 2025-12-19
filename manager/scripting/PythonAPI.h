#ifndef PYTHONAPI_H
#define PYTHONAPI_H

#include <QString>
#include <QVariant>

#undef slots
#include <pybind11/pybind11.h>
#define slots Q_SLOTS

namespace py = pybind11;

struct RGBAColor;
struct ESPBlockData;

class PythonAPI
{
public:
    static void setCurrentBot(const QString &botName);
    static QString getCurrentBot();
    static void setCurrentScript(const QString &scriptName);
    static QString getCurrentScript();

    static py::object getPosition(const std::string &botName = "");
    static py::object getDimension(const std::string &botName = "");
    static py::object getHealth(const std::string &botName = "");
    static py::object getHunger(const std::string &botName = "");
    static py::object getSaturation(const std::string &botName = "");
    static py::object getAir(const std::string &botName = "");
    static py::object getExperienceLevel(const std::string &botName = "");
    static py::object getExperienceProgress(const std::string &botName = "");
    static py::object getSelectedSlot(const std::string &botName = "");
    static py::object getServer(const std::string &botName = "");
    static py::object getAccount(const std::string &botName = "");
    static py::object getUptime(const std::string &botName = "");
    static bool isOnline(const std::string &botName = "");
    static std::string getStatus(const std::string &botName = "");
    static py::object getInventory(const std::string &botName = "");
    static py::dict getNetworkStats(const std::string &botName = "");
    static py::object getScreen(const std::string &botName = "");
    static py::list listAllBots();

    static void sendChat(const std::string &message, const std::string &botName = "");
    static void sendCommand(const std::string &command, const std::string &botName = "");

    static void startBot(const std::string &botName = "");
    static void stopBot(const std::string &reason = "", const std::string &botName = "");
    static void restartBot(const std::string &reason = "", const std::string &botName = "");

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

    // World queries
    static py::object getBlock(int x, int y, int z, const std::string &bot = "");
    static py::list findBlocks(const std::string &blockType, double centerX, double centerY, double centerZ,
                                int radius, const std::string &bot = "");
    static py::object findNearestBlock(const py::list &blockTypes, int maxDistance, const std::string &bot = "");
    static int getLoadedChunkCount(const std::string &bot = "");
    static size_t getWorldMemoryUsage(const std::string &bot = "");
    static py::list getLoadedChunks(const std::string &bot = "");

    // World interaction
    static void interactBlock(int x, int y, int z, bool sneak = false, bool lookAtBlock = true, const std::string &bot = "");

    static void log(const std::string &message);
    static void error(const std::string &message);

    static py::object qVariantToPyObject(const QVariant &value);

private:
    static QString resolveBotName(const std::string &botName);
    static struct BotInstance* ensureBotOnline(const QString &botName);
    static QVariant pyObjectToQVariant(const py::object &value);
    static py::dict rgbaColorToDict(const RGBAColor &color);
    static py::dict espBlockDataToDict(const ESPBlockData &data);

    static thread_local QString currentBot;
    static thread_local QString currentScript;
};

#endif // PYTHONAPI_H
