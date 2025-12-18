# baritone module

## Navigation

### `goto(x, y, z, bot="")` or `goto(x, z, bot="")`

Navigate to coordinates. You can specify either X/Y/Z coordinates or just X/Z coordinates (Y level will be determined by baritone).

**Parameters:**

- `x` (`float`) - X coordinate
- `y` (`float`, optional) - Y coordinate
- `z` (`float`) - Z coordinate
- `bot` (`str`, optional) - Bot name

**Examples:**

```python
# Navigate to specific X/Y/Z coordinates
baritone.goto(100, 64, 200)

# Navigate to X/Z coordinates (Y determined by baritone)
baritone.goto(100, 200)
```

### `follow(player, bot="")`

Follow a player.

**Parameters:**

- `player` (`str`) - Player name to follow
- `bot` (`str`, optional) - Bot name

```python
baritone.follow("PlayerName")
```

### `cancel(bot="")`

Cancel current task.

**Parameters:**

- `bot` (`str`, optional) - Bot name

```python
baritone.cancel()
```

## Automation

### `mine(block_type, bot="")`

Mine specific block type.

**Parameters:**

- `block_type` (`str`) - Block type to mine (e.g., `"diamond_ore"`)
- `bot` (`str`, optional) - Bot name

```python
baritone.mine("diamond_ore")
```

### `farm(bot="")`

Start farming.

**Parameters:**

- `bot` (`str`, optional) - Bot name

```python
baritone.farm()
```

## Commands

### `command(command, bot="")`

Send Baritone command directly.

**Parameters:**

- `command` (`str`) - Baritone command
- `bot` (`str`, optional) - Bot name

```python
baritone.command("build structure.schematic")
```

## Settings

### `set_setting(setting, value, bot="")`

Set Baritone setting.

**Parameters:**

- `setting` (`str`) - Setting name
- `value` - Setting value
- `bot` (`str`, optional) - Bot name

```python
baritone.set_setting("allowBreak", True)
baritone.set_setting("primaryTimeoutMS", 5000)
```

### `get_setting(setting, bot="")`

Get Baritone setting value.

**Parameters:**

- `setting` (`str`) - Setting name
- `bot` (`str`, optional) - Bot name

**Returns:** Setting value

```python
value = baritone.get_setting("allowBreak")
```

## Status Monitoring

### Path Event Types

Baritone provides an enum `PathEventType` for path events:

```python
from baritone import PathEventType

# Available values:
PathEventType.CALC_STARTED                      # 0
PathEventType.CALC_FINISHED_NOW_EXECUTING       # 1
PathEventType.CALC_FAILED                       # 2
PathEventType.NEXT_SEGMENT_CALC_STARTED         # 3
PathEventType.NEXT_SEGMENT_CALC_FINISHED        # 4
PathEventType.CONTINUING_ONTO_PLANNED_NEXT      # 5
PathEventType.SPLICING_ONTO_NEXT_EARLY          # 6
PathEventType.AT_GOAL                           # 7
PathEventType.PATH_FINISHED_NEXT_STILL_CALCULATING  # 8
PathEventType.NEXT_CALC_FAILED                  # 9
PathEventType.DISCARD_NEXT                      # 10
PathEventType.CANCELED                          # 11
```

### `get_process_status(bot="")`

Get current Baritone process status and pathfinding state.

**Parameters:**

- `bot` (`str`, optional) - Bot name

**Returns:** Dictionary with the following keys:

- `is_pathing` (`bool`) - Whether the bot is currently pathfinding
- `event_type` (`PathEventType`) - Current path event type (see enum above)
- `goal_description` (`str`, optional) - Description of current goal
- `active_process` (`dict`, optional) - Active process info with keys:
    - `process_name` (`str`) - Internal process name
    - `display_name` (`str`) - User-friendly display name
    - `priority` (`float`) - Process priority
    - `is_active` (`bool`) - Whether process is active
    - `is_temporary` (`bool`) - Whether process is temporary
- `estimated_ticks_to_goal` (`float`, optional) - Estimated ticks remaining to reach goal
- `ticks_remaining_in_segment` (`float`, optional) - Ticks remaining in current path segment

**Polling approach:**

!!! note "Timing Consideration"
    When you call `goto()`, baritone needs time to calculate the path. The status won't immediately show `is_pathing=True`. You can either:

    - Wait a moment before checking status (1-2 seconds)
    - Use the `baritone_status_update` event instead (recommended)
    - Poll until `is_pathing` becomes `True` or an event occurs

```python
import baritone
from baritone import PathEventType
import time

# Start pathfinding to coordinates
baritone.goto(500, 70, -300)

# Wait for pathfinding to start
while True:
    status = baritone.get_process_status()
    if status['is_pathing']:
        print("Pathfinding has started!")
        break
    time.sleep(0.1)

# Monitor progress
while True:
    status = baritone.get_process_status()

    if not status['is_pathing']:
        print("Pathfinding complete or stopped")
        break

    # Show ETA if available
    if 'estimated_ticks_to_goal' in status:
        seconds = status['estimated_ticks_to_goal'] / 20.0
        print(f"ETA: {seconds:.1f}s")

    # Check event type
    event = status['event_type']
    if event == PathEventType.CALC_FAILED:
        print("Pathfinding failed!")
        break
    elif event == PathEventType.AT_GOAL:
        print("Reached goal!")
        break

    time.sleep(1)
```

**Events approach:**

```python
from baritone import PathEventType

# Track whether we've started
started = False

@on("baritone_status_update")
def on_status(status):
    global started

    if status['is_pathing']:
        if not started:
            utils.log("Pathfinding started!")
            started = True

        if 'estimated_ticks_to_goal' in status:
            seconds = status['estimated_ticks_to_goal'] / 20.0
            utils.log(f"ETA: {seconds:.1f}s")

    # Check completion
    event = status['event_type']
    if event == PathEventType.AT_GOAL:
        utils.log("Reached destination!")
        started = False
    elif event == PathEventType.CALC_FAILED:
        utils.log("Pathfinding failed!")
        started = False

# Start pathfinding - events will handle the rest
baritone.goto(500, 70, -300)
```
