# Available Events

Events that can be handled with the `@on` decorator.

## Usage

```python
@on("event_name")
def handler(param1, param2):
    # Handle event
    pass
```

## Event List

### `chat_message`

Fired when a chat message is received.

**Parameters:**

- `chat_data` (`dict`) - Dictionary containing chat message details with the following keys:
  - `sender` (`str`) - Player who sent the message or "SYSTEM" for system messages
  - `content` (`str`) - Message content
  - `type` (`str`) - Message type: `"PLAYER_CHAT"` or `"SYSTEM_MESSAGE"`
  - `timestamp` (`int`) - Unix timestamp in milliseconds
  - `is_signed` (`bool`) - Whether the message was signed
  - `bot_name` (`str`) - Which bot received the message
  - `sender_uuid` (`str`, optional) - Player UUID (present for PLAYER_CHAT, absent for SYSTEM_MESSAGE)
  - `minecraft_chat_type` (`str`, optional) - Minecraft chat type (only for PLAYER_CHAT), one of:
    - `"CHAT"` - Normal player chat
    - `"MSG_COMMAND_INCOMING"` - Received whisper
    - `"MSG_COMMAND_OUTGOING"` - Sent whisper
    - `"EMOTE_COMMAND"` - /me command
    - `"SAY_COMMAND"` - /say command
    - `"TEAM_MSG_COMMAND_INCOMING"` - Received team message
    - `"TEAM_MSG_COMMAND_OUTGOING"` - Sent team message
    - `"UNKNOWN"` - Unknown chat type

This event is also delivered to **global scripts**, which coordinate multiple
bots; use `bot_name` to tell which bot received the message.

```python
@on("chat_message")
def handle_chat(chat_data):
    sender = chat_data["sender"]
    message = chat_data["content"]
    msg_type = chat_data["type"]

    if message == "!help":
        bot.chat("Available commands: !help, !goto")

    # Check if it's a whisper
    if chat_data.get("minecraft_chat_type") == "MSG_COMMAND_INCOMING":
        utils.log(f"Received whisper from {sender}: {message}")
```

### `health_change`

Fired when bot health changes.

**Parameters:**

- `old_health` (`float`) - Previous health
- `new_health` (`float`) - New health

```python
@on("health_change")
def health_monitor(old_health, new_health):
    if new_health < 10:
        utils.log("Low health warning!")
```

### `hunger_change`

Fired when bot hunger changes.

**Parameters:**

- `old_hunger` (`float`) - Previous hunger
- `new_hunger` (`float`) - New hunger

```python
@on("hunger_change")
def hunger_monitor(old_hunger, new_hunger):
    if new_hunger < 6:
        utils.log("Low hunger!")
```

### `player_state`

Fired when player state updates.

**Parameters:**

- `state` (`dict`) - Player state information

```python
@on("player_state")
def state_update(state):
    utils.log(f"State: {state}")
```

### `inventory_update`

Fired when inventory is updated.

**Parameters:**

- `selected_slot` (`int`) - Currently selected hotbar slot (0-8)
- `inventory` (`list`) - List of inventory items, each item is a dict with keys: `slot`, `item_id`, `count`, `display_name`, `enchantments`. Empty slots are omitted. The full inventory is always passed, even though the client sends updates as deltas.

```python
@on("inventory_update")
def inv_update(selected_slot, inventory):
    utils.log(f"Inventory: {len(inventory)} items, slot {selected_slot}")
    for item in inventory:
        utils.log(f"  {item['display_name']} x{item['count']} in slot {item['slot']}")
```

### `screen_updated`

Fired when the current GUI screen changes or its state is refreshed (e.g. after a button click or key press).

**Parameters:**

- `screen` (`ScreenState` or `None`) - Full current screen state, or `None` if no screen is open (back in-game)

```python
@on("screen_updated")
def on_screen_update(screen):
    if screen is None:
        utils.log("Back in-game")
    elif "DeathScreen" in screen.screen_class:
        bot.chat("I died!")
    else:
        utils.log(f"Screen: {screen.title or screen.screen_class}, {len(screen.widgets)} widgets")
```

### `container_update`

Fired when an open container's contents are synced (chest, furnace, crafting table,
etc.). Not fired when the container closes.

**Parameters:**

- `container` (`dict`) - Container state with keys:
  - `id` (`int`) - Container/window ID
  - `type` (`int`) - Menu type ID
  - `items` (`list`) - Items in the container, each a dict with keys `slot`, `item_id`, `count`, `damage`, `max_damage`, `display_name`, `enchantments`
  - `x`, `y`, `z` (`int`, optional) - Block position, present only for containers with a world position
  - `properties` (`dict`, optional) - Menu properties (e.g. furnace burn/cook progress), present only when non-empty

Slot numbering covers the whole open menu: container slots first, then the player's
main inventory and hotbar (for a single chest: 0-26 chest, 27-53 player main, 54-62
hotbar).

```python
@on("container_update")
def on_container(container):
    utils.log(f"Container {container['id']} has {len(container['items'])} slots")
    for item in container['items']:
        if item['item_id'] != "minecraft:air":
            utils.log(f"  slot {item['slot']}: {item['item_id']} x{item['count']}")
```

### `block_update`

Fired when a single block changes.

**Parameters:**

- `x` (`int`) - Block X coordinate
- `y` (`int`) - Block Y coordinate
- `z` (`int`) - Block Z coordinate
- `block_id` (`str`) - New block state string (e.g. `"minecraft:oak_log[axis=y]"`)

```python
@on("block_update")
def on_block(x, y, z, block_id):
    if "chest" in block_id:
        utils.log(f"Chest placed at ({x}, {y}, {z})")
```

### `multi_block_update`

Fired when several blocks in one chunk section change at once. Only the number of changed blocks is passed; the world data is already
updated, so query positions with [`world.get_block`](api/world.md).

**Parameters:**

- `count` (`int`) - Number of blocks that changed

```python
@on("multi_block_update")
def on_multi_block(count):
    utils.log(f"{count} blocks changed at once")
```

### `chunk_loaded`

Fired when a chunk is received from the server and added to the world data.

**Parameters:**

- `chunk_x` (`int`) - Chunk X coordinate (block X >> 4)
- `chunk_z` (`int`) - Chunk Z coordinate (block Z >> 4)
- `dimension` (`str`) - Dimension the chunk belongs to (e.g. `"minecraft:overworld"`)

```python
@on("chunk_loaded")
def on_chunk(chunk_x, chunk_z, dimension):
    utils.log(f"Loaded chunk ({chunk_x}, {chunk_z}) in {dimension}")
```

### `chunk_unloaded`

Fired when a chunk leaves render distance and is dropped from the world data. Blocks
in that chunk are no longer readable from memory (`world.get_block(..., use_disk=True)`
can still read them if world saving is on).

**Parameters:**

- `chunk_x` (`int`) - Chunk X coordinate
- `chunk_z` (`int`) - Chunk Z coordinate

```python
@on("chunk_unloaded")
def on_chunk_unload(chunk_x, chunk_z):
    utils.log(f"Unloaded chunk ({chunk_x}, {chunk_z})")
```

### `baritone_status_update`

Fired when baritone pathfinding status changes.

**Parameters:**

- `status` (`dict`) - Baritone status information with keys:
  - `is_pathing` (`bool`) - Whether currently pathfinding
  - `event_type` (`PathEventType`) - Path event type (see `baritone.PathEventType` enum)
  - `goal_description` (`str`, optional) - Description of current goal
  - `active_process` (`dict`, optional) - Active process info
  - `estimated_ticks_to_goal` (`float`, optional) - ETA in ticks
  - `ticks_remaining_in_segment` (`float`, optional) - Ticks remaining in segment

```python
from baritone import PathEventType

@on("baritone_status_update")
def on_baritone_update(status):
    if status['is_pathing']:
        if 'estimated_ticks_to_goal' in status:
            seconds = status['estimated_ticks_to_goal'] / 20.0
            utils.log(f"Pathfinding - ETA: {seconds:.1f}s")

        # Check if reached goal
        if status['event_type'] == PathEventType.AT_GOAL:
            utils.log("Reached destination!")
        elif status['event_type'] == PathEventType.CALC_FAILED:
            utils.log("Pathfinding failed!")
```

### `baritone_log`

Fired when Baritone logs a message on the client (chat log lines, toasts, desktop
notifications). Baritone writes this output directly to the local chat HUD without any
network packet, so it does not appear in `chat_message` events.

**Parameters:**

- `log` (`dict`) - Log message details with keys:
  - `content` (`str`) - Plain message text, with the `[Baritone]` prefix stripped
  - `kind` (`str`) - Where Baritone sent the message: `"chat"`, `"toast"` or `"notification"`
  - `is_error` (`bool`) - Whether Baritone flagged the message as an error (`"notification"` kind only)
  - `title` (`str`, optional) - Toast title (`"toast"` kind, only when Baritone used a custom title)
  - `bot_name` (`str`) - Which bot's Baritone emitted the message

This event is also delivered to **global scripts**, which coordinate multiple
bots; use `bot_name` to tell whose Baritone spoke.
  - `timestamp` (`int`) - Unix timestamp in milliseconds

```python
@on("baritone_log")
def on_baritone_log(log):
    utils.log(f"Baritone: {log['content']}")

    # React to output that has no structured event
    if log['content'].startswith("Unable to find a path"):
        utils.log("Pathfinding gave up, trying a different goal")
```

### `script_message`

Fired when another script sends this script a message via
[`comms.emit`](api/comms.md#emittopic-datanone) or
[`comms.send`](api/comms.md#sendbot_name-datanone-topic). This event is also
delivered to **global scripts**. Registering the handler makes the script
reachable by `comms.send`; broadcasts additionally require a
`comms.subscribe(topic)` call. While this handler is registered,
`comms.receive()` gets nothing - messages go to the handler.

**Parameters:**

- `msg` (`dict`) - The message, with keys:
  - `topic` (`str`) - Topic passed to `emit`/`send` (`""` for a plain `send`)
  - `data` - The payload (plain data, deep-copied)
  - `sender_scope` (`str`) - Sender's bot name, or `"_global"`
  - `sender_script` (`str`) - Sender's script filename
  - `timestamp` (`float`) - Send time, epoch seconds

```python
comms.subscribe("tasks")

@on("script_message")
def on_message(msg):
    utils.log(f"{msg['sender_scope']}/{msg['sender_script']}: {msg['data']}")
    if msg["topic"] == "tasks":
        comms.send(msg["sender_scope"], {"status": "accepted"})
```

### `bot_connected`

Fired when a bot finishes connecting to the manager (status becomes Online).
Delivered to the bot's own scripts and to **global scripts**.

**Parameters:**

- `bot_name` (`str`) - Name of the bot that connected

```python
@on("bot_connected")
def on_connect(bot_name):
    utils.log(f"{bot_name} is online")
    comms.send(bot_name, {"cmd": "resume"})
```

### `bot_disconnected`

Fired when a bot disconnects from the manager (status becomes Offline).
Delivered to the bot's own scripts and to **global scripts**. The bot's
scripts keep running in the manager after a disconnect.

**Parameters:**

- `bot_name` (`str`) - Name of the bot that disconnected
- `uptime_seconds` (`float`) - How long the bot was connected (0.0 if unknown)

```python
@on("bot_disconnected")
def on_disconnect(bot_name, uptime_seconds):
    utils.log(f"{bot_name} went offline after {uptime_seconds:.0f}s")
```
