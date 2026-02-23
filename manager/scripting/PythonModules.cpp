#include "PythonAPI.h"
#include "inventory.qpb.h"

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

    py::enum_<PythonAPI::PathEventType>(m, "PathEventType")
        .value("CALC_STARTED", PythonAPI::PathEventType::CALC_STARTED)
        .value("CALC_FINISHED_NOW_EXECUTING", PythonAPI::PathEventType::CALC_FINISHED_NOW_EXECUTING)
        .value("CALC_FAILED", PythonAPI::PathEventType::CALC_FAILED)
        .value("NEXT_SEGMENT_CALC_STARTED", PythonAPI::PathEventType::NEXT_SEGMENT_CALC_STARTED)
        .value("NEXT_SEGMENT_CALC_FINISHED", PythonAPI::PathEventType::NEXT_SEGMENT_CALC_FINISHED)
        .value("CONTINUING_ONTO_PLANNED_NEXT", PythonAPI::PathEventType::CONTINUING_ONTO_PLANNED_NEXT)
        .value("SPLICING_ONTO_NEXT_EARLY", PythonAPI::PathEventType::SPLICING_ONTO_NEXT_EARLY)
        .value("AT_GOAL", PythonAPI::PathEventType::AT_GOAL)
        .value("PATH_FINISHED_NEXT_STILL_CALCULATING", PythonAPI::PathEventType::PATH_FINISHED_NEXT_STILL_CALCULATING)
        .value("NEXT_CALC_FAILED", PythonAPI::PathEventType::NEXT_CALC_FAILED)
        .value("DISCARD_NEXT", PythonAPI::PathEventType::DISCARD_NEXT)
        .value("CANCELED", PythonAPI::PathEventType::CANCELED)
        .export_values();

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

    // Container interaction enums
    py::enum_<PythonAPI::MouseButton>(m, "MouseButton")
        .value("LEFT", PythonAPI::MouseButton::LEFT)
        .value("RIGHT", PythonAPI::MouseButton::RIGHT)
        .value("MIDDLE", PythonAPI::MouseButton::MIDDLE)
        .export_values();

    py::enum_<PythonAPI::ContainerClickType>(m, "ClickType")
        .value("PICKUP", PythonAPI::ContainerClickType::PICKUP)
        .value("QUICK_MOVE", PythonAPI::ContainerClickType::QUICK_MOVE)
        .value("SWAP", PythonAPI::ContainerClickType::SWAP)
        .value("CLONE", PythonAPI::ContainerClickType::CLONE)
        .value("THROW", PythonAPI::ContainerClickType::THROW)
        .value("QUICK_CRAFT", PythonAPI::ContainerClickType::QUICK_CRAFT)
        .value("PICKUP_ALL", PythonAPI::ContainerClickType::PICKUP_ALL)
        .export_values();

    // Container type enum
    py::enum_<mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType>(m, "ContainerType")
        .value("PLAYER_INVENTORY", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::PLAYER_INVENTORY)
        .value("CHEST", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::CHEST)
        .value("ENDER_CHEST", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::ENDER_CHEST)
        .value("SHULKER_BOX", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::SHULKER_BOX)
        .value("FURNACE", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::FURNACE)
        .value("BLAST_FURNACE", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::BLAST_FURNACE)
        .value("SMOKER", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::SMOKER)
        .value("CRAFTING_TABLE", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::CRAFTING_TABLE)
        .value("ENCHANTING_TABLE", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::ENCHANTING_TABLE)
        .value("ANVIL", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::ANVIL)
        .value("BREWING_STAND", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::BREWING_STAND)
        .value("VILLAGER_TRADE", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::VILLAGER_TRADE)
        .value("HORSE_INVENTORY", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::HORSE_INVENTORY)
        .value("HOPPER", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::HOPPER)
        .value("DISPENSER", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::DISPENSER)
        .value("DROPPER", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::DROPPER)
        .value("BEACON", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::BEACON)
        .value("OTHER", mankool::mcbot::protocol::ContainerUpdate_QtProtobufNested::ContainerType::OTHER)
        .export_values();

    // Container interaction functions
    m.def("click_slot", &PythonAPI::clickContainerSlot,
          "Click a container slot",
          py::arg("slot_index"),
          py::arg("button") = PythonAPI::MouseButton::LEFT,
          py::arg("click_type") = PythonAPI::ContainerClickType::PICKUP,
          py::arg("bot") = "");
    m.def("close_container", &PythonAPI::closeContainer,
          "Close currently open container",
          py::arg("bot") = "");
    m.def("open_inventory", &PythonAPI::openInventory,
          "Open player inventory",
          py::arg("bot") = "");
    m.def("get_container", &PythonAPI::getContainer,
          "Get current open container info (None if no container open)",
          py::arg("bot") = "");

    // Recipe registry
    m.def("get_recipe", &PythonAPI::getRecipe,
          "Get recipe data by ID (e.g., 'minecraft:diamond_pickaxe')",
          py::arg("recipe_id"),
          py::arg("bot") = "");
    m.def("get_item_info", &PythonAPI::getItemInfo,
          "Get item info (max_stack_size, max_damage) by ID",
          py::arg("item_id"),
          py::arg("bot") = "");
    m.def("get_all_recipes", &PythonAPI::getAllRecipes,
          "Get list of all available recipe IDs",
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
