# world module

World data queries and block interaction.

The world module provides access to chunk data collected from the Minecraft client, enabling block queries and world interaction. Chunks are automatically synchronized and cached in memory.

## Block Queries

### `get_block(x, y, z, bot="")`

Get the block state at the specified coordinates.

**Parameters:**

- `x` (`int`) - Block X coordinate
- `y` (`int`) - Block Y coordinate
- `z` (`int`) - Block Z coordinate
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `str` - Block state string (e.g., `"minecraft:stone"`, `"minecraft:chest[facing=north]"`), or `None` if chunk is not loaded or bot is offline

**Raises:** `RuntimeError` if bot not found or not online

```python
# Get block at specific position
block = world.get_block(100, 64, 200)
if block:
    print(f"Block at (100, 64, 200): {block}")

    # Check block type
    if "chest" in block:
        print("Found a chest!")
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

## Block Interaction

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
- `button` (`MouseButton`, optional) - Mouse button or hotbar index for SWAP (default: `LEFT`)
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

# Swap chest slot 0 with hotbar slot 8
world.click_slot(0, button=8, click_type=world.ClickType.SWAP)

# Right-click a slot (split stack)
world.click_slot(5, button=world.MouseButton.RIGHT)
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
| `max_stack_size` | `int` | Maximum stack size (usually 1, 16, or 64) |
| `max_damage` | `int` | Maximum durability (0 for non-damageable items) |

```python
info = world.get_item_info("minecraft:diamond_pickaxe")
if info:
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

### `get_light(x, y, z, bot="")`

Get the light levels at the specified block position.

**Parameters:**

- `x` (`float`) - X coordinate
- `y` (`float`) - Y coordinate
- `z` (`float`) - Z coordinate
- `bot` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `dict` or `None` if the chunk is not loaded

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
