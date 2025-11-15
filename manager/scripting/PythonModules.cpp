#include "PythonAPI.h"

#undef slots
#include <pybind11/embed.h>

namespace py = pybind11;

PYBIND11_EMBEDDED_MODULE(bot, m) {
    m.doc() = "Bot state and control";

    m.def("position", &PythonAPI::getPosition,
          "Get position as dict {x, y, z}",
          py::arg("bot_name") = "");
    m.def("dimension", &PythonAPI::getDimension,
          "Get dimension name",
          py::arg("bot_name") = "");
    m.def("health", &PythonAPI::getHealth,
          "Get health",
          py::arg("bot_name") = "");
    m.def("hunger", &PythonAPI::getHunger,
          "Get hunger level",
          py::arg("bot_name") = "");
    m.def("saturation", &PythonAPI::getSaturation,
          "Get food saturation",
          py::arg("bot_name") = "");
    m.def("air", &PythonAPI::getAir,
          "Get air level",
          py::arg("bot_name") = "");
    m.def("experience_level", &PythonAPI::getExperienceLevel,
          "Get XP level",
          py::arg("bot_name") = "");
    m.def("experience_progress", &PythonAPI::getExperienceProgress,
          "Get XP progress to next level",
          py::arg("bot_name") = "");
    m.def("selected_slot", &PythonAPI::getSelectedSlot,
          "Get selected hotbar slot",
          py::arg("bot_name") = "");
    m.def("server", &PythonAPI::getServer,
          "Get server address",
          py::arg("bot_name") = "");
    m.def("account", &PythonAPI::getAccount,
          "Get account username",
          py::arg("bot_name") = "");
    m.def("uptime", &PythonAPI::getUptime,
          "Get bot uptime in seconds",
          py::arg("bot_name") = "");
    m.def("is_online", &PythonAPI::isOnline,
          "Check if bot is online",
          py::arg("bot_name") = "");
    m.def("status", &PythonAPI::getStatus,
          "Get status: 'Offline', 'Starting', 'Online', 'Error'",
          py::arg("bot_name") = "");
    m.def("inventory", &PythonAPI::getInventory,
          "Get inventory as list of items",
          py::arg("bot_name") = "");
    m.def("network_stats", &PythonAPI::getNetworkStats,
          "Get network stats dict",
          py::arg("bot_name") = "");
    m.def("list_all", &PythonAPI::listAllBots,
          "List all bot names");

    m.def("chat", &PythonAPI::sendChat,
          "Send chat message",
          py::arg("message"),
          py::arg("bot_name") = "");
    m.def("manager_command", &PythonAPI::sendCommand,
          "Send raw manager command",
          py::arg("command"),
          py::arg("bot_name") = "");

    m.def("start", &PythonAPI::startBot,
          "Start bot",
          py::arg("bot_name") = "");
    m.def("stop", &PythonAPI::stopBot,
          "Stop bot",
          py::arg("reason") = "",
          py::arg("bot_name") = "");
    m.def("restart", &PythonAPI::restartBot,
          "Restart bot",
          py::arg("reason") = "",
          py::arg("bot_name") = "");
}

PYBIND11_EMBEDDED_MODULE(baritone, m) {
    m.doc() = "Baritone";

    m.def("goto", &PythonAPI::baritoneGoto,
          "Navigate to coordinates",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("bot") = "");
    m.def("follow", &PythonAPI::baritoneFollow,
          "Follow player",
          py::arg("player"),
          py::arg("bot") = "");
    m.def("cancel", &PythonAPI::baritoneCancel,
          "Cancel current task",
          py::arg("bot") = "");
    m.def("mine", &PythonAPI::baritoneMine,
          "Mine block type",
          py::arg("block_type"),
          py::arg("bot") = "");
    m.def("farm", &PythonAPI::baritoneFarm,
          "Start farming",
          py::arg("bot") = "");
    m.def("command", &PythonAPI::baritoneCommand,
          "Send raw baritone command",
          py::arg("command"),
          py::arg("bot") = "");
    m.def("set_setting", &PythonAPI::baritoneSetSetting,
          "Set setting value",
          py::arg("setting"), py::arg("value"),
          py::arg("bot") = "");
    m.def("get_setting", &PythonAPI::baritoneGetSetting,
          "Get setting value",
          py::arg("setting"),
          py::arg("bot") = "");
}

PYBIND11_EMBEDDED_MODULE(meteor, m) {
    m.doc() = "Meteor client";

    m.def("toggle", &PythonAPI::meteorToggle,
          "Toggle module",
          py::arg("module"),
          py::arg("bot") = "");
    m.def("enable", &PythonAPI::meteorEnable,
          "Enable module",
          py::arg("module"),
          py::arg("bot") = "");
    m.def("disable", &PythonAPI::meteorDisable,
          "Disable module",
          py::arg("module"),
          py::arg("bot") = "");
    m.def("set_setting", &PythonAPI::meteorSetSetting,
          "Set module setting",
          py::arg("module"), py::arg("setting"), py::arg("value"),
          py::arg("bot") = "");
    m.def("get_setting", &PythonAPI::meteorGetSetting,
          "Get module setting",
          py::arg("module"), py::arg("setting"),
          py::arg("bot") = "");
    m.def("get_module", &PythonAPI::meteorGetModule,
          "Get module info dict",
          py::arg("module"),
          py::arg("bot") = "");
    m.def("list_modules", &PythonAPI::meteorListModules,
          "List all module names",
          py::arg("bot") = "");
}

PYBIND11_EMBEDDED_MODULE(utils, m) {
    m.doc() = "Utility functions";

    m.def("log", &PythonAPI::log,
          "Log message to console",
          py::arg("message"));
    m.def("error", &PythonAPI::error,
          "Log error to console",
          py::arg("message"));
}
