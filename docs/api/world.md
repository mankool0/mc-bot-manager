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

### `find_blocks(block_type, center_x, center_y, center_z, radius, bot="")`

Find all blocks of a specific type within a spherical radius.

This function only searches loaded chunks. Blocks in unloaded chunks will not be found.

**Parameters:**

- `block_type` (`str`) - Block type to search for (e.g., `"minecraft:diamond_ore"`)
- `center_x` (`float`) - Search center X coordinate
- `center_y` (`float`) - Search center Y coordinate
- `center_z` (`float`) - Search center Z coordinate
- `radius` (`int`) - Search radius in blocks
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
    Pass `step['recipe_id']` to `get_recipe()` when executing plan steps — do not re-look up by item ID, as there may be multiple recipes for the same output.

## Notes

- **Chunk Loading**: Blocks can only be queried in loaded chunks. The client automatically loads chunks as the bot moves around.
- **Performance**: Block queries are O(1) for `get_block()`. Searches like `find_blocks()` are optimized but still need to scan blocks, so prefer smaller radii when possible.
- **Block State Format**: Block states are returned as strings in the format `"minecraft:block_name[property=value,...]"`. Use Python string operations to check block types.
