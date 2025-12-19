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
    m.def("screen", &PythonAPI::getScreen,
          "Get current screen class name (None if in-game)",
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

    // Path event type constants as a nested class
    auto pathEventType = m.def_submodule("PathEventType", "Path event types");
    pathEventType.attr("CALC_STARTED") = 0;
    pathEventType.attr("CALC_FINISHED_NOW_EXECUTING") = 1;
    pathEventType.attr("CALC_FAILED") = 2;
    pathEventType.attr("NEXT_SEGMENT_CALC_STARTED") = 3;
    pathEventType.attr("NEXT_SEGMENT_CALC_FINISHED") = 4;
    pathEventType.attr("CONTINUING_ONTO_PLANNED_NEXT") = 5;
    pathEventType.attr("SPLICING_ONTO_NEXT_EARLY") = 6;
    pathEventType.attr("AT_GOAL") = 7;
    pathEventType.attr("PATH_FINISHED_NEXT_STILL_CALCULATING") = 8;
    pathEventType.attr("NEXT_CALC_FAILED") = 9;
    pathEventType.attr("DISCARD_NEXT") = 10;
    pathEventType.attr("CANCELED") = 11;

    m.def("goto",
          static_cast<void(*)(double, double, double, const std::string&)>(&PythonAPI::baritoneGoto),
          "Navigate to X/Y/Z coordinates",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("bot") = "");
    m.def("goto",
          static_cast<void(*)(double, double, const std::string&)>(&PythonAPI::baritoneGoto),
          "Navigate to X/Z coordinates",
          py::arg("x"), py::arg("z"),
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
    m.def("get_process_status", &PythonAPI::baritoneGetProcessStatus,
          "Get current process status and pathfinding state",
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

PYBIND11_EMBEDDED_MODULE(world, m) {
    m.doc() = "World data queries and interaction";

    // World queries
    m.def("get_block", &PythonAPI::getBlock,
          "Get block state at position, returns block ID string or None if chunk not loaded",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("bot") = "");
    m.def("find_blocks", &PythonAPI::findBlocks,
          "Find all blocks of type within radius of center, returns list of (x,y,z) tuples",
          py::arg("block_type"), py::arg("center_x"), py::arg("center_y"), py::arg("center_z"),
          py::arg("radius"),
          py::arg("bot") = "");
    m.def("find_nearest", &PythonAPI::findNearestBlock,
          "Find nearest block matching any type in list, returns (x,y,z) tuple or None",
          py::arg("block_types"), py::arg("max_distance") = 128,
          py::arg("bot") = "");
    m.def("loaded_chunk_count", &PythonAPI::getLoadedChunkCount,
          "Get number of loaded chunks",
          py::arg("bot") = "");
    m.def("memory_usage", &PythonAPI::getWorldMemoryUsage,
          "Get world data memory usage in bytes",
          py::arg("bot") = "");
    m.def("loaded_chunks", &PythonAPI::getLoadedChunks,
          "Get list of loaded chunk positions as (x,z) tuples",
          py::arg("bot") = "");

    // World interaction
    m.def("interact_block", &PythonAPI::interactBlock,
          "Right-click/interact with block at position",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("sneak") = false,
          py::arg("look_at_block") = true,
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
