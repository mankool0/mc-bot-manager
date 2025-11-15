# baritone module

## Navigation

### `goto(x, y, z, bot="")`

Navigate to coordinates.

**Parameters:**

- `x` (`float`) - X coordinate
- `y` (`float`) - Y coordinate
- `z` (`float`) - Z coordinate
- `bot` (`str`, optional) - Bot name

```python
baritone.goto(100, 64, 200)
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
