#include "PythonAPI.h"
#include "inventory.qpb.h"

#undef slots
#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;

PYBIND11_EMBEDDED_MODULE(bot, m) {
    m.doc() = "Bot state and control";

    py::class_<PyGuiWidget>(m, "GuiWidget")
        .def_readonly("index", &PyGuiWidget::index)
        .def_readonly("type", &PyGuiWidget::widgetType)
        .def_readonly("class_name", &PyGuiWidget::className)
        .def_readonly("x", &PyGuiWidget::x)
        .def_readonly("y", &PyGuiWidget::y)
        .def_readonly("width", &PyGuiWidget::width)
        .def_readonly("height", &PyGuiWidget::height)
        .def_readonly("active", &PyGuiWidget::active)
        .def_readonly("visible", &PyGuiWidget::visible)
        .def_readonly("text", &PyGuiWidget::text)
        .def_readonly("edit_value", &PyGuiWidget::editValue)
        .def_readonly("edit_editable", &PyGuiWidget::editEditable)
        .def_readonly("selected", &PyGuiWidget::selected);

    py::class_<PyGuiSlot>(m, "GuiSlot")
        .def_readonly("index", &PyGuiSlot::index)
        .def_readonly("x", &PyGuiSlot::x)
        .def_readonly("y", &PyGuiSlot::y)
        .def_readonly("active", &PyGuiSlot::active)
        .def_readonly("item_id", &PyGuiSlot::itemId)
        .def_readonly("count", &PyGuiSlot::count)
        .def_readonly("display_name", &PyGuiSlot::displayName)
        .def_readonly("damage", &PyGuiSlot::damage)
        .def_readonly("max_damage", &PyGuiSlot::maxDamage)
        .def_readonly("enchantments", &PyGuiSlot::enchantments)
        .def_readonly("repair_cost", &PyGuiSlot::repairCost);

    py::class_<PyScreenState>(m, "ScreenState")
        .def_readonly("id", &PyScreenState::screenId)
        .def_readonly("screen_class", &PyScreenState::screenClass)
        .def_readonly("title", &PyScreenState::title)
        .def_readonly("width", &PyScreenState::width)
        .def_readonly("height", &PyScreenState::height)
        .def_readonly("widgets", &PyScreenState::widgets)
        .def_readonly("slots", &PyScreenState::guiSlots);

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
    m.def("select_slot", &PythonAPI::selectSlot,
          "Select hotbar slot (0-8)",
          py::arg("slot"),
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
    m.def("proxy", &PythonAPI::getProxy,
          "Get proxy info",
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
    m.def("get_cursor_item", &PythonAPI::getCursorItem,
          "Get item currently held on the cursor (slot=-1, item_id='minecraft:air' if empty)",
          py::arg("bot_name") = "");
    m.def("get_screen", &PythonAPI::getScreen,
          "Get current screen state (None if in-game with no GUI open)",
          py::arg("bot_name") = "");
    m.def("open_game_menu", &PythonAPI::openGameMenu,
          "Open the game/pause menu (equivalent to pressing ESC in-game). Raises if bot is not online or a screen is already open.",
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

    py::enum_<BlockRegistry::Direction>(m, "Direction")
        .value("DOWN",  BlockRegistry::Direction::DOWN)
        .value("UP",    BlockRegistry::Direction::UP)
        .value("NORTH", BlockRegistry::Direction::NORTH)
        .value("SOUTH", BlockRegistry::Direction::SOUTH)
        .value("WEST",  BlockRegistry::Direction::WEST)
        .value("EAST",  BlockRegistry::Direction::EAST)
        .export_values();

    // World queries
    m.def("get_weather", &PythonAPI::getWeather,
          "Get current weather state as dict with is_raining, is_thundering, rain_level, thunder_level. Returns None if bot offline.",
          py::arg("bot") = "");
    m.def("get_block", &PythonAPI::getBlock,
          "Get block state at position. Returns block ID string or None if not found. "
          "If use_disk=True, reads saved world data when chunk not loaded. "
          "dimension requires use_disk=True.",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("use_disk") = false,
          py::arg("dimension") = "",
          py::arg("bot") = "");
    m.def("get_light", &PythonAPI::getLight,
          "Get light levels at position as dict with block (0-15) and sky (0-15). Returns None if not found. "
          "If use_disk=True, reads saved world data when chunk not loaded. "
          "dimension requires use_disk=True.",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("use_disk") = false,
          py::arg("dimension") = "",
          py::arg("bot") = "");
    m.def("get_block_entity", &PythonAPI::getBlockEntity,
          "Get block entity at position. Returns dict {type, x, y, z, items?} or None. "
          "If use_disk=True, falls back to saved world when not in memory. "
          "dimension requires use_disk=True.",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("use_disk") = false,
          py::arg("dimension") = "",
          py::arg("bot") = "");
    m.def("get_block_entities_in_chunk", &PythonAPI::getBlockEntitiesInChunk,
          "Get all block entities in a chunk as list of dicts {type, x, y, z, items?}. "
          "If use_disk=True and chunk not loaded, reads from saved world (no items in result).",
          py::arg("chunk_x"), py::arg("chunk_z"),
          py::arg("use_disk") = false,
          py::arg("dimension") = "",
          py::arg("bot") = "");
    m.def("is_solid", &PythonAPI::isBlockSolid,
          "Check if a block state string has a solid face in the given direction. "
          "Returns True/False, or None if block registry not loaded.",
          py::arg("block_state"),
          py::arg("face") = BlockRegistry::Direction::UP,
          py::arg("bot") = "");
    m.def("find_blocks", &PythonAPI::findBlocks,
          "Find all blocks of type within radius of center, returns list of (x,y,z) tuples. "
          "Optionally filter by block/sky light range (0-15).",
          py::arg("block_type"), py::arg("center_x"), py::arg("center_y"), py::arg("center_z"),
          py::arg("radius"),
          py::arg("min_block_light") = 0, py::arg("max_block_light") = 15,
          py::arg("min_sky_light") = 0, py::arg("max_sky_light") = 15,
          py::arg("bot") = "");
    m.def("entities", &PythonAPI::getEntities,
          "Get all tracked entities as list of dicts",
          py::arg("bot") = "");
    m.def("find_entities_near", &PythonAPI::findEntitiesNear,
          "Find entities within radius of (x,y,z), optional type prefix filter, returns list of entity dicts",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("radius"),
          py::arg("type") = "",
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
    m.def("hold_attack", &PythonAPI::holdAttack,
          "Hold or release left-click attack in-game.",
          py::arg("enabled"),
          py::arg("duration_ticks") = 0,
          py::arg("bot_name") = "");
    m.def("get_hold_attack", &PythonAPI::getHoldAttack,
          "Query the current hold-attack state from the client. Returns True if attack is "
          "currently being held, False otherwise. Blocks until the client responds.",
          py::arg("bot_name") = "");

    py::enum_<PythonAPI::BlockFace>(m, "BlockFace")
        .value("AUTO",  PythonAPI::BlockFace::AUTO)
        .value("DOWN",  PythonAPI::BlockFace::DOWN)
        .value("UP",    PythonAPI::BlockFace::UP)
        .value("NORTH", PythonAPI::BlockFace::NORTH)
        .value("SOUTH", PythonAPI::BlockFace::SOUTH)
        .value("WEST",  PythonAPI::BlockFace::WEST)
        .value("EAST",  PythonAPI::BlockFace::EAST)
        .export_values();

    m.def("look_at", &PythonAPI::lookAt,
          "Look at a block at (x, y, z). face selects which face to target; AUTO finds the best visible face. "
          "sneak=False tries standing first, falls back to crouching eye height if nothing is visible.",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("face") = PythonAPI::BlockFace::AUTO,
          py::arg("sneak") = false,
          py::arg("bot_name") = "");
    m.def("can_reach_block", &PythonAPI::canReachBlock,
          "Check if a block is reachable from the bot's current position. "
          "face=AUTO checks any face; pass a specific face to check only that face.",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("sneak") = false,
          py::arg("face") = PythonAPI::BlockFace::AUTO,
          py::arg("bot") = "");
    m.def("can_reach_block_from", &PythonAPI::canReachBlockFrom,
          "Check if a block (x,y,z) is reachable when standing at (from_x,from_y,from_z). "
          "face=AUTO checks any face; pass a specific face to check only that face.",
          py::arg("from_x"), py::arg("from_y"), py::arg("from_z"),
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("sneak") = false,
          py::arg("face") = PythonAPI::BlockFace::AUTO,
          py::arg("bot") = "");
    m.def("interact_block", &PythonAPI::interactBlock,
          "Right-click/interact with block at position",
          py::arg("x"), py::arg("y"), py::arg("z"),
          py::arg("sneak") = false,
          py::arg("look_at_block") = true,
          py::arg("face") = PythonAPI::BlockFace::AUTO,
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
          py::arg("button") = 0,
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
    m.def("click_widget", &PythonAPI::clickScreenWidget,
          "Click a widget on the current screen by index from bot.get_screen(). Raises if screen_id doesn't match.",
          py::arg("screen_id"),
          py::arg("widget_index"),
          py::arg("button") = PythonAPI::MouseButton::LEFT,
          py::arg("bot") = "");
    m.def("click_screen", &PythonAPI::clickScreenPosition,
          "Click at specific pixel coordinates on the current screen. Raises if screen_id doesn't match.",
          py::arg("screen_id"),
          py::arg("x"),
          py::arg("y"),
          py::arg("button") = PythonAPI::MouseButton::LEFT,
          py::arg("bot") = "");
    m.def("type_text", &PythonAPI::typeText,
          "Type text into the current screen (sends charTyped events). Raises if screen_id doesn't match.",
          py::arg("screen_id"),
          py::arg("text"),
          py::arg("bot") = "");
    m.def("press_key", &PythonAPI::pressKey,
          "Press a key on the current screen. Use world.Key.* for key codes and world.KeyMod.* for modifiers. Raises if screen_id doesn't match.",
          py::arg("screen_id"),
          py::arg("key_code"),
          py::arg("modifiers") = 0,
          py::arg("bot") = "");

    py::enum_<PythonAPI::Key>(m, "Key")
        .value("SPACE", PythonAPI::Key::SPACE)
        .value("APOSTROPHE", PythonAPI::Key::APOSTROPHE)
        .value("COMMA", PythonAPI::Key::COMMA)
        .value("MINUS", PythonAPI::Key::MINUS)
        .value("PERIOD", PythonAPI::Key::PERIOD)
        .value("SLASH", PythonAPI::Key::SLASH)
        .value("NUM_0", PythonAPI::Key::NUM_0).value("NUM_1", PythonAPI::Key::NUM_1)
        .value("NUM_2", PythonAPI::Key::NUM_2).value("NUM_3", PythonAPI::Key::NUM_3)
        .value("NUM_4", PythonAPI::Key::NUM_4).value("NUM_5", PythonAPI::Key::NUM_5)
        .value("NUM_6", PythonAPI::Key::NUM_6).value("NUM_7", PythonAPI::Key::NUM_7)
        .value("NUM_8", PythonAPI::Key::NUM_8).value("NUM_9", PythonAPI::Key::NUM_9)
        .value("SEMICOLON", PythonAPI::Key::SEMICOLON).value("EQUAL", PythonAPI::Key::EQUAL)
        .value("A", PythonAPI::Key::A).value("B", PythonAPI::Key::B).value("C", PythonAPI::Key::C)
        .value("D", PythonAPI::Key::D).value("E", PythonAPI::Key::E).value("F", PythonAPI::Key::F)
        .value("G", PythonAPI::Key::G).value("H", PythonAPI::Key::H).value("I", PythonAPI::Key::I)
        .value("J", PythonAPI::Key::J).value("K", PythonAPI::Key::K).value("L", PythonAPI::Key::L)
        .value("M", PythonAPI::Key::M).value("N", PythonAPI::Key::N).value("O", PythonAPI::Key::O)
        .value("P", PythonAPI::Key::P).value("Q", PythonAPI::Key::Q).value("R", PythonAPI::Key::R)
        .value("S", PythonAPI::Key::S).value("T", PythonAPI::Key::T).value("U", PythonAPI::Key::U)
        .value("V", PythonAPI::Key::V).value("W", PythonAPI::Key::W).value("X", PythonAPI::Key::X)
        .value("Y", PythonAPI::Key::Y).value("Z", PythonAPI::Key::Z)
        .value("LEFT_BRACKET", PythonAPI::Key::LEFT_BRACKET)
        .value("BACKSLASH", PythonAPI::Key::BACKSLASH)
        .value("RIGHT_BRACKET", PythonAPI::Key::RIGHT_BRACKET)
        .value("GRAVE_ACCENT", PythonAPI::Key::GRAVE_ACCENT)
        .value("ESCAPE", PythonAPI::Key::ESCAPE).value("ENTER", PythonAPI::Key::ENTER)
        .value("TAB", PythonAPI::Key::TAB).value("BACKSPACE", PythonAPI::Key::BACKSPACE)
        .value("INSERT", PythonAPI::Key::INSERT).value("DELETE", PythonAPI::Key::DELETE)
        .value("RIGHT", PythonAPI::Key::RIGHT).value("LEFT", PythonAPI::Key::LEFT)
        .value("DOWN", PythonAPI::Key::DOWN).value("UP", PythonAPI::Key::UP)
        .value("PAGE_UP", PythonAPI::Key::PAGE_UP).value("PAGE_DOWN", PythonAPI::Key::PAGE_DOWN)
        .value("HOME", PythonAPI::Key::HOME).value("END", PythonAPI::Key::END)
        .value("CAPS_LOCK", PythonAPI::Key::CAPS_LOCK)
        .value("SCROLL_LOCK", PythonAPI::Key::SCROLL_LOCK)
        .value("NUM_LOCK", PythonAPI::Key::NUM_LOCK)
        .value("PRINT_SCREEN", PythonAPI::Key::PRINT_SCREEN)
        .value("PAUSE", PythonAPI::Key::PAUSE)
        .value("F1", PythonAPI::Key::F1).value("F2", PythonAPI::Key::F2)
        .value("F3", PythonAPI::Key::F3).value("F4", PythonAPI::Key::F4)
        .value("F5", PythonAPI::Key::F5).value("F6", PythonAPI::Key::F6)
        .value("F7", PythonAPI::Key::F7).value("F8", PythonAPI::Key::F8)
        .value("F9", PythonAPI::Key::F9).value("F10", PythonAPI::Key::F10)
        .value("F11", PythonAPI::Key::F11).value("F12", PythonAPI::Key::F12)
        .value("KP_0", PythonAPI::Key::KP_0).value("KP_1", PythonAPI::Key::KP_1)
        .value("KP_2", PythonAPI::Key::KP_2).value("KP_3", PythonAPI::Key::KP_3)
        .value("KP_4", PythonAPI::Key::KP_4).value("KP_5", PythonAPI::Key::KP_5)
        .value("KP_6", PythonAPI::Key::KP_6).value("KP_7", PythonAPI::Key::KP_7)
        .value("KP_8", PythonAPI::Key::KP_8).value("KP_9", PythonAPI::Key::KP_9)
        .value("KP_DECIMAL", PythonAPI::Key::KP_DECIMAL)
        .value("KP_DIVIDE", PythonAPI::Key::KP_DIVIDE)
        .value("KP_MULTIPLY", PythonAPI::Key::KP_MULTIPLY)
        .value("KP_SUBTRACT", PythonAPI::Key::KP_SUBTRACT)
        .value("KP_ADD", PythonAPI::Key::KP_ADD)
        .value("KP_ENTER", PythonAPI::Key::KP_ENTER)
        .value("KP_EQUAL", PythonAPI::Key::KP_EQUAL)
        .value("LEFT_SHIFT", PythonAPI::Key::LEFT_SHIFT)
        .value("LEFT_CONTROL", PythonAPI::Key::LEFT_CONTROL)
        .value("LEFT_ALT", PythonAPI::Key::LEFT_ALT)
        .value("LEFT_SUPER", PythonAPI::Key::LEFT_SUPER)
        .value("RIGHT_SHIFT", PythonAPI::Key::RIGHT_SHIFT)
        .value("RIGHT_CONTROL", PythonAPI::Key::RIGHT_CONTROL)
        .value("RIGHT_ALT", PythonAPI::Key::RIGHT_ALT)
        .value("RIGHT_SUPER", PythonAPI::Key::RIGHT_SUPER)
        .value("MENU", PythonAPI::Key::MENU)
        .export_values();

    py::enum_<PythonAPI::KeyMod>(m, "KeyMod")
        .value("SHIFT", PythonAPI::KeyMod::SHIFT)
        .value("CONTROL", PythonAPI::KeyMod::CONTROL)
        .value("ALT", PythonAPI::KeyMod::ALT)
        .value("SUPER", PythonAPI::KeyMod::SUPER)
        .value("CAPS_LOCK", PythonAPI::KeyMod::CAPS_LOCK)
        .value("NUM_LOCK", PythonAPI::KeyMod::NUM_LOCK)
        .export_values();

    // Recipe registry
    m.def("get_recipe", &PythonAPI::getRecipe,
          "Get recipe data by exact recipe ID (e.g., 'minecraft:gold_ingot_from_gold_block'). Returns None if not found.",
          py::arg("recipe_id"),
          py::arg("bot") = "");
    m.def("get_recipes_for", &PythonAPI::getRecipesFor,
          "Get all recipes that produce a given item ID (e.g., 'minecraft:gold_ingot'). Returns a list (may be empty).",
          py::arg("item_id"),
          py::arg("bot") = "");
    m.def("get_item_info", &PythonAPI::getItemInfo,
          "Get item info (max_stack_size, max_damage) by ID",
          py::arg("item_id"),
          py::arg("bot") = "");
    m.def("get_all_recipes", &PythonAPI::getAllRecipes,
          "Get list of all available recipe IDs",
          py::arg("bot") = "");
    m.def("plan_recursive_craft", &PythonAPI::planRecursiveCraft,
          "Plan recursive crafting from current inventory, returns list of (item_id, count) tuples",
          py::arg("item_id"),
          py::arg("count") = 1,
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

PYBIND11_EMBEDDED_MODULE(server, m) {
    m.doc() = "Server info and tab list";

    py::class_<PyServerInfo>(m, "ServerInfo")
        .def_readonly("address", &PyServerInfo::address)
        .def_readonly("motd", &PyServerInfo::motd)
        .def_readonly("ping", &PyServerInfo::ping)
        .def_readonly("version", &PyServerInfo::version)
        .def_readonly("players_online", &PyServerInfo::players_online)
        .def_readonly("players_max", &PyServerInfo::players_max);

    py::enum_<Gamemode>(m, "Gamemode")
        .value("SURVIVAL", Gamemode::SURVIVAL)
        .value("CREATIVE", Gamemode::CREATIVE)
        .value("ADVENTURE", Gamemode::ADVENTURE)
        .value("SPECTATOR", Gamemode::SPECTATOR);

    py::class_<PyTabListPlayer>(m, "TabListPlayer")
        .def_readonly("name", &PyTabListPlayer::name)
        .def_readonly("uuid", &PyTabListPlayer::uuid)
        .def_readonly("ping", &PyTabListPlayer::ping)
        .def_readonly("gamemode", &PyTabListPlayer::gamemode)
        .def_readonly("display_name", &PyTabListPlayer::display_name);

    m.def("get_info", &PythonAPI::getServerInfo,
          "Get server metadata. Returns ServerInfo or None if bot is offline.",
          py::arg("bot_name") = "");
    m.def("get_player_list", &PythonAPI::getPlayerList,
          "Get full tab list. Returns list[TabListPlayer], empty if offline or not yet received.",
          py::arg("bot_name") = "");
}
