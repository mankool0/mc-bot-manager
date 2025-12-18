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
- `inventory` (`list`) - List of inventory items, each item is a dict with keys: `slot`, `item_id`, `count`, `display_name`

```python
@on("inventory_update")
def inv_update(selected_slot, inventory):
    utils.log(f"Inventory: {len(inventory)} items, slot {selected_slot}")
    for item in inventory:
        utils.log(f"  {item['display_name']} x{item['count']} in slot {item['slot']}")
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
