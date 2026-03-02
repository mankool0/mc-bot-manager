"""
Enchanting library for mc-bot-manager.

Provides enchantment database, chest scanning for enchanted books,
book selection, anvil cost optimization, and book collection.

Usage:
    import enchanting

    books = enchanting.scan_chests(radius=48)
    selected, missing = enchanting.select_books(books, "sword", {
        "minecraft:sharpness": 5,
        "minecraft:unbreaking": 3,
        "minecraft:mending": 1,
    })
    result = enchanting.optimize_anvil_order(selected)
    enchanting.collect_books(selected)
    enchanting.print_instructions(result)
"""

import bot
import world
import utils
import time

# =============================================================================
# Enchantment Database (Minecraft 1.21)
# =============================================================================

# Book multiplier = anvil cost per level when sacrificing a book.
# "group" = mutual exclusivity group (only one per group on an item).
# "incompatible" = set of enchantment IDs that conflict but can't be expressed via group (e.g. riptide <-> channeling).
# "curse" = True for curses (rejected unless explicitly desired).

ENCHANTMENTS = {
    # --- Melee ---
    "minecraft:sharpness":          {"items": {"sword", "axe"}, "max": 5, "mult": 1, "group": "damage"},
    "minecraft:smite":              {"items": {"sword", "axe", "mace"}, "max": 5, "mult": 1, "group": "damage"},
    "minecraft:bane_of_arthropods": {"items": {"sword", "axe", "mace"}, "max": 5, "mult": 1, "group": "damage"},
    "minecraft:knockback":          {"items": {"sword"}, "max": 2, "mult": 1},
    "minecraft:fire_aspect":        {"items": {"sword", "mace"}, "max": 2, "mult": 2},
    "minecraft:looting":            {"items": {"sword"}, "max": 3, "mult": 2},
    "minecraft:sweeping_edge":      {"items": {"sword"}, "max": 3, "mult": 2},

    # --- Tool ---
    "minecraft:efficiency":         {"items": {"pickaxe", "axe", "shovel", "hoe", "shears"}, "max": 5, "mult": 1},
    "minecraft:silk_touch":         {"items": {"pickaxe", "axe", "shovel", "hoe"}, "max": 1, "mult": 4, "group": "silk_fortune"},
    "minecraft:fortune":            {"items": {"pickaxe", "axe", "shovel", "hoe"}, "max": 3, "mult": 2, "group": "silk_fortune"},

    # --- Armor (all pieces) ---
    "minecraft:protection":             {"items": {"helmet", "chestplate", "leggings", "boots"}, "max": 4, "mult": 1, "group": "protection"},
    "minecraft:fire_protection":        {"items": {"helmet", "chestplate", "leggings", "boots"}, "max": 4, "mult": 1, "group": "protection"},
    "minecraft:blast_protection":       {"items": {"helmet", "chestplate", "leggings", "boots"}, "max": 4, "mult": 2, "group": "protection"},
    "minecraft:projectile_protection":  {"items": {"helmet", "chestplate", "leggings", "boots"}, "max": 4, "mult": 1, "group": "protection"},
    "minecraft:thorns":                 {"items": {"helmet", "chestplate", "leggings", "boots"}, "max": 3, "mult": 4},

    # --- Helmet ---
    "minecraft:aqua_affinity":  {"items": {"helmet"}, "max": 1, "mult": 2},
    "minecraft:respiration":    {"items": {"helmet"}, "max": 3, "mult": 2},

    # --- Boots ---
    "minecraft:feather_falling": {"items": {"boots"}, "max": 4, "mult": 1},
    "minecraft:depth_strider":   {"items": {"boots"}, "max": 3, "mult": 2, "group": "movement"},
    "minecraft:frost_walker":    {"items": {"boots"}, "max": 2, "mult": 2, "group": "movement"},
    "minecraft:soul_speed":      {"items": {"boots"}, "max": 3, "mult": 4},

    # --- Leggings ---
    "minecraft:swift_sneak":    {"items": {"leggings"}, "max": 3, "mult": 4},

    # --- Bow ---
    "minecraft:power":      {"items": {"bow"}, "max": 5, "mult": 1},
    "minecraft:punch":      {"items": {"bow"}, "max": 2, "mult": 2},
    "minecraft:flame":      {"items": {"bow"}, "max": 1, "mult": 2},
    "minecraft:infinity":   {"items": {"bow"}, "max": 1, "mult": 4, "group": "bow_special"},

    # --- Crossbow ---
    "minecraft:quick_charge": {"items": {"crossbow"}, "max": 3, "mult": 1},
    "minecraft:multishot":    {"items": {"crossbow"}, "max": 1, "mult": 2, "group": "crossbow_shot"},
    "minecraft:piercing":     {"items": {"crossbow"}, "max": 4, "mult": 1, "group": "crossbow_shot"},

    # --- Trident ---
    "minecraft:loyalty":    {"items": {"trident"}, "max": 3, "mult": 1, "group": "trident_move"},
    "minecraft:riptide":    {"items": {"trident"}, "max": 3, "mult": 2, "group": "trident_move", "incompatible": {"minecraft:channeling"}},
    "minecraft:channeling": {"items": {"trident"}, "max": 1, "mult": 4, "incompatible": {"minecraft:riptide"}},
    "minecraft:impaling":   {"items": {"trident"}, "max": 5, "mult": 2},

    # --- Fishing rod ---
    "minecraft:luck_of_the_sea": {"items": {"fishing_rod"}, "max": 3, "mult": 2},
    "minecraft:lure":            {"items": {"fishing_rod"}, "max": 3, "mult": 2},

    # --- Mace ---
    "minecraft:density":    {"items": {"mace"}, "max": 5, "mult": 1, "group": "damage"},
    "minecraft:breach":     {"items": {"mace"}, "max": 4, "mult": 2, "group": "damage"},
    "minecraft:wind_burst": {"items": {"mace"}, "max": 3, "mult": 2},

    # --- Universal ---
    "minecraft:unbreaking": {"items": {"sword", "pickaxe", "axe", "shovel", "hoe", "bow", "crossbow",
                                        "trident", "helmet", "chestplate", "leggings", "boots",
                                        "elytra", "shears", "fishing_rod", "shield", "mace"}, "max": 3, "mult": 1},
    "minecraft:mending":    {"items": {"sword", "pickaxe", "axe", "shovel", "hoe", "bow", "crossbow",
                                        "trident", "helmet", "chestplate", "leggings", "boots",
                                        "elytra", "shears", "fishing_rod", "shield", "mace"}, "max": 1, "mult": 2, "group": "bow_special"},

    # --- Curses ---
    "minecraft:binding_curse":   {"items": {"helmet", "chestplate", "leggings", "boots", "elytra"}, "max": 1, "mult": 4, "curse": True},
    "minecraft:vanishing_curse": {"items": {"all"}, "max": 1, "mult": 4, "curse": True},
}


def get_item_type(item_id):
    """Determine item type category from a Minecraft item ID."""
    name = item_id.replace("minecraft:", "")
    if name.endswith("_sword"):      return "sword"
    if name.endswith("_pickaxe"):    return "pickaxe"
    if name.endswith("_axe"):        return "axe"
    if name.endswith("_shovel"):     return "shovel"
    if name.endswith("_hoe"):        return "hoe"
    if name.endswith("_helmet"):     return "helmet"
    if name.endswith("_chestplate"): return "chestplate"
    if name.endswith("_leggings"):   return "leggings"
    if name.endswith("_boots"):      return "boots"
    if name == "turtle_shell":  return "helmet"
    if name == "bow":           return "bow"
    if name == "crossbow":      return "crossbow"
    if name == "trident":       return "trident"
    if name == "fishing_rod":   return "fishing_rod"
    if name == "elytra":        return "elytra"
    if name == "shears":        return "shears"
    if name == "shield":        return "shield"
    if name == "mace":          return "mace"
    return None


def enchant_applies(ench_id, item_type):
    """Check if an enchantment applies to a given item type."""
    data = ENCHANTMENTS.get(ench_id)
    if not data:
        return False
    return "all" in data["items"] or item_type in data["items"]


def is_curse(ench_id):
    """Check if an enchantment is a curse."""
    return ENCHANTMENTS.get(ench_id, {}).get("curse", False)


# =============================================================================
# Enchantment Parsing
# =============================================================================

def parse_enchantment(ench_str):
    """Parse 'minecraft:sharpness 5' into ('minecraft:sharpness', 5)."""
    parts = ench_str.strip().rsplit(" ", 1)
    if len(parts) == 2:
        return (parts[0], int(parts[1]))
    return (parts[0], 1)


def parse_item_enchantments(item):
    """Parse all enchantments from an item dict. Returns {id: level}."""
    enchants = {}
    for ench_str in item.get("enchantments", []):
        ench_id, level = parse_enchantment(ench_str)
        enchants[ench_id] = level
    return enchants


# =============================================================================
# Chest Scanner
# =============================================================================

def _wait_for_arrival(bot_name="", timeout=30):
    """Wait for baritone pathfinding to complete."""
    import baritone

    # Wait for pathing to start (up to 5s)
    start_time = time.time()
    pathing_started = False
    while time.time() - start_time < 5:
        status = baritone.get_process_status(bot=bot_name)
        if status.get("is_pathing", False):
            pathing_started = True
            break
        time.sleep(0.05)  # 50ms poll — catches brief is_pathing=True for short moves

    if not pathing_started:
        return False  # Baritone never started — no path found

    # Wait for pathing to complete
    deadline = time.time() + timeout
    while time.time() < deadline:
        status = baritone.get_process_status(bot=bot_name)
        if not status.get("is_pathing", False):
            return True
        time.sleep(0.5)
    return False


def _get_chest_type_and_facing(bx, by, bz, bot_name=""):
    """Parse chest block state to get type (single/left/right) and facing."""
    state = world.get_block(bx, by, bz, bot=bot_name)
    if not state or "chest" not in state:
        return None, None
    
    # Example state: "minecraft:chest[facing=north,type=left]"
    ctype = "single"
    if "type=left" in state: ctype = "left"
    elif "type=right" in state: ctype = "right"
    
    facing = "north"
    for f in ["north", "south", "east", "west"]:
        if f"facing={f}" in state:
            facing = f
            break
            
    return ctype, facing


def _find_chest_partner(bx, by, bz, bot_name=""):
    """Find the other half of a double chest."""
    ctype, facing = _get_chest_type_and_facing(bx, by, bz, bot_name)
    if not ctype or ctype == "single":
        return None
    
    # Offset to partner based on type and facing
    # North facing: Left is +X (East), Right is -X (West)
    # South facing: Left is -X (West), Right is +X (East)
    # West facing: Left is -Z (North), Right is +Z (South)
    # East facing: Left is +Z (South), Right is -Z (North)
    
    offsets = {
        "north": {"left": (1, 0, 0), "right": (-1, 0, 0)},
        "south": {"left": (-1, 0, 0), "right": (1, 0, 0)},
        "west": {"left": (0, 0, -1), "right": (0, 0, 1)},
        "east": {"left": (0, 0, 1), "right": (0, 0, -1)},
    }
    
    dx, dy, dz = offsets.get(facing, {}).get(ctype, (0, 0, 0))
    if dx == 0 and dy == 0 and dz == 0:
        return None
        
    return (bx + dx, by + dy, bz + dz)


def _get_groups(enchantments):
    """Get the set of exclusivity groups present in an enchantment dict."""
    groups = set()
    for ench_id in enchantments:
        group = ENCHANTMENTS.get(ench_id, {}).get("group")
        if group:
            groups.add(group)
    return groups


def _is_compatible(ench_id, target_enchantments):
    """Check if a sacrifice enchantment is compatible with the target's enchantments."""
    sac_data = ENCHANTMENTS.get(ench_id, {})
    sac_group = sac_data.get("group")
    sac_incompatible = sac_data.get("incompatible", set())

    for tgt_id in target_enchantments:
        if tgt_id == ench_id:
            continue
        # Explicit incompatibility (e.g. riptide <-> channeling)
        if tgt_id in sac_incompatible:
            return False
        tgt_data = ENCHANTMENTS.get(tgt_id, {})
        if ench_id in tgt_data.get("incompatible", set()):
            return False
        # Mutual exclusivity group (e.g. sharpness/smite/bane, silk_touch/fortune)
        if sac_group and sac_group == tgt_data.get("group"):
            return False
    return True


def _is_chest_blocked(bx, by, bz, bot_name=""):
    """Check if a chest is blocked by a solid block above it."""
    state = world.get_block(bx, by, bz, bot=bot_name)
    if not state or "chest" not in state or "barrel" in state:
        return False
    
    above = world.get_block(bx, by + 1, bz, bot=bot_name)
    if not above: return False
    
    # Standard transparent/non-full blocks that allow chests to open
    if any(t in above for t in ["air", "water", "lava", "glass", "leaves", "slab", "stair", "fence", "wall", "chest", "sign"]):
        return False
        
    return True


def _find_standing_pos(targets, bot_name="", reach=4.5):
    """
    Find a valid standing position for a container (which may have multiple blocks, e.g. double chest).
    Scans outward by horizontal distance, then Y levels.
    """
    if not isinstance(targets, list):
        targets = [targets]
        
    bp = bot.position(bot_name)
    
    # Filter out blocked chest halves
    valid_targets = [t for t in targets if not _is_chest_blocked(t[0], t[1], t[2], bot_name)]
    if not valid_targets:
        return None

    # Scan outward by horizontal distance (Manhattan-ish)
    for h_dist in range(1, 5):
        best_pos = None
        best_player_dist = float('inf')
        
        # Square perimeter at h_dist
        for dx in range(-h_dist, h_dist + 1):
            for dz in range(-h_dist, h_dist + 1):
                if max(abs(dx), abs(dz)) != h_dist:
                    continue
                
                # Use the first target block as a reference for horizontal offset
                ref_x, ref_y, ref_z = valid_targets[0]
                check_x, check_z = ref_x + dx, ref_z + dz
                
                # Scan Y range from block Y down
                for dy_off in range(1, -6, -1):
                    check_y = ref_y + dy_off
                    
                    block_below = world.get_block(check_x, check_y - 1, check_z, bot=bot_name)
                    block_at_feet = world.get_block(check_x, check_y, check_z, bot=bot_name)
                    block_at_head = world.get_block(check_x, check_y + 1, check_z, bot=bot_name)

                    if not (block_below and block_at_feet and block_at_head):
                        continue
                    if not ("air" in block_at_feet and "air" in block_at_head):
                        continue
                    if "air" in block_below:
                        continue

                    # Use Java raytrace to verify this position can see at least one target half
                    if not any(world.can_reach_block_from(check_x, check_y, check_z, tx, ty, tz, bot=bot_name)
                               for tx, ty, tz in valid_targets):
                        continue

                    dist = (check_x+0.5-bp['x'])**2 + (check_y-bp['y'])**2 + (check_z+0.5-bp['z'])**2
                    if dist < best_player_dist:
                        best_player_dist = dist
                        best_pos = (check_x, check_y, check_z)
        
        if best_pos:
            return best_pos
            
    return None


def _navigate_to_block(targets, bot_name="", reach=3.8):
    """Navigate to reach any of the target blocks, verified with Java raytrace."""
    import baritone
    if not isinstance(targets, list): targets = [targets]

    stand_pos = _find_standing_pos(targets, bot_name)
    if not stand_pos:
        return False

    bp = bot.position(bot_name)
    already_there = (abs(bp["x"] - (stand_pos[0] + 0.5)) < 0.2
                     and abs(bp["y"] - stand_pos[1]) < 0.1
                     and abs(bp["z"] - (stand_pos[2] + 0.5)) < 0.2)
    if not already_there:
        baritone.goto(stand_pos[0], stand_pos[1], stand_pos[2], bot=bot_name)
        if not _wait_for_arrival(bot_name):
            return False
        time.sleep(0.3)  # let player fully stop before raytrace

    return any(world.can_reach_block(tx, ty, tz, bot=bot_name) for tx, ty, tz in targets)


def scan_chests(radius=48, bot_name=""):
    """
    Find all chests within radius and catalog their enchanted books.
    """
    pos = bot.position(bot_name)
    if not pos:
        raise RuntimeError("Cannot get bot position")

    # Find all chest-type blocks
    cx, cy, cz = pos["x"], pos["y"], pos["z"]
    positions = world.find_blocks("minecraft:chest", cx, cy, cz, radius, bot=bot_name)
    positions += world.find_blocks("minecraft:trapped_chest", cx, cy, cz, radius, bot=bot_name)
    positions += world.find_blocks("minecraft:barrel", cx, cy, cz, radius, bot=bot_name)

    unique_positions = sorted(list(set((int(x), int(y), int(z)) for x, y, z in positions)))

    # Group halves of double chests
    containers = []
    seen = set()
    for p in unique_positions:
        if p in seen: continue
        partner = _find_chest_partner(p[0], p[1], p[2], bot_name)
        halves = [p]
        seen.add(p)
        if partner and partner in unique_positions:
            halves.append(partner)
            seen.add(partner)
        containers.append(halves)

    found_books = []
    scanned_container_ids = set()

    for halves in containers:
        # Use first half as primary interact target, but navigate logic knows about both
        bx, by, bz = halves[0]
        
        if not _navigate_to_block(halves, bot_name):
            continue

        # Open container (try first half, then second if it's a double chest), with retry
        opened = False
        for hx, hy, hz in halves:
            container = None
            for attempt in range(2):
                if attempt > 0:
                    time.sleep(0.5)
                world.interact_block(hx, hy, hz, bot=bot_name)

                first_open_time = None
                deadline = time.time() + 2.0
                while time.time() < deadline:
                    c = world.get_container(bot=bot_name)
                    if c is not None:
                        if first_open_time is None:
                            first_open_time = time.time()
                        elif time.time() - first_open_time >= 0.2:
                            container = c
                            break
                    time.sleep(0.05)

                if container:
                    break

            if container:
                opened = True
                break
        
        if not opened:
            utils.log(f"Failed to open container at {halves[0]}, skipping")
            continue

        container_id = container.get("id")
        if container_id is not None and container_id in scanned_container_ids:
            world.close_container(bot=bot_name)
            time.sleep(0.2)
            continue
        if container_id is not None:
            scanned_container_ids.add(container_id)

        for item in container.get("items", []):
            if item["item_id"] == "minecraft:enchanted_book":
                enchants = parse_item_enchantments(item)
                if enchants:
                    found_books.append({
                        "chest_pos": (bx, by, bz),
                        "halves": halves, # store all halves for collection
                        "slot": item["slot"],
                        "enchantments": enchants,
                        "display_name": item.get("display_name", ""),
                    })

        world.close_container(bot=bot_name)
        time.sleep(0.2)

    utils.log(f"Scan complete: found {len(found_books)} enchanted book(s)")
    return found_books


# =============================================================================
# Book Selector
# =============================================================================

def select_books(found_books, target_type, desired_enchants, max_per_enchant=None):
    """
    Select the optimal set of books covering all desired enchantments.

    Args:
        found_books: list from scan_chests()
        target_type: item type string (e.g., "sword", "boots")
        desired_enchants: dict {enchantment_id: desired_level}
        max_per_enchant: optional cap on how many selected books may carry each
            desired enchantment.  Pass an int to apply the same limit to all
            enchantments, or a dict {enchant_id: limit} for per-enchantment
            control.  Example: {"minecraft:mending": 1, "minecraft:protection": 1}
            prevents the selector from picking a second book that also has mending
            or protection, avoiding redundant XP cost at the anvil.

    Returns:
        (selected_books, missing_enchants)
        - selected_books: list of book dicts to collect
        - missing_enchants: list of enchantment IDs that couldn't be covered
    """
    # Build set of desired exclusivity groups
    desired_groups = set()
    for ench_id in desired_enchants:
        g = ENCHANTMENTS.get(ench_id, {}).get("group")
        if g:
            desired_groups.add(g)

    # Filter to valid books
    valid = []
    for book in found_books:
        enchants = book["enchantments"]

        # Reject undesired curses
        if any(is_curse(e) and e not in desired_enchants for e in enchants):
            continue

        # Reject if any enchantment is (a) applicable to target, (b) not desired,
        # and (c) not blocked by a desired exclusive group member
        bad = False
        for ench_id in enchants:
            if ench_id in desired_enchants:
                continue
            if not enchant_applies(ench_id, target_type):
                continue  # harmlessly ignored on target item
            # This enchantment WOULD apply — check if it's blocked by exclusivity
            group = ENCHANTMENTS.get(ench_id, {}).get("group")
            if group and group in desired_groups:
                continue  # blocked by a desired enchantment in the same group
            bad = True
            break
        if bad:
            continue

        # Which desired enchantments does this book cover?
        covers = {}
        for ench_id, level in enchants.items():
            if ench_id in desired_enchants:
                covers[ench_id] = level

        if covers:
            valid.append({**book, "_covers": covers})

    def _at_limit(book, pool):
        """True if selecting this book would exceed any per-enchant cap."""
        if max_per_enchant is None:
            return False
        for ench_id in book.get("_covers", {}):
            limit = (max_per_enchant if isinstance(max_per_enchant, int)
                     else max_per_enchant.get(ench_id))
            if limit is None:
                continue
            if sum(1 for s in pool if ench_id in s.get("_covers", {})) >= limit:
                return True
        return False

    # Greedy set cover: prefer books covering the most uncovered enchantments
    # at the best levels, with fewest wasted applicable enchants
    remaining = set(desired_enchants.keys())
    selected = []

    while remaining:
        best = None
        best_score = (-1, -1)

        for book in valid:
            if _at_limit(book, selected):
                continue
            relevant = {e: l for e, l in book["_covers"].items() if e in remaining}
            if not relevant:
                continue
            cover_count = len(relevant)
            level_score = sum(1 for e, l in relevant.items() if l >= desired_enchants[e])
            score = (cover_count, level_score)
            if score > best_score:
                best_score = score
                best = book

        if best is None:
            break

        selected.append(best)
        valid.remove(best)
        for ench_id in best["_covers"]:
            remaining.discard(ench_id)

    # Level-up: if any enchantment was covered below the desired level,
    # select additional books so they can be combined to reach it.
    for ench_id, desired_level in desired_enchants.items():
        if ench_id in remaining:
            continue

        have_level = 0
        for book in selected:
            if ench_id in book["_covers"]:
                have_level = max(have_level, book["_covers"][ench_id])

        if have_level >= desired_level:
            continue

        max_lvl = ENCHANTMENTS.get(ench_id, {}).get("max", desired_level)
        target = min(desired_level, max_lvl)
        if have_level >= target:
            continue

        candidates = [b for b in valid if ench_id in b.get("_covers", {})]
        candidates.sort(key=lambda b: (-b["_covers"][ench_id], len(b["enchantments"])))

        levels = [have_level]
        additional = []

        for book in candidates:
            if _at_limit(book, selected + additional):
                continue
            book_level = book["_covers"][ench_id]
            levels.append(book_level)
            additional.append(book)

            changed = True
            while changed:
                changed = False
                levels.sort(reverse=True)
                new_levels = []
                i = 0
                while i < len(levels):
                    if i + 1 < len(levels) and levels[i] == levels[i + 1]:
                        new_levels.append(min(levels[i] + 1, max_lvl))
                        i += 2
                        changed = True
                    else:
                        new_levels.append(levels[i])
                        i += 1
                levels = new_levels

            if max(levels) >= target:
                break

        for book in additional:
            selected.append(book)
            if book in valid:
                valid.remove(book)

    missing = list(remaining)
    return selected, missing


# =============================================================================
# Anvil Cost Optimizer
# =============================================================================

class AnvilItem:
    """An item (book or gear) in the anvil combining process."""

    def __init__(self, enchantments, work_count=0, is_book=True, name=""):
        self.enchantments = dict(enchantments)  # {id: level}
        self.work_count = work_count
        self.is_book = is_book
        self.name = name

    def penalty(self):
        return (2 ** self.work_count) - 1

    def to_state(self):
        enchs = tuple(sorted(self.enchantments.items()))
        return (enchs, self.work_count, self.is_book)

    def __repr__(self):
        return self.name or f"Item({list(self.enchantments.keys())})"


def _combine(target, sacrifice):
    """
    Simulate an anvil combine. Returns (cost, result_item) or (None, None).
    """
    # Base penalty cost
    penalty_cost = target.penalty() + sacrifice.penalty()
    
    ench_cost = 0
    result_enchants = dict(target.enchantments)
    changed = False
    
    for ench_id, sac_level in sacrifice.enchantments.items():
        if not _is_compatible(ench_id, target.enchantments):
            continue
            
        mult = ENCHANTMENTS.get(ench_id, {}).get("mult", 1)
        if not sacrifice.is_book:
            mult *= 2
            
        tgt_level = target.enchantments.get(ench_id, 0)
        
        if tgt_level < sac_level:
            result_enchants[ench_id] = sac_level
            ench_cost += sac_level * mult
            changed = True
        elif tgt_level == sac_level:
            max_lvl = ENCHANTMENTS.get(ench_id, {}).get("max", sac_level)
            if tgt_level < max_lvl:
                result_enchants[ench_id] = tgt_level + 1
                ench_cost += (tgt_level + 1) * mult
                changed = True
                
    if not changed:
        return None, None
        
    total_cost = penalty_cost + ench_cost
    if total_cost > 39:
        return None, None

    result = AnvilItem(
        enchantments=result_enchants,
        work_count=max(target.work_count, sacrifice.work_count) + 1,
        is_book=(target.is_book and sacrifice.is_book),
        name=f"({target.name}+{sacrifice.name})",
    )
    return total_cost, result


def optimize_anvil_order(selected_books, target_item_name="Item"):
    """
    Find the optimal anvil combining order for a set of books.
    """
    if not selected_books:
        return {"total_cost": 0, "steps": []}

    items = []
    for i, book in enumerate(selected_books):
        enchants = book["enchantments"] if isinstance(book, dict) else book.enchantments
        items.append(AnvilItem(enchants, work_count=0, is_book=True, name=f"Book{i+1}"))

    items.append(AnvilItem({}, work_count=0, is_book=False, name=target_item_name))

    # Phase 1: memoize only costs (keyed by enchantment state, not names).
    # Storing steps in the memo causes stale names when two items share the same
    # enchantment state but were produced via different combining paths.
    cost_memo = {}

    def _best_cost(pool):
        state = tuple(sorted(item.to_state() for item in pool))
        if state in cost_memo:
            return cost_memo[state]

        if len(pool) == 1:
            cost_memo[state] = 0
            return 0

        best = float('inf')
        for i in range(len(pool)):
            for j in range(len(pool)):
                if i == j:
                    continue
                tgt, sac = pool[i], pool[j]
                if not sac.is_book and tgt.is_book:
                    continue
                cost, combined = _combine(tgt, sac)
                if cost is None:
                    continue
                new_pool = tuple(pool[k] for k in range(len(pool)) if k != i and k != j) + (combined,)
                sub = _best_cost(new_pool)
                if sub != float('inf'):
                    best = min(best, cost + sub)

        cost_memo[state] = best
        return best

    initial_pool = tuple(items)
    total = _best_cost(initial_pool)
    if total == float('inf'):
        return None

    # Phase 2: reconstruct steps by following the optimal cost path.
    # Item names come from the actual pool items at each step, so they are
    # always consistent with the steps that preceded them.
    def _reconstruct(pool):
        if len(pool) == 1:
            return []
        state = tuple(sorted(item.to_state() for item in pool))
        target_cost = cost_memo[state]
        for i in range(len(pool)):
            for j in range(len(pool)):
                if i == j:
                    continue
                tgt, sac = pool[i], pool[j]
                if not sac.is_book and tgt.is_book:
                    continue
                cost, combined = _combine(tgt, sac)
                if cost is None:
                    continue
                new_pool = tuple(pool[k] for k in range(len(pool)) if k != i and k != j) + (combined,)
                new_state = tuple(sorted(item.to_state() for item in new_pool))
                if cost + cost_memo.get(new_state, float('inf')) == target_cost:
                    step = {
                        "target": tgt.name,
                        "sacrifice": sac.name,
                        "cost": cost,
                        "result": combined.name,
                    }
                    return [step] + _reconstruct(new_pool)
        return []

    steps = _reconstruct(initial_pool)
    return {"total_cost": total, "steps": steps}


# =============================================================================
# Book Collector
# =============================================================================

def collect_books(selected_books, bot_name=""):
    """
    Navigate to chests and pick up the selected books.

    Args:
        selected_books: list of book dicts with "chest_pos" and "slot"

    Returns True on success.
    """
    # Group by chest to minimize trips
    by_chest = {}
    for book in selected_books:
        key = book["chest_pos"]
        by_chest.setdefault(key, []).append(book)

    for chest_pos, books in by_chest.items():
        bx, by, bz = chest_pos
        halves = books[0].get("halves", [(bx, by, bz)])

        if not _navigate_to_block(halves, bot_name):
            utils.error(f"Could not reach chest at {chest_pos}")
            continue

        time.sleep(0.3)  # let player fully settle after navigation

        # Open chest — try each half (same as scan), with one retry per half
        container = None
        for hx, hy, hz in halves:
            for attempt in range(2):
                if attempt > 0:
                    time.sleep(0.5)
                world.interact_block(hx, hy, hz, bot=bot_name)

                first_open_time = None
                deadline = time.time() + 2.0
                while time.time() < deadline:
                    c = world.get_container(bot=bot_name)
                    if c is not None:
                        if first_open_time is None:
                            first_open_time = time.time()
                        elif time.time() - first_open_time >= 0.2:
                            container = c
                            break
                    time.sleep(0.05)

                if container:
                    break

            if container:
                break

        if not container:
            utils.error(f"Failed to open chest at {chest_pos}")
            continue

        for book in books:
            slot = book["slot"]
            # Verify book is still there
            found = any(
                item["slot"] == slot and item["item_id"] == "minecraft:enchanted_book"
                for item in container.get("items", [])
            )
            if not found:
                utils.error(f"Book at slot {slot} in chest {chest_pos} is gone!")
                continue

            world.click_slot(slot, button=world.MouseButton.LEFT,
                             click_type=world.ClickType.QUICK_MOVE, bot=bot_name)
            time.sleep(0.1)

        world.close_container(bot=bot_name)
        time.sleep(0.3)

    utils.log("Book collection complete")
    return True


# =============================================================================
# Output Helpers
# =============================================================================

def print_instructions(result):
    """Print step-by-step anvil combining instructions."""
    if result is None:
        utils.error("No valid combining order found (all exceed 39 levels)")
        return

    if not result["steps"]:
        utils.log("No anvil steps needed")
        return

    utils.log(f"\n=== Anvil Combining Instructions ===")
    utils.log(f"Total XP cost: {result['total_cost']} levels")
    utils.log(f"Steps: {len(result['steps'])}\n")

    for i, step in enumerate(result["steps"], 1):
        utils.log(f"  Step {i}: [{step['target']}] + [{step['sacrifice']}] = {step['cost']} levels")

    utils.log(f"\n  TOTAL: {result['total_cost']} levels")
    utils.log(f"=== End Instructions ===")


def print_book_summary(selected_books, desired_enchants):
    """Print a summary of selected books and what they cover."""
    utils.log(f"\nSelected {len(selected_books)} book(s):")
    for i, book in enumerate(selected_books, 1):
        ench_strs = []
        for eid, lvl in book["enchantments"].items():
            tag = " *" if eid in desired_enchants else ""
            ench_strs.append(f"{eid.replace('minecraft:', '')} {lvl}{tag}")
        utils.log(f"  Book{i}: {', '.join(ench_strs)}")
        utils.log(f"         Chest {book['chest_pos']} slot {book['slot']}")
