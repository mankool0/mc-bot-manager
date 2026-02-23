import world
import bot
import baritone
import time


class RecipeType:
    CRAFTING_SHAPED = "minecraft:crafting_shaped"
    CRAFTING_SHAPELESS = "minecraft:crafting_shapeless"


def _get_crafting_grid_slots(container_type):
    """Return slot indices for crafting grid based on container type."""
    if container_type == world.ContainerType.CRAFTING_TABLE:
        return {
            "result": 0,
            "grid": list(range(1, 10)),  # 3x3
            "player_inventory_start": 10,
            "grid_width": 3,
            "grid_height": 3
        }
    elif container_type == world.ContainerType.PLAYER_INVENTORY:
        return {
            "result": 0,
            "grid": [1, 2, 3, 4],  # 2x2
            "player_inventory_start": 5,
            "grid_width": 2,
            "grid_height": 2
        }
    else:
        raise ValueError(f"Unsupported container type for crafting: {container_type}")


def _can_craft_in_2x2(recipe):
    """Check if recipe can fit in a 2x2 player inventory grid."""
    if recipe.get('is_shapeless', False):
        return len(recipe.get('ingredients', [])) <= 4

    # For 2x2, all ingredients must be in top-left quadrant: slots 1,2,4,5
    valid_2x2_slots = {1, 2, 4, 5}
    for ingredient in recipe.get('ingredients', []):
        if ingredient.get('slot', 0) not in valid_2x2_slots:
            return False
    return True


def _plan_fits_in_2x2(plan, bot_name=""):
    """Check if all steps in a crafting plan can be done in 2x2 grid."""
    for step in plan.get('steps', []):
        recipe = world.get_recipe(step['output_item'], bot=bot_name)
        if not recipe or not _can_craft_in_2x2(recipe):
            return False
    return True


def _open_crafting_container(bot_name="", max_distance=128, use_player_inventory=False):
    """Open appropriate crafting interface. Returns container type to use."""
    # Check what's currently open
    container = world.get_container(bot=bot_name)

    if use_player_inventory:
        # 2x2 recipes can use player inventory or crafting table
        if not container:
            # Nothing open - use player inventory (container ID 0)
            return world.ContainerType.PLAYER_INVENTORY
        elif container.get('type') == world.ContainerType.CRAFTING_TABLE:
            # Crafting table is open - can use it
            return world.ContainerType.CRAFTING_TABLE
        else:
            # Something else is open - close it and use player inventory
            world.close_container(bot=bot_name)
            time.sleep(0.1)
            return world.ContainerType.PLAYER_INVENTORY
    else:
        # 3x3 recipes require crafting table
        if container and container.get('type') == world.ContainerType.CRAFTING_TABLE:
            # Crafting table already open
            return world.ContainerType.CRAFTING_TABLE

        # Close any open container
        if container:
            world.close_container(bot=bot_name)
            time.sleep(0.1)

        # Find and open crafting table
        crafting_table = world.find_nearest(["minecraft:crafting_table"],
                                            max_distance=max_distance, bot=bot_name)
        if not crafting_table:
            raise RuntimeError(f"No crafting table found within {max_distance} blocks")

        x, y, z = crafting_table

        # Find best position to stand next to the crafting table
        bot_pos = bot.position(bot_name) if bot_name else bot.position()
        best_pos = None
        best_dist = float('inf')

        for dx, dz in [(1, 0), (-1, 0), (0, 1), (0, -1)]:
            check_x = x + dx
            check_z = z + dz
            block_at_feet = world.get_block(check_x, y, check_z, bot=bot_name) if bot_name else world.get_block(check_x, y, check_z)
            block_at_head = world.get_block(check_x, y + 1, check_z, bot=bot_name) if bot_name else world.get_block(check_x, y + 1, check_z)

            if block_at_feet and block_at_head:
                if 'air' in block_at_feet and 'air' in block_at_head:
                    dist = ((check_x - bot_pos['x'])**2 + (y - bot_pos['y'])**2 + (check_z - bot_pos['z'])**2)**0.5
                    if dist < best_dist:
                        best_dist = dist
                        best_pos = (check_x, y, check_z)

        if best_pos:
            if bot_name:
                baritone.goto(best_pos[0], best_pos[1], best_pos[2], bot=bot_name)
            else:
                baritone.goto(best_pos[0], best_pos[1], best_pos[2])
        else:
            if bot_name:
                baritone.goto(x, z, bot=bot_name)
            else:
                baritone.goto(x, z)

        # Wait for pathfinding to start
        start_time = time.time()
        pathing_started = False
        while time.time() - start_time < 5:
            status = baritone.get_process_status(bot_name) if bot_name else baritone.get_process_status()
            if status.get('is_pathing', False):
                pathing_started = True
                break
            time.sleep(0.1)

        # Wait for pathfinding to complete (if it started)
        if pathing_started:
            max_wait = 60
            wait_start = time.time()
            while time.time() - wait_start < max_wait:
                status = baritone.get_process_status(bot_name) if bot_name else baritone.get_process_status()
                if not status.get('is_pathing', False):
                    break
                time.sleep(0.5)

        if bot_name:
            world.interact_block(x, y, z, bot=bot_name)
        else:
            world.interact_block(x, y, z)

        # Wait for container to be available
        max_wait = 5
        start_time = time.time()
        while time.time() - start_time < max_wait:
            container = world.get_container(bot=bot_name)
            if container and container.get('type') == world.ContainerType.CRAFTING_TABLE:
                return world.ContainerType.CRAFTING_TABLE
            time.sleep(0.05)
        raise RuntimeError("Failed to open crafting table (container not available)")


def _find_ingredient_in_inventory(ingredient, available_items):
    """Find an available item from ingredient's accepted items list."""
    for item_id in ingredient.get('items', []):
        if available_items.get(item_id, 0) > 0:
            return item_id
    return None


def _count_available_items(bot_name=""):
    """Count all items in inventory, returning {item_id: count}."""
    inventory = bot.inventory(bot_name)
    available = {}
    for inv_item in inventory:
        item_id = inv_item['item_id']
        available[item_id] = available.get(item_id, 0) + inv_item['count']
    return available


def get_missing_materials(item_id, count=1, bot_name=""):
    """Get missing materials for crafting. Returns None if recipe not found."""
    recipe = world.get_recipe(item_id, bot=bot_name)
    if not recipe:
        return None

    available = _count_available_items(bot_name).copy()
    total_missing = {}

    for ingredient in recipe.get('ingredients', []):
        acceptable_items = ingredient.get('items', [])
        needed_for_slot = count

        for item in acceptable_items:
            if needed_for_slot <= 0:
                break
            qty_available = available.get(item, 0)
            if qty_available > 0:
                to_use = min(qty_available, needed_for_slot)
                available[item] -= to_use
                needed_for_slot -= to_use

        if needed_for_slot > 0:
            missing_item = acceptable_items[0] if acceptable_items else 'unknown'
            total_missing[missing_item] = total_missing.get(missing_item, 0) + needed_for_slot

    return total_missing


def has_materials(item_id, count=1, bot_name=""):
    """Check if player has materials for recipe."""
    missing = get_missing_materials(item_id, count, bot_name)
    return missing is not None and len(missing) == 0


def craft_item(item_id, count=1, bot_name="", container_type=None):
    """Craft item using specified container type or currently open container."""
    recipe = world.get_recipe(item_id, bot=bot_name)
    if not recipe:
        raise ValueError(f"Unknown recipe: {item_id}")

    # Determine container type
    if container_type is None:
        container = world.get_container(bot=bot_name)
        if not container:
            raise RuntimeError("No container open and no container type specified")
        container_type = container['type']
        container_items = [item.copy() for item in container.get('items', [])]
    else:
        # Using player inventory (no container object needed)
        if container_type == world.ContainerType.PLAYER_INVENTORY:
            # Build container items from inventory
            inventory = bot.inventory(bot_name)
            container_items = []
            for inv_item in inventory:
                container_items.append({
                    'slot': inv_item['slot'],
                    'item_id': inv_item['item_id'],
                    'count': inv_item['count']
                })
            # Add empty slots for crafting grid (slots 1-4) if not present
            existing_slots = {item['slot'] for item in container_items}
            for slot in range(0, 5):  # Result slot 0 + crafting grid 1-4
                if slot not in existing_slots:
                    container_items.append({'slot': slot, 'item_id': 'minecraft:air', 'count': 0})
        else:
            # For other types, need actual container
            container = world.get_container(bot=bot_name)
            if not container or container['type'] != container_type:
                raise RuntimeError(f"Expected {container_type} container to be open")
            container_items = [item.copy() for item in container.get('items', [])]

    missing = get_missing_materials(item_id, count, bot_name)
    if missing:
        items_str = ", ".join(f"{cnt}x {itm}" for itm, cnt in missing.items())
        raise ValueError(f"Missing materials: {items_str}")

    slots = _get_crafting_grid_slots(container_type)

    if container_type == world.ContainerType.PLAYER_INVENTORY and not _can_craft_in_2x2(recipe):
        raise RuntimeError("Recipe requires 3x3 crafting table, not 2x2 player inventory")

    # Determine max batch size based on ingredient stack sizes
    max_batch_size = 64
    for ingredient in recipe.get('ingredients', []):
        if ingredient.get('items'):
            for item in ingredient['items']:
                info = world.get_item_info(item, bot=bot_name)
                if info:
                    max_batch_size = min(max_batch_size, info['max_stack_size'])

    available = _count_available_items(bot_name)

    # Clear crafting grid if needed
    items_in_grid = [i for i in container_items if i['slot'] in slots['grid'] and i['item_id'] != 'minecraft:air']
    if items_in_grid:
        for item in items_in_grid:
            world.click_slot(item['slot'], button=world.MouseButton.LEFT,
                           click_type=world.ClickType.QUICK_MOVE, bot=bot_name)
            item['item_id'] = 'minecraft:air'
            item['count'] = 0
            time.sleep(0.05)
        container = world.get_container(bot=bot_name)
        container_items = [item.copy() for item in container.get('items', [])]

    remaining_crafts = count

    while remaining_crafts > 0:
        current_batch = min(remaining_crafts, max_batch_size)

        for ingredient in recipe.get('ingredients', []):
            slot = ingredient['slot']
            needed_for_slot = current_batch
            current_in_slot = 0

            while current_in_slot < needed_for_slot:
                needed_now = needed_for_slot - current_in_slot

                item_id_to_place = _find_ingredient_in_inventory(ingredient, available)
                if not item_id_to_place:
                    raise RuntimeError(f"Ran out of ingredients for slot {slot} (needed {needed_now} more)")

                source_item = None
                source_idx = -1
                for idx, item in enumerate(container_items):
                    if (item['item_id'] == item_id_to_place and
                        item['slot'] not in slots['grid'] and
                        item['slot'] != slots['result']):
                        source_item = item
                        source_idx = idx
                        break

                if not source_item:
                    container = world.get_container(bot=bot_name)
                    container_items = [item.copy() for item in container.get('items', [])]
                    for idx, item in enumerate(container_items):
                        if (item['item_id'] == item_id_to_place and
                            item['slot'] not in slots['grid'] and
                            item['slot'] != slots['result']):
                            source_item = item
                            source_idx = idx
                            break

                    if not source_item:
                        raise RuntimeError(f"Could not find {item_id_to_place} in container slots!")

                item_slot = source_item['slot']
                stack_count = source_item['count']

                world.click_slot(item_slot, button=world.MouseButton.LEFT,
                               click_type=world.ClickType.PICKUP, bot=bot_name)
                time.sleep(0.05)

                to_place = min(needed_now, stack_count)

                if to_place == stack_count and current_in_slot == 0:
                    world.click_slot(slot, button=world.MouseButton.LEFT,
                                   click_type=world.ClickType.PICKUP, bot=bot_name)
                else:
                    for _ in range(to_place):
                        world.click_slot(slot, button=world.MouseButton.RIGHT,
                                       click_type=world.ClickType.PICKUP, bot=bot_name)
                time.sleep(0.05)

                if stack_count > to_place:
                    world.click_slot(item_slot, button=world.MouseButton.LEFT,
                                   click_type=world.ClickType.PICKUP, bot=bot_name)
                    time.sleep(0.05)
                    container_items[source_idx]['count'] -= to_place
                else:
                    container_items[source_idx]['item_id'] = 'minecraft:air'
                    container_items[source_idx]['count'] = 0

                current_in_slot += to_place
                available[item_id_to_place] -= to_place

        # Shift-click to craft batch
        world.click_slot(slots['result'], button=world.MouseButton.LEFT,
                        click_type=world.ClickType.QUICK_MOVE, bot=bot_name)
        time.sleep(0.15)

        remaining_crafts -= current_batch

    return True


def auto_craft(item_id, count=1, bot_name="", max_distance=128, keep_container_open=False, skip_material_check=False):
    """Auto-find crafting table and craft item."""
    recipe = world.get_recipe(item_id, bot=bot_name)
    if not recipe:
        raise ValueError(f"Unknown recipe: {item_id}")

    if not skip_material_check:
        missing = get_missing_materials(item_id, count, bot_name)
    else:
        missing = None

    if missing:
        items_str = ", ".join(f"{cnt}x {itm}" for itm, cnt in missing.items())
        raise ValueError(f"Missing materials: {items_str}")

    # Determine if we can use player inventory
    use_player_inventory = _can_craft_in_2x2(recipe)
    container_type = _open_crafting_container(bot_name, max_distance, use_player_inventory)

    try:
        result = craft_item(item_id, count, bot_name, container_type=container_type)

        # Close container if requested and one is actually open
        if not keep_container_open:
            container = world.get_container(bot=bot_name)
            if container:
                world.close_container(bot=bot_name)

        return result
    except Exception:
        # Clean up on error
        if not keep_container_open:
            container = world.get_container(bot=bot_name)
            if container:
                world.close_container(bot=bot_name)
        raise


def auto_craft_recursive(item_id, count=1, bot_name="", max_distance=128):
    """Recursively craft item and its dependencies using C++ planner."""
    plan = world.plan_recursive_craft(item_id, count, bot=bot_name)

    if not plan['success']:
        raise RuntimeError(f"Failed to plan crafting: {plan['error']}")

    use_player_inventory = _plan_fits_in_2x2(plan, bot_name)
    container_type = _open_crafting_container(bot_name, max_distance, use_player_inventory)

    try:
        for step in plan['steps']:
            craft_item_id = step['output_item']
            craft_count = step['times']

            max_retries = 10
            retry_delay = 0.1

            for attempt in range(max_retries):
                try:
                    craft_item(craft_item_id, craft_count, bot_name, container_type=container_type)
                    break
                except ValueError as e:
                    if "Missing materials" in str(e) and attempt < max_retries - 1:
                        time.sleep(retry_delay)
                        retry_delay = min(retry_delay * 1.5, 2.0)
                    else:
                        raise

            time.sleep(0.1)

        # Close container if one is open
        container = world.get_container(bot=bot_name)
        if container:
            world.close_container(bot=bot_name)

        return True

    except Exception as e:
        try:
            container = world.get_container(bot=bot_name)
            if container:
                world.close_container(bot=bot_name)
        except:
            pass
        raise RuntimeError(f"Failed to craft: {e}")
