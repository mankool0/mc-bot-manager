# bot module

Bot state queries and basic commands.

## State Queries

### `position(bot_name="")`

Get bot position.

**Returns:** `dict` with keys `x`, `y`, `z`, or `None` if bot is offline

### `health(bot_name="")`

Get bot health.

**Returns:** `float` or `None` if bot is offline

### `hunger(bot_name="")`

Get bot hunger level.

**Returns:** `int` or `None` if bot is offline

### `saturation(bot_name="")`

Get bot food saturation.

**Returns:** `float` or `None` if bot is offline

### `air(bot_name="")`

Get bot air level.

**Returns:** `int` or `None` if bot is offline

### `experience_level(bot_name="")`

Get bot XP level.

**Returns:** `int` or `None` if bot is offline

### `experience_progress(bot_name="")`

Get bot XP progress to next level.

**Returns:** `float` or `None` if bot is offline

### `selected_slot(bot_name="")`

Get currently selected hotbar slot.

**Returns:** `int` or `None` if bot is offline

### `select_slot(slot, bot_name="")`

Select a hotbar slot.

**Parameters:**

- `slot` (`int`) - Slot to select (0–8)
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online, or if slot is out of range

```python
bot.select_slot(0)  # Select first hotbar slot
bot.select_slot(8)  # Select last hotbar slot
```

### `server(bot_name="")`

Get server address.

**Returns:** `str` or `None`

### `account(bot_name="")`

Get account username.

**Returns:** `str` or `None`

### `uptime(bot_name="")`

Get bot uptime in seconds.

**Returns:** `int` or `None`

### `dimension(bot_name="")`

Get current dimension.

**Returns:** `str` or `None` if bot is offline or dimension not available

### `is_online(bot_name="")`

Check if bot is online.

**Returns:** `bool`

### `status(bot_name="")`

Get bot status string.

**Returns:** `str` - One of: `"Offline"`, `"Starting"`, `"Online"`, `"Stopping"`, `"Error"`

**Raises:** `RuntimeError` if bot not found

### `inventory(bot_name="")`

Get bot inventory.

**Returns:** `list` of [item dicts](#item-dict), or `None` if bot is offline. Each dict also includes a `slot` field (0–40). Shulker boxes and other container items additionally have `container_items` (list of item dicts for the contents).

```python
items = bot.inventory()
if items is not None:
    for item in items:
        print(f"Slot {item['slot']}: {item['count']}x {item['item_id']} ({item['display_name']})")
```

### Item dict

The item dict schema used by `bot.inventory()`, `world.get_container()`, and entity item fields:

| Key | Type | Description |
|-----|------|-------------|
| `item_id` | `str` | Item ID (e.g. `"minecraft:diamond"`) |
| `count` | `int` | Stack size |
| `damage` | `int` | Current damage (0 = undamaged) |
| `max_damage` | `int` | Max durability (0 for non-damageable items) |
| `display_name` | `str` | Display name (may include formatting) |
| `enchantments` | `dict[str, int]` | Map of enchantment ID to level (e.g. `{"minecraft:sharpness": 3}`) |

### `screen(bot_name="")`

Get current screen class name.

**Returns:** `str` - Fully qualified class name of the current screen, or `None` if no screen is open (in-game)

```python
current_screen = bot.screen()

if current_screen is None:
    print("In-game, no GUI open")
elif "ChatScreen" in current_screen:
    print("Chat is open")
elif "InventoryScreen" in current_screen:
    print("Inventory is open")
elif "TitleScreen" in current_screen:
    print("At main menu")
else:
    print(f"Screen: {current_screen}")
```

**Note:** Works with all vanilla and modded screens. Screen class names follow the pattern:

- Vanilla: `net.minecraft.client.gui.screens.*`
- Modded: `com.modname.gui.*` or similar

### `network_stats(bot_name="")`

Get network statistics.

**Returns:** `dict` with network information

### `list_all()`

List all bots with their status.

**Returns:** `list[dict]` where each dict has:

- `name` (`str`) - Bot name
- `status` (`str`) - Bot status ("Offline", "Starting", "Online", "Stopping", "Error")

```python
bots = bot.list_all()
for b in bots:
    print(f"{b['name']}: {b['status']}")
```

## Bot Control

### `start(bot_name="")`

Start the bot.

**Parameters:**

- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found

```python
bot.start()  # Start current bot
bot.start("bot2")  # Start specific bot
```

### `stop(reason="", bot_name="")`

Stop the bot gracefully.

**Parameters:**

- `reason` (`str`, optional) - Reason for stopping (shown in logs)
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found

```python
bot.stop()  # Stop with default reason
bot.stop("Task completed")  # Stop with custom reason
```

### `restart(reason="", bot_name="")`

Restart the bot.

**Parameters:**

- `reason` (`str`, optional) - Reason for restarting (shown in logs)
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found

```python
bot.restart()  # Restart with default reason
bot.restart("Updating configuration")  # Restart with custom reason
```

## Commands

### `chat(message, bot_name="")`

Send chat message. Supports `/` commands and `#` for baritone.

**Parameters:**

- `message` (`str`) - Message to send
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
bot.chat("Hello world!")
bot.chat("/tp 0 64 0")  # Slash commands work
bot.chat("#goto 100 64 100")  # Baritone commands work
bot.chat("Hello from bot2!", "bot2")  # Send from specific bot
```

### `manager_command(command, bot_name="")`

Send raw manager protocol command (advanced).

**Parameters:**

- `command` (`str`) - Manager command to execute
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
# Advanced usage - prefer using bot.chat() or specific APIs
bot.manager_command("chat Hello")
bot.manager_command("baritone goto 100 64 100")
bot.manager_command("chat Hello", "bot2")  # Send to specific bot
```
