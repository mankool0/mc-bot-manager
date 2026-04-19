# world module

World data queries and block interaction.

The world module provides access to chunk data collected from the Minecraft client, enabling block queries and world interaction. Chunks are automatically synchronized and cached in memory.

## Block Queries

### `get_block(x, y, z, use_disk=False, dimension="", bot="")`

Get the block state at the specified coordinates.

**Parameters:**

- `x` (`int`) - Block X coordinate
- `y` (`int`) - Block Y coordinate
- `z` (`int`) - Block Z coordinate
- `use_disk` (`bool`, optional) - If `True` and the chunk is not loaded in memory, read the block from the saved `.mca` region file on disk (default: `False`)
- `dimension` (`str`, optional) - Dimension string (e.g. `"minecraft:overworld"`, `"minecraft:the_nether"`). Defaults to the bot's current dimension. **Requires `use_disk=True`** - raises `ValueError` if set without it. If the specified dimension differs from the bot's current one, memory is skipped and disk is read directly.
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `str` - Block state string (e.g., `"minecraft:stone"`, `"minecraft:chest[facing=north]"`), or `None` if chunk is not loaded (and not on disk when `use_disk=True`) or bot is offline

**Raises:** `RuntimeError` if bot not found or not online; `ValueError` if `dimension` is set without `use_disk=True`

**Note:** Disk reads require world saving to be enabled. Returns `None` if the world save path is not available or the chunk has never been saved.

```python
# Get block at specific position
block = world.get_block(100, 64, 200)
if block:
    print(f"Block at (100, 64, 200): {block}")

    # Check block type
    if "chest" in block:
        print("Found a chest!")

# Read a block from a chunk that isn't currently loaded
block = world.get_block(5000, 64, 5000, use_disk=True)
if block:
    print(f"Saved block: {block}")

# Read a block from a different dimension
block = world.get_block(100, 64, 100, use_disk=True, dimension="minecraft:the_nether")
```

### `find_blocks(block_type, center_x, center_y, center_z, radius, min_block_light=0, max_block_light=15, min_sky_light=0, max_sky_light=15, bot="")`

Find all blocks of a specific type within a spherical radius, with optional light level filters.

This function only searches loaded chunks. Blocks in unloaded chunks will not be found.

**Parameters:**

- `block_type` (`str`) - Block type to search for (e.g., `"minecraft:diamond_ore"`)
- `center_x` (`float`) - Search center X coordinate
- `center_y` (`float`) - Search center Y coordinate
- `center_z` (`float`) - Search center Z coordinate
- `radius` (`int`) - Search radius in blocks
- `min_block_light` (`int`, optional) - Minimum block light level, inclusive (default: 0)
- `max_block_light` (`int`, optional) - Maximum block light level, inclusive (default: 15)
- `min_sky_light` (`int`, optional) - Minimum sky light level, inclusive (default: 0)
- `max_sky_light` (`int`, optional) - Maximum sky light level, inclusive (default: 15)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `list[tuple]` - List of block positions as `(x, y, z)` tuples of floats

**Note:** Returns float coordinates. Convert to int for block operations: `int(x), int(y), int(z)`

**Raises:** `RuntimeError` if bot not found or not online

```python
# Find all diamond ore within 64 blocks of bot
pos = bot.position()
diamonds = world.find_blocks("minecraft:diamond_ore",
                             pos["x"], pos["y"], pos["z"],
                             radius=64)

print(f"Found {len(diamonds)} diamond ore blocks")
for x, y, z in diamonds:
    print(f"  Diamond at ({x}, {y}, {z})")

# Find all chests near spawn
chests = world.find_blocks("minecraft:chest", 0, 64, 0, radius=100)

# Find air blocks with no block light and no sky access (fully dark, unlit caves)
pos = bot.position()
dark_air = world.find_blocks("minecraft:air",
                             pos["x"], pos["y"], pos["z"],
                             radius=128,
                             max_block_light=0,
                             max_sky_light=0)
```

### `find_nearest(block_types, max_distance=128, bot="")`

Find the nearest block matching any of the specified types.

This function searches from the bot's current position and only searches loaded chunks.

**Parameters:**

- `block_types` (`list[str]`) - List of block types to search for
- `max_distance` (`int`, optional) - Maximum search distance in blocks (default: 128)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `tuple` - Position as `(x, y, z)` tuple of floats, or `None` if no matching block found

**Note:** Returns float coordinates. Convert to int for block operations: `int(x), int(y), int(z)`

**Raises:** `RuntimeError` if bot not found or not online

```python
import time

# Find nearest chest or barrel
container = world.find_nearest([
    "minecraft:chest",
    "minecraft:barrel",
    "minecraft:trapped_chest"
])

if container:
    x, y, z = container
    print(f"Found container at ({int(x)}, {int(y)}, {int(z)})")

    # Find a valid standing position around the container
    # Check adjacent blocks (not diagonals)
    offsets = [(1, 0, 0), (-1, 0, 0), (0, 0, 1), (0, 0, -1)]
    standing_pos = None

    for dx, dy, dz in offsets:
        check_x, check_y, check_z = int(x) + dx, int(y) + dy, int(z) + dz
        # Check if position is air (can stand there)
        block_at_pos = world.get_block(check_x, check_y, check_z)
        block_above = world.get_block(check_x, check_y + 1, check_z)

        if block_at_pos and block_above and "air" in block_at_pos and "air" in block_above:
            standing_pos = (check_x, check_y, check_z)
            break

    if standing_pos:
        sx, sy, sz = standing_pos
        print(f"Going to standing position ({sx}, {sy}, {sz})")
        baritone.goto(sx, sy, sz)

        # Wait for bot to arrive
        while True:
            pos = bot.position()
            if abs(pos["x"] - sx) < 1.5 and abs(pos["y"] - sy) < 1.5 and abs(pos["z"] - sz) < 1.5:
                break
            time.sleep(0.2)

        # To interact, convert to int:
        world.interact_block(int(x), int(y), int(z))
    else:
        print("No valid standing position found near container")
else:
    print("No containers found nearby")

# Find nearest ore
ore = world.find_nearest([
    "minecraft:diamond_ore",
    "minecraft:iron_ore",
    "minecraft:coal_ore"
], max_distance=50)

if ore:
    x, y, z = ore
    print(f"Found ore at ({int(x)}, {int(y)}, {int(z)})")
    block = world.get_block(int(x), int(y), int(z))
    print(f"Ore type: {block}")
else:
    print("No ore found within 50 blocks")
```

## Block Entities

Block entities are blocks with attached data: chests, furnaces, signs, shulker boxes, etc. The bot tracks block entities for chunks it has loaded this session. With `use_disk=True`, entities from saved but currently unloaded chunks can also be queried.

### `get_block_entity(x, y, z, use_disk=False, dimension="", bot="")`

Get the block entity at the specified position.

**Parameters:**

- `x` (`int`) - Block X coordinate
- `y` (`int`) - Block Y coordinate
- `z` (`int`) - Block Z coordinate
- `use_disk` (`bool`, optional) - If `True` and no in-memory data exists, read from the saved `.mca` file (default: `False`)
- `dimension` (`str`, optional) - Dimension string. Defaults to the bot's current dimension. **Requires `use_disk=True`** - raises `ValueError` if set without it.
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `dict` or `None` if no block entity exists at that position

| Key | Type | Present when |
|-----|------|-------------|
| `type` | `str` | always (e.g. `"minecraft:chest"`) |
| `x` | `int` | always |
| `y` | `int` | always |
| `z` | `int` | always |
| `items` | `list` | container was opened this session (memory) or in a previous session that was saved to disk |

**Note on `items`:** The Minecraft server only sends container contents when a container is opened, so `items` is only present if the container was opened at some point. Memory always takes priority: if items are already in memory (opened this session), they are returned even when `use_disk=True`. Disk is only consulted when items are absent from memory - in that case, items may be present on disk if the container was opened in a previous session that was saved.

```python
# items only present if container was opened this session
be = world.get_block_entity(cx, cy, cz)
if be and 'items' in be:
    for item in be['items']:
        if item['item_id'] != 'minecraft:air':
            utils.log(f"  {item['count']}x {item['item_id']}")

# also check saved data from previous sessions where container was opened
be = world.get_block_entity(cx, cy, cz, use_disk=True)
if be and 'items' in be:
    utils.log(f"Chest contents from disk: {len(be['items'])} stacks")

# check a saved but unloaded chunk
be = world.get_block_entity(5000, 64, 5000, use_disk=True)
if be and be['type'] == 'minecraft:chest':
    utils.log("Found a chest in saved data")
```

---

### `get_block_entities_in_chunk(chunk_x, chunk_z, use_disk=False, dimension="", bot="")`

Get all block entities in a chunk.

**Parameters:**

- `chunk_x` (`int`) - Chunk X coordinate (block X divided by 16, rounded down)
- `chunk_z` (`int`) - Chunk Z coordinate (block Z divided by 16, rounded down)
- `use_disk` (`bool`, optional) - If `True` and the chunk is not loaded, read from the saved `.mca` file (default: `False`)
- `dimension` (`str`, optional) - Dimension string (e.g. `"minecraft:overworld"`, `"minecraft:the_nether"`). Defaults to the bot's current dimension. If a different dimension is specified, **requires `use_disk=True`** - raises `ValueError` otherwise.
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `list[dict]` - List of block entity dicts (same schema as `get_block_entity`)

Memory always takes priority: if the chunk is loaded, memory data is returned regardless of `use_disk`. Disk is only read when the chunk is not loaded. `items` is present on any container that was opened (either this session from memory, or a previous session from disk).

```python
import math

# Get all block entities in the chunk under the bot
pos = bot.position()
cx = math.floor(pos['x'] / 16)
cz = math.floor(pos['z'] / 16)

entities = world.get_block_entities_in_chunk(cx, cz)
for be in entities:
    utils.log(f"{be['type']} at ({be['x']}, {be['y']}, {be['z']})")

# Scan a saved chunk for chests
entities = world.get_block_entities_in_chunk(312, -5, use_disk=True)
chests = [be for be in entities if be['type'] == 'minecraft:chest']
utils.log(f"Found {len(chests)} chests in saved chunk (312, -5)")

# Scan a chunk in the nether from disk
nether_ents = world.get_block_entities_in_chunk(10, 10,
    use_disk=True, dimension="minecraft:the_nether")
```

---

## Block Interaction

### `hold_attack(enabled, duration_ticks=0, bot_name="")`

Hold or release the left-click attack button in-game. While enabled, the client drives `continueDestroyBlock` every game tick against whatever block the crosshair is currently targeting.

**Parameters:**

- `enabled` (`bool`) - `True` to start holding attack, `False` to release
- `duration_ticks` (`int`, optional) - Auto-release after this many game ticks. `0` holds indefinitely until an explicit `False` call (default: `0`)
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

```python
# Hold for 100 ticks (5 seconds), then auto-release
world.hold_attack(True, duration_ticks=100)

# Hold indefinitely
world.hold_attack(True)
# ... later:
world.hold_attack(False)
```

---

### `get_hold_attack(bot_name="")`

Query whether the client is currently holding the attack button.

**Returns:** `bool` - `True` if attack is being held, `False` otherwise

**Parameters:**

- `bot_name` (`str`, optional) - Bot name, defaults to current bot

```python
if world.get_hold_attack():
    utils.log("Currently mining")
```

---

### `look_at(x, y, z, bot_name="")`

Rotate the bot to look at a specific position.

**Parameters:**

- `x` (`float`) - X coordinate to look at
- `y` (`float`) - Y coordinate to look at
- `z` (`float`) - Z coordinate to look at
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
# Look at the top face of a block before placing on it
world.look_at(bx + 0.5, by + 1.0, bz + 0.5)
world.interact_block(bx, by, bz, sneak=True, look_at_block=False)
```

### `interact_block(x, y, z, sneak=False, look_at_block=True, bot="")`

Interact with (right-click) a block at the specified position.

This is used to open containers, press buttons, use beds, etc.

**Parameters:**

- `x` (`int`) - Block X coordinate
- `y` (`int`) - Block Y coordinate
- `z` (`int`) - Block Z coordinate
- `sneak` (`bool`, optional) - Whether to sneak while interacting (default: False)
- `look_at_block` (`bool`, optional) - Whether to rotate and look at the block before interacting (default: True)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
# Open a chest (bot will look at it)
world.interact_block(100, 64, 200)

# Open a chest while sneaking (places block instead if holding one)
world.interact_block(100, 64, 200, sneak=True)

# Interact without looking at the block
world.interact_block(100, 64, 200, look_at_block=False)

# Use a bed
pos = world.find_nearest(["minecraft:white_bed"])
if pos:
    x, y, z = pos
    world.interact_block(int(x), int(y), int(z))
```

## Container Interaction

### `get_container(bot="")`

Get the currently open container. Returns `None` if no container is open or bot is offline.

**Returns:** `dict` with container info, or `None`

| Key | Type | Description |
|-----|------|-------------|
| `id` | `int` | Container ID |
| `type` | `ContainerType` | Container type enum value |
| `items` | `list` | List of [item dicts](bot.md#item-dict) for all slots |

**Note:** Only works for external containers (chests, barrels, etc.). For the player's own inventory use `bot.inventory()`.

```python
import world, time

world.interact_block(cx, cy, cz)
time.sleep(0.3)  # wait for server to open container

container = world.get_container()
if container:
    print(f"Container type: {container['type']}")
    for item in container['items']:
        if item['item_id'] != 'minecraft:air':
            print(f"  Slot {item['slot']}: {item['count']}x {item['item_id']}")
```

---

### `click_slot(slot_index, button=world.MouseButton.LEFT, click_type=world.ClickType.PICKUP, bot="")`

Click a slot in the currently open container (or player inventory).

**Parameters:**

- `slot_index` (`int`) - Slot index using **InventoryMenu numbering** (see note below)
- `button` (`int` or `MouseButton`, optional) - Mouse button for normal clicks, or hotbar key number (1-9) for `SWAP` (default: `LEFT`)
- `click_type` (`ClickType`, optional) - Click type (default: `PICKUP`)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

**Slot numbering** depends on what is open. Container slots always start at 0, with player inventory slots appended after.

**Single chest (27 slots):**

| Range | Contents |
|-------|----------|
| 0–26 | Chest |
| 27–53 | Player main inventory |
| 54–62 | Player hotbar |

**Double chest (54 slots):**

| Range | Contents |
|-------|----------|
| 0–53 | Chest |
| 54–80 | Player main inventory |
| 81–89 | Player hotbar |

**Player inventory screen (no external container):**

| Range | Contents |
|-------|----------|
| 0 | Crafting output |
| 1–4 | Crafting grid |
| 5–8 | Armor |
| 9–35 | Main inventory |
| 36–44 | Hotbar |
| 45 | Offhand |

> **Note:** `bot.inventory()` numbers hotbar as 0–8 and main as 9–35. When a chest is open, player main starts at `chest_size` and hotbar at `chest_size + 27`, so `bot.inventory()` slot numbers cannot be used directly - calculate the offset based on container size.

```python
import world

# Shift-click slot 0 of a chest into inventory
world.click_slot(0, click_type=world.ClickType.QUICK_MOVE)

# Swap chest slot 0 with hotbar slot 8 (button = slot index + 1)
world.click_slot(0, button=9, click_type=world.ClickType.SWAP)

# Right-click a slot (split stack)
world.click_slot(5, button=world.MouseButton.RIGHT)
```

---

### `click_widget(screen_id, widget_index, button=world.MouseButton.LEFT, bot="")`

Click a widget (button, etc.) on the currently open screen.

**Parameters:**

- `screen_id` (`str`) - The `id` from `bot.get_screen()`. Ensures the screen hasn't changed since you read the dump.
- `widget_index` (`int`) - The `index` from a `GuiWidget` in `bot.get_screen().widgets`
- `button` (`MouseButton`, optional) - Mouse button (default: `LEFT`)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found, not online, or `screen_id` doesn't match the currently open screen

> **Note:** Only use for non-slot widgets (buttons, checkboxes, etc.). For container slots, use `click_slot()`.

```python
screen = bot.get_screen()
if screen is not None:
    for w in screen.widgets:
        if w.text == "Accept" and w.active:
            world.click_widget(screen.id, w.index)
            break
```

---

### `click_screen(screen_id, x, y, button=world.MouseButton.LEFT, bot="")`

Click at specific pixel coordinates on the currently open screen. Useful for sliders and other widgets where you need to control the exact click position.

**Parameters:**

- `screen_id` (`str`) - The `id` from `bot.get_screen()`. Ensures the screen hasn't changed since you read the dump.
- `x` (`float`) - Screen pixel X coordinate
- `y` (`float`) - Screen pixel Y coordinate
- `button` (`MouseButton`, optional) - Mouse button (default: `LEFT`)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found, not online, or `screen_id` doesn't match the currently open screen

```python
screen = bot.get_screen()
if screen is not None:
    for w in screen.widgets:
        if w.text.startswith("Max Framerate"):
            # Slider range: 10-260 fps
            fraction = (90 - 10) / (260 - 10)
            click_x = w.x + fraction * w.width
            world.click_screen(screen.id, click_x, w.y + w.height / 2)
            break
```

---

### `type_text(screen_id, text, bot="")`

Type text into the currently focused element on the open screen (sign lines, chat, edit boxes). Sends individual `charTyped` events for each character.

**Parameters:**

- `screen_id` (`str`) - The `id` from `bot.get_screen()`. Ensures the screen hasn't changed since you read the dump.
- `text` (`str`) - The text to type
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
screen = bot.get_screen()
if screen and "SignEditScreen" in screen.screen_class:
    world.type_text(screen.id, "Hello world")
    world.press_key(screen.id, world.Key.ENTER)
```

---

### `press_key(screen_id, key_code, modifiers=0, bot="")`

Press a key on the currently open screen (sends `keyPressed` + `keyReleased`).

**Parameters:**

- `screen_id` (`str`) - The `id` from `bot.get_screen()`. Ensures the screen hasn't changed since you read the dump.
- `key_code` (`int`) - GLFW key code - use `world.Key.*` constants
- `modifiers` (`int`, optional) - GLFW modifier flags - use `world.KeyMod.*` constants, default `0`
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

**`world.Key` constants:**

| Group | Constants |
|---|---|
| Navigation | `UP`, `DOWN`, `LEFT`, `RIGHT`, `HOME`, `END`, `PAGE_UP`, `PAGE_DOWN`, `INSERT`, `DELETE` |
| Control | `ESCAPE`, `ENTER`, `TAB`, `BACKSPACE`, `SPACE`, `CAPS_LOCK`, `SCROLL_LOCK`, `NUM_LOCK`, `PRINT_SCREEN`, `PAUSE`, `MENU` |
| Letters | `A`-`Z` |
| Digits | `NUM_0`-`NUM_9` |
| Punctuation | `APOSTROPHE`, `COMMA`, `MINUS`, `PERIOD`, `SLASH`, `SEMICOLON`, `EQUAL`, `LEFT_BRACKET`, `BACKSLASH`, `RIGHT_BRACKET`, `GRAVE_ACCENT` |
| Function | `F1`-`F12` |
| Keypad | `KP_0`-`KP_9`, `KP_DECIMAL`, `KP_DIVIDE`, `KP_MULTIPLY`, `KP_SUBTRACT`, `KP_ADD`, `KP_ENTER`, `KP_EQUAL` |
| Modifiers | `LEFT_SHIFT`, `LEFT_CONTROL`, `LEFT_ALT`, `LEFT_SUPER`, `RIGHT_SHIFT`, `RIGHT_CONTROL`, `RIGHT_ALT`, `RIGHT_SUPER` |

**`world.KeyMod` constants:** `SHIFT`, `CONTROL`, `ALT`, `SUPER`, `CAPS_LOCK`, `NUM_LOCK`

```python
screen = bot.get_screen()
if screen is not None:
    # Select all text and delete it
    world.press_key(screen.id, world.Key.A, world.KeyMod.CONTROL)
    world.press_key(screen.id, world.Key.DELETE)
```

---

### `close_container(bot="")`

Close the currently open container.

**Raises:** `RuntimeError` if bot not found or not online

```python
world.close_container()
```

---

### `open_inventory(bot="")`

Open the player's own inventory screen.

**Raises:** `RuntimeError` if bot not found or not online

```python
world.open_inventory()
```

---

### Enums

#### `world.MouseButton`

| Value | Description |
|-------|-------------|
| `LEFT` | Left mouse button |
| `RIGHT` | Right mouse button |
| `MIDDLE` | Middle mouse button |

#### `world.ClickType`

| Value | Description |
|-------|-------------|
| `PICKUP` | Pick up / place item (left or right click) |
| `QUICK_MOVE` | Shift-click (move to/from inventory) |
| `SWAP` | Swap with hotbar slot (`button` = hotbar index 0–8) |
| `CLONE` | Clone item (creative mode middle-click) |
| `THROW` | Throw item out of inventory |
| `QUICK_CRAFT` | Quick craft drag operation |
| `PICKUP_ALL` | Double-click to collect all matching items |

#### `world.ContainerType`

| Value | |
|-------|-|
| `PLAYER_INVENTORY` | `CRAFTING_TABLE` |
| `CHEST` | `ENCHANTING_TABLE` |
| `ENDER_CHEST` | `ANVIL` |
| `SHULKER_BOX` | `BREWING_STAND` |
| `FURNACE` | `VILLAGER_TRADE` |
| `BLAST_FURNACE` | `HORSE_INVENTORY` |
| `SMOKER` | `HOPPER` |
| `DISPENSER` | `DROPPER` |
| `BEACON` | `OTHER` |

---

## Chunk Information

### `loaded_chunk_count(bot="")`

Get the number of chunks currently loaded in memory.

**Parameters:**

- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `int` - Number of loaded chunks

**Raises:** `RuntimeError` if bot not found or not online

```python
count = world.loaded_chunk_count()
print(f"Loaded chunks: {count}")
```

### `loaded_chunks(bot="")`

Get list of all loaded chunk positions.

**Parameters:**

- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `list[tuple]` - List of chunk positions as `(chunk_x, chunk_z)` tuples

**Raises:** `RuntimeError` if bot not found or not online

```python
chunks = world.loaded_chunks()
print(f"Loaded {len(chunks)} chunks:")
for cx, cz in chunks:
    print(f"  Chunk ({cx}, {cz}) - blocks ({cx*16}, {cz*16}) to ({cx*16+15}, {cz*16+15})")
```

### `memory_usage(bot="")`

Get the total memory used by world data storage.

**Parameters:**

- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `int` - Memory usage in bytes

**Raises:** `RuntimeError` if bot not found or not online

```python
memory = world.memory_usage()
print(f"World data memory usage: {memory / 1024 / 1024:.2f} MB")
```

## Usage Examples

### Mining Helper

```python
import time

# Check if bot has a pickaxe
inventory = bot.inventory()
has_pickaxe = False

if inventory:
    for item in inventory:
        if item and "pickaxe" in item["item_id"].lower():
            has_pickaxe = True
            print(f"Found {item['display_name']} in slot {item['slot']}")
            break

if not has_pickaxe:
    print("No pickaxe found in inventory! Cannot mine.")
else:
    # Find diamond ore
    pos = bot.position()
    diamonds = world.find_blocks("minecraft:diamond_ore",
                                 pos["x"], pos["y"], pos["z"],
                                 radius=64)

    if diamonds:
        print(f"Found {len(diamonds)} diamond ore blocks")

        # Sort by distance
        diamonds.sort(key=lambda p: (p[0]-pos["x"])**2 + (p[1]-pos["y"])**2 + (p[2]-pos["z"])**2)

        # Mine each one
        for x, y, z in diamonds:
            print(f"Mining diamond at ({int(x)}, {int(y)}, {int(z)})")

            # Go near the block
            baritone.goto(x, y, z)

            # Wait for arrival
            while True:
                p = bot.position()
                if abs(p["x"] - x) < 3 and abs(p["z"] - z) < 3:
                    break
                time.sleep(0.5)

            # Break the block using sel cleararea command
            # Set selection for the single block
            baritone.command(f"sel pos1 {int(x)} {int(y)} {int(z)}")
            baritone.command(f"sel pos2 {int(x)} {int(y)} {int(z)}")
            baritone.command("sel cleararea")

            # Wait for block to be broken
            while True:
                block = world.get_block(int(x), int(y), int(z))
                if block and "air" in block:
                    break
                time.sleep(0.5)

            # Clear selections for next iteration
            baritone.command("sel clear")
    else:
        print("No diamonds found nearby")
```

### Container Finder

```python
# Find all chests
pos = bot.position()
chests = world.find_blocks("minecraft:chest",
                           pos["x"], pos["y"], pos["z"],
                           radius=100)

print(f"Found {len(chests)} chests")

# Go to nearest chest and open it
if chests:
    nearest = min(chests, key=lambda p: (p[0]-pos["x"])**2 + (p[1]-pos["y"])**2 + (p[2]-pos["z"])**2)
    x, y, z = nearest

    baritone.goto(x, y+1, z)
    # Wait for arrival...

    world.interact_block(int(x), int(y), int(z))
    print("Opened chest!")
```

### Block Scanner

```python
# Scan area and categorize blocks
pos = bot.position()
radius = 32

ores = []
containers = []
fluids = []

chunks = world.loaded_chunks()
print(f"Scanning {len(chunks)} loaded chunks...")

# Count different block types
for cx, cz in chunks:
    # Check if chunk is in range
    chunk_center_x = cx * 16 + 8
    chunk_center_z = cz * 16 + 8
    dist = ((chunk_center_x - pos["x"])**2 + (chunk_center_z - pos["z"])**2)**0.5

    if dist > radius:
        continue

    # Scan blocks in chunk
    for x in range(cx * 16, cx * 16 + 16):
        for z in range(cz * 16, cz * 16 + 16):
            for y in range(-64, 320):
                block = world.get_block(x, y, z)
                if not block:
                    continue

                if "ore" in block:
                    ores.append((x, y, z, block))
                elif any(c in block for c in ["chest", "barrel", "shulker"]):
                    containers.append((x, y, z, block))
                elif any(f in block for f in ["water", "lava"]):
                    fluids.append((x, y, z, block))

print(f"Found {len(ores)} ore blocks")
print(f"Found {len(containers)} containers")
print(f"Found {len(fluids)} fluid blocks")
```

## Recipe Registry

### `get_recipe(recipe_id, bot="")`

Get recipe data by its exact recipe ID.

**Parameters:**

- `recipe_id` (`str`) - Exact recipe ID (e.g., `"minecraft:gold_ingot_from_gold_block"`)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `dict` with recipe data, or `None` if not found

```python
recipe = world.get_recipe("minecraft:stick")
if recipe:
    print(f"Makes {recipe['result_count']}x {recipe['result_item']}")
    for ing in recipe['ingredients']:
        print(f"  Slot {ing['slot']}: {ing['items']} x{ing['count']}")
```

**Recipe dict fields:**

| Field | Type | Description |
|---|---|---|
| `recipe_id` | `str` | The recipe's unique ID |
| `type` | `str` | Recipe type (e.g., `"minecraft:crafting_shaped"`) |
| `result_item` | `str` | Output item ID |
| `result_count` | `int` | Number of items produced |
| `is_shapeless` | `bool` | Whether the recipe is shapeless |
| `ingredients` | `list` | List of ingredient dicts (see below) |
| `experience` | `float` | XP granted (smelting recipes only) |
| `cooking_time` | `int` | Ticks to smelt (smelting recipes only) |

Each ingredient dict:

| Field | Type | Description |
|---|---|---|
| `slot` | `int` | Grid slot index (1-9 for 3×3, 1-4 for 2×2) |
| `count` | `int` | Amount required |
| `items` | `list[str]` | Accepted item IDs (e.g., any log type) |

---

### `get_recipes_for(item_id, bot="")`

Get all recipes that produce a given item.

**Parameters:**

- `item_id` (`str`) - Item ID to look up (e.g., `"minecraft:gold_ingot"`)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `list[dict]` - List of recipe dicts (same format as `get_recipe()`). Empty list if no recipes found.

```python
recipes = world.get_recipes_for("minecraft:gold_ingot")
print(f"{len(recipes)} way(s) to craft gold ingot:")
for r in recipes:
    print(f"  {r['recipe_id']}")
# e.g.:
#   minecraft:gold_ingot_from_nuggets
#   minecraft:gold_ingot_from_gold_block
```

---

### `get_item_info(item_id, bot="")`

Get item metadata from the item registry.

**Parameters:**

- `item_id` (`str`) - Item ID (e.g., `"minecraft:diamond_sword"`)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `dict` or `None` if item not found

| Field | Type | Description |
|---|---|---|
| `item_id` | `str` | Item ID |
| `display_name` | `str` | Human-readable item name (e.g., `"Diamond Pickaxe"`) |
| `max_stack_size` | `int` | Maximum stack size (usually 1, 16, or 64) |
| `max_damage` | `int` | Maximum durability (0 for non-damageable items) |

```python
info = world.get_item_info("minecraft:diamond_pickaxe")
if info:
    print(f"Name: {info['display_name']}")
    print(f"Max durability: {info['max_damage']}")
    print(f"Stack size: {info['max_stack_size']}")
```

---

### `get_all_recipes(bot="")`

Get a list of all known recipe IDs.

**Parameters:**

- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `list[str]` - All recipe IDs

```python
recipes = world.get_all_recipes()
# Filter to just pickaxe recipes
pickaxe_recipes = [r for r in recipes if "pickaxe" in r]
```

---

### `plan_recursive_craft(item_id, count=1, bot="")`

Plan the full crafting tree for an item, including all intermediate steps, based on the bot's current inventory.

**Parameters:**

- `item_id` (`str`) - Item to craft (e.g., `"minecraft:piston"`)
- `count` (`int`, optional) - How many to craft (default: 1)
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `dict`

| Field | Type | Description |
|---|---|---|
| `success` | `bool` | Whether a complete plan was found |
| `error` | `str` | Error message if `success` is False |
| `steps` | `list` | Ordered crafting steps (see below) |
| `raw_materials` | `dict` | Items needed that cannot be crafted: `{item_id: count}` |
| `leftovers` | `dict` | Items left over after all steps: `{item_id: count}` |

Each step dict:

| Field | Type | Description |
|---|---|---|
| `recipe_id` | `str` | Recipe to use for this step |
| `output_item` | `str` | Item produced |
| `output_count` | `int` | Items produced per craft |
| `times` | `int` | How many times to run this recipe |
| `inputs` | `dict` | Items consumed: `{item_id: count}` |

```python
plan = world.plan_recursive_craft("minecraft:piston", 4)
if not plan['success']:
    utils.error(f"Can't plan: {plan['error']}")
else:
    utils.log(f"Raw materials needed: {dict(plan['raw_materials'])}")
    for step in plan['steps']:
        utils.log(f"Craft {step['output_item']} x{step['times']} (recipe: {step['recipe_id']})")
```

!!! note
    Pass `step['recipe_id']` to `get_recipe()` when executing plan steps - do not re-look up by item ID, as there may be multiple recipes for the same output.

## Entity Tracking

Live entity data is pushed from the Minecraft client every tick (only changed/new/removed entities).

### Entity dict schema

| Key | Type | Present when |
|-----|------|-------------|
| `entity_id` | `int` | always |
| `uuid` | `str` | always |
| `type` | `str` | always (e.g. `"minecraft:item"`, `"minecraft:zombie"`) |
| `x`, `y`, `z` | `float` | always |
| `yaw`, `pitch` | `float` | always - yaw is normalised to -180–180 |
| `vel_x`, `vel_y`, `vel_z` | `float` | always |
| `health` | `float` | living entities |
| `max_health` | `float` | living entities |
| `item` | [item dict](bot.md#item-dict) | `type == "minecraft:item"` (dropped item entities) |
| `player_name` | `str` | player entities |

The `item` sub-dict follows the standard [item dict schema](bot.md#item-dict).

---

### `world.entities(bot="")`

Returns a list of all currently tracked entity dicts.

```python
import world

for e in world.entities():
    print(e['type'], e['x'], e['y'], e['z'])
```

---

### `world.find_entities_near(x, y, z, radius, type="", bot="")`

Returns entities within `radius` blocks of `(x, y, z)`. Optional `type` is a prefix filter on the entity type string (e.g. `"minecraft:item"` or `"minecraft:"` for all vanilla entities).

```python
import bot, world

pos = bot.position()
items = world.find_entities_near(pos['x'], pos['y'], pos['z'], 8, type='minecraft:item')
for item_ent in items:
    print(item_ent['item']['item_id'], 'at', item_ent['x'], item_ent['y'], item_ent['z'])
```

#### Wait for a specific item to drop after breaking a block

```python
import bot, world, time

MINE_ITEMS = {'minecraft:diamond', 'minecraft:ancient_debris'}

def wait_for_drops(bx, by, bz, timeout=3.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        items = world.find_entities_near(bx, by, bz, 4, type='minecraft:item')
        if any(i['item']['item_id'] in MINE_ITEMS for i in items):
            return items
        time.sleep(0.1)
    return []
```

#### Check nearby mob health

```python
import bot, world

pos = bot.position()
mobs = [e for e in world.find_entities_near(pos['x'], pos['y'], pos['z'], 16)
        if 'health' in e and e['type'] != 'minecraft:player']
for mob in mobs:
    print(mob['type'], f"{mob['health']:.1f}/{mob['max_health']:.1f} HP")
```

---

### Update strategy

The Java client sends `EntityUpdate` messages only when entities change - new arrivals, position/rotation changes, and removals.

On server disconnect, all entity data is cleared so stale entities never persist across reconnections.

---

## Light

### `get_light(x, y, z, use_disk=False, dimension="", bot="")`

Get the light levels at the specified block position.

**Parameters:**

- `x` (`float`) - X coordinate
- `y` (`float`) - Y coordinate
- `z` (`float`) - Z coordinate
- `use_disk` (`bool`, optional) - If `True` and the chunk is not loaded in memory, read light data from the saved `.mca` region file on disk (default: `False`)
- `dimension` (`str`, optional) - Dimension string. Defaults to the bot's current dimension. **Requires `use_disk=True`** - raises `ValueError` if set without it. If the specified dimension differs from the bot's current one, memory is skipped and disk is read directly.
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online; `ValueError` if `dimension` is set without `use_disk=True`

**Returns:** `dict` or `None` if the chunk is not loaded (and not on disk when `use_disk=True`)

| Key | Type | Description |
|-----|------|-------------|
| `block` | `int` | Block light level (0-15, from torches, lava, etc.) |
| `sky` | `int` | Raw sky light level (0-15, always 0 in nether/end) |

Light data is captured on chunk load and updated as the server sends light updates (e.g. from placing or removing light sources).

**Note:** `sky` is the raw stored sky light, not the internal sky light used for mob spawning calculations. Internal sky light also depends on time of day and weather, which are not factored in here.

```python
light = world.get_light(x, y, z)
if light:
    utils.log(f"Block light: {light['block']}, Sky light: {light['sky']}")

    # A position with no block light and no sky access is fully dark
    if light['block'] == 0 and light['sky'] == 0:
        utils.log("No light sources reach this block")

# Read light from a saved but unloaded chunk
light = world.get_light(5000, 64, 5000, use_disk=True)
```

---

## Block Solidity

### `is_solid(block_state, face=Direction.UP, bot="")`

Check if a block state has a fully solid face in the given direction. Useful for finding mob spawn surfaces, attachment points, and pathfinding.

**Parameters:**

- `block_state` (`str`) - Block state string (e.g. `"minecraft:stone"` or `"minecraft:oak_slab[type=top,waterlogged=false]"`)
- `face` (`Direction`, optional) - Face direction to check, defaults to `Direction.UP`
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `bool`, or `None` if the block registry is not loaded

### `Direction`

Enum for block face directions, matching Minecraft's internal direction ordering.

| Value | Description |
|-------|-------------|
| `Direction.DOWN` | Bottom face |
| `Direction.UP` | Top face |
| `Direction.NORTH` | North face (-Z) |
| `Direction.SOUTH` | South face (+Z) |
| `Direction.WEST` | West face (-X) |
| `Direction.EAST` | East face (+X) |

```python
# Check if a block has a solid top face (mob spawn surface)
block = world.get_block(x, y, z)
if world.is_solid(block):
    utils.log(f"{block} has a solid top face")

# Check a specific face
if world.is_solid("minecraft:oak_stairs[facing=north,half=bottom,shape=straight]", world.Direction.NORTH):
    utils.log("North face is solid")

# Find all spawnable locations in a radius
pos = bot.position()
cx, cy, cz = int(pos['x']), int(pos['y']), int(pos['z'])
dark_air = world.find_blocks("minecraft:air", cx, cy, cz, 64,
                             min_block_light=0, max_block_light=0)
spawnable = []
for (x, y, z) in dark_air:
    surface = world.get_block(x, y - 1, z)
    head = world.get_block(x, y + 1, z)
    if surface and world.is_solid(surface) and head == "minecraft:air":
        spawnable.append((x, y, z))
utils.log(f"Found {len(spawnable)} spawnable locations")
```

---

## Weather

### `get_weather(bot="")`

Get the current weather state.

**Parameters:**

- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `dict` or `None` if bot is offline

| Key | Type | Description |
|-----|------|-------------|
| `is_raining` | `bool` | Whether it is raining |
| `is_thundering` | `bool` | Whether there is a thunderstorm |
| `rain_level` | `float` | Rain intensity (0.0-1.0) |
| `thunder_level` | `float` | Thunder intensity (0.0-1.0) |

**Note:** Weather only applies to the overworld. In the nether or end, values will show no rain since those dimensions have no weather system.

```python
weather = world.get_weather()
if weather and weather['is_raining']:
    utils.log(f"It's raining (intensity: {weather['rain_level']:.2f})")
    if weather['is_thundering']:
        utils.log("Thunderstorm!")
```

---

## Notes

- **Chunk Loading**: Blocks can only be queried in loaded chunks. The client automatically loads chunks as the bot moves around.
- **Performance**: Block queries are O(1) for `get_block()`. Searches like `find_blocks()` are optimized but still need to scan blocks, so prefer smaller radii when possible.
- **Block State Format**: Block states are returned as strings in the format `"minecraft:block_name[property=value,...]"`. Use Python string operations to check block types.
