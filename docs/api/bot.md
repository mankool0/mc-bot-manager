# bot module

Bot state queries and basic commands.

## State Queries

### `position(bot_name="")`

Get bot position.

**Returns:** `dict` with keys `x`, `y`, `z`, or `None` if bot is offline

### `rotation(bot_name="")`

Get the bot's view rotation in degrees.

**Parameters:**

- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `dict` with `yaw` and `pitch` (`float`), or `None` if bot is offline

Yaw: `0` south, `90` west, `180`/`-180` north, `-90` east. Pitch: `-90` straight up, `0` level, `90` straight down.

```python
rot = bot.rotation()
if rot:
    utils.log(f"yaw={rot['yaw']:.1f} pitch={rot['pitch']:.1f}")
```

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

### `rotate(yaw, pitch, bot_name="")`

Set the bot's view rotation. Same convention as `rotation()`: yaw `0` south, `90` west, `180` north, `-90` east; pitch `-90` up, `0` level, `90` down.

**Parameters:**

- `yaw` (`float`) - Horizontal rotation in degrees
- `pitch` (`float`) - Vertical rotation in degrees
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
bot.rotate(90, 0)  # Face west, level
bot.rotate(0, 90)  # Look straight down

# Turn 45 degrees to the right of the current heading
rot = bot.rotation()
bot.rotate(rot["yaw"] + 45, rot["pitch"])
```

### `use_item(hand=bot.Hand.MAIN, bot_name="")`

Use the item held in a hand once, like a single tap of right-click with nothing targeted: throw an ender pearl or snowball, cast a fishing rod, splash a potion, empty a bucket. Aim first with `bot.rotate()` or `world.look_at()`.

This does not work for items that must be held. Eating, drinking, drawing a bow or crossbow, and raising a shield all start on the tap and are released again on the next tick, so use `hold_use()` for those. To right-click a block use `world.interact_block()`.

**Parameters:**

- `hand` (`bot.Hand`, optional) - `bot.Hand.MAIN` (default) or `bot.Hand.OFF`
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
bot.select_slot(3)
bot.use_item()  # Right-click with the main hand
bot.use_item(bot.Hand.OFF)  # Right-click with the offhand
```

### `hold_use(enabled, duration_ticks=0, bot_name="")`

Hold or release the right-click use button in-game. While enabled the client keeps the use key pressed every tick, so the game itself starts, continues and completes the use exactly as for a player holding the button: eating and drinking finish, a bow charges until release, a shield stays up. Main hand is tried first, then the offhand, as in-game.

With nothing in progress the game re-triggers the use every four ticks, so holding past the end of an eat starts on the next item in the stack. That second use is cancelled by the release without consuming anything as long as it has not run to completion, which is why the example below holds for 40 ticks to eat exactly one item (eating takes 32).

**Parameters:**

- `enabled` (`bool`) - `True` to start holding use, `False` to release
- `duration_ticks` (`int`, optional) - Auto-release after this many game ticks. `0` holds until an explicit `False` call (default: `0`)
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
# Eat one item from the selected hotbar slot
bot.select_slot(food_slot)
bot.hold_use(True, duration_ticks=40)

# Draw a bow for a second, then release the arrow
world.look_at_entity(target["entity_id"])
bot.hold_use(True, duration_ticks=20)

# Keep a shield up until told otherwise
bot.hold_use(True)
# ... later:
bot.hold_use(False)
```

### `get_hold_use(bot_name="")`

Query whether the client is currently holding the use button.

**Returns:** `bool` - `True` if use is being held, `False` otherwise

**Parameters:**

- `bot_name` (`str`, optional) - Bot name, defaults to current bot

```python
if bot.get_hold_use():
    utils.log("Still eating")
```

### `drop_item(drop_all=False, bot_name="")`

Drop the item in the selected hotbar slot: one item, or the whole stack with `drop_all=True`. Same as pressing Q (or Ctrl+Q) in-game. When the slot is empty the client replies `No item to drop` in the bot console; nothing is raised.

**Parameters:**

- `drop_all` (`bool`, optional) - Drop the entire stack instead of a single item (default: False)
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
bot.select_slot(0)
bot.drop_item()  # Drop one item
bot.drop_item(drop_all=True)  # Drop the whole stack
```

### `server(bot_name="")`

Get server address.

**Returns:** `str` or `None`

### `singleplayer_world(bot_name="")`

Get the singleplayer world name. Returns `None` if connected to a multiplayer server.

**Returns:** `str` or `None`

### `is_singleplayer(bot_name="")`

Check if the bot is connected to a local/singleplayer world.

**Returns:** `bool`

### `account(bot_name="")`

Get account username.

**Returns:** `str` or `None`

### `data_version(bot_name="")`

Get the Minecraft [data version](https://minecraft.wiki/w/Data_version) of the client this bot
runs -- `4189` for 1.21.4, `4790` for 26.1.2. Reported by the mod in its handshake, so an online
bot always has one.

This is the **client's** version, not the server's. Under a translating proxy (ViaVersion and
friends) they differ, and it is the client's that describes the world the bot sees: block and item
names come from the client's own registry, so this is the vocabulary `world.export_sections()`
palettes and `world.get_block()` names are written in.

**Returns:** `int` or `None`

### `uptime(bot_name="")`

Get bot uptime in seconds.

**Returns:** `int` or `None`

### `proxy(bot_name="")`

Get proxy configuration and health state.

**Returns:** `dict` or `None` if no proxy is configured (host is empty)

Keys:

- `enabled` (`bool`) - Whether the proxy is currently enabled
- `host` (`str`) - Proxy host address
- `port` (`int`) - Proxy port
- `type` (`str`) - Proxy type: `"SOCKS4"` or `"SOCKS5"`
- `username` (`str`) - Username (empty string if not set)
- `password` (`str`) - Password (empty string if not set)
- `health` (`str`) - One of: `"Unknown"`, `"Alive"`, `"Dead"`

```python
p = bot.proxy()
if p and p["health"] == "Dead":
    utils.log("Proxy is down")
```

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

### `resync_inventory(bot_name="")`

Force a full inventory resync from the server. Sends a stateId mismatch packet to trigger `sendAllDataToRemote` server-side, which sends back the authoritative inventory state.

Only works when no container is currently open. Raises if a container is open.

**Raises:** `RuntimeError` if bot not found, not online, or a container is open

```python
world.close_container()
bot.resync_inventory()
time.sleep(0.2)  # wait for server response
items = bot.inventory()
```

---

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

!!! warning "Item dict fields are sparse"
    A field is only present when it is meaningful: `damage`/`max_damage` are
    absent for undamaged/undamageable items, `enchantments` is absent when there
    are none, etc. Always read optional fields with `item.get("field", default)`
    - `item["damage"]` raises `KeyError` on a full-durability tool. 

### `get_cursor_item(bot_name="")`

Get the item currently held on the mouse cursor (the stack picked up by a container
click). Returns an [item dict](#item-dict) with `slot` set to `-1`; when the cursor is
empty, `item_id` is `"minecraft:air"` and `count` is `0`.

**Returns:** `dict` or `None` if the bot is not found or not online

```python
world.click_slot(0)
cursor = bot.get_cursor_item()
if cursor and cursor["item_id"] != "minecraft:air":
    utils.log(f"Holding {cursor['item_id']} x{cursor['count']}")
```

### `get_screen(bot_name="")`

Get full screen dump as a `ScreenState` object.

**Returns:** `ScreenState` or `None` if no screen is open (in-game)

Screen and widget class names are Mojang names on every supported Minecraft version. Obfuscated
versions (1.21.x) hand the mod intermediary names like `net.minecraft.class_419` at runtime, and it
maps them back, so matching on `"DeathScreen" in screen.screen_class` works everywhere. Class names
of screens added by other mods are reported as-is.

**`ScreenState` attributes:**

| Attribute | Type | Description |
|---|---|---|
| `id` | `str` | Stable screen ID - pass to `world.click_widget()` |
| `screen_class` | `str` | Fully qualified Mojang class name, e.g. `net.minecraft.client.gui.screens.multiplayer.JoinMultiplayerScreen` |
| `title` | `str` | Display title of the screen |
| `width` | `int` | Screen width in pixels |
| `height` | `int` | Screen height in pixels |
| `widgets` | `list[GuiWidget]` | All interactive widgets (buttons, edit boxes, sliders, etc.) - includes widgets nested inside list-based screens like Video Settings |
| `slots` | `list[GuiSlot]` | Container slots (only present for inventory-like screens) |

**`GuiWidget` attributes:**

| Attribute | Type | Description |
|---|---|---|
| `index` | `int` | Widget index - pass to `world.click_widget()` |
| `type` | `str` | `"Button"`, `"EditBox"`, `"Checkbox"`, `"ListEntry"`, `"SignLine"`, or simple class name for others |
| `class_name` | `str` | Fully qualified Mojang class name |
| `x`, `y` | `int` | Screen pixel coordinates |
| `width`, `height` | `int` | Widget dimensions |
| `active` | `bool` | Whether the widget can be interacted with |
| `visible` | `bool` | Whether the widget is rendered |
| `text` | `str` | Button label or widget text |
| `edit_value` | `str` | Current text (EditBox only) |
| `edit_editable` | `bool` | Whether the EditBox can be edited |
| `selected` | `bool` | Whether this widget is currently focused/selected (e.g. selected entry in the multiplayer or world list) |

**`GuiSlot` attributes:**

| Attribute | Type | Description |
|---|---|---|
| `index` | `int` | Slot index - pass to `world.click_slot()` |
| `x`, `y` | `int` | Screen pixel coordinates |
| `active` | `bool` | Whether the slot is active |
| `item_id` | `str` | Item ID (e.g. `"minecraft:diamond"`, `"minecraft:air"` if empty) |
| `count` | `int` | Stack count |
| `display_name` | `str` | Display name |
| `damage` | `int` | Current durability damage |
| `max_damage` | `int` | Max durability (0 if not damageable) |
| `enchantments` | `dict[str, int]` | Enchantment ID to level |
| `repair_cost` | `int` | Anvil repair cost |

```python
# Click a button by label on any screen
screen = bot.get_screen()
if screen is not None:
    for w in screen.widgets:
        if w.text == "Multiplayer" and w.active:
            world.click_widget(screen.id, w.index)
            break

# Auction house: slots act as buttons, items describe what they do
screen = bot.get_screen()
if screen is not None:
    for slot in screen.slots:
        if "Buy" in slot.display_name:
            world.click_slot(slot.index)
            break
```

### `open_game_menu(bot_name="")`

Open the game/pause menu, equivalent to pressing ESC while in-game.

**Parameters:**

- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

**Note:** The client performs additional checks before opening the menu (screen already open, not in a world) and will send a failure response if these conditions aren't met.

```python
bot.open_game_menu()
```

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

## Game Window

The manager can tile bot windows automatically (Settings -> Bots -> Game Windows); these calls give
scripts the same control. See [Bot Windows](../bot-windows.md) for how windows are identified and
what each platform supports.

All rectangles are the window's **outer frame** (decorations included) in pixels, relative to the
top-left of a monitor's **work area** (the monitor minus panels/taskbar).

### `window(bot_name="")`

Get the game window's placement.

**Returns:** `WindowState` or `None` if the bot is offline or did not answer within 3s.

**`WindowState` attributes:**

| Attribute | Type | Description |
|---|---|---|
| `platform` | `str` | GLFW platform: `"x11"`, `"wayland"`, `"win32"`, `"cocoa"` |
| `can_move` | `bool` | Whether the window can be positioned. `False` on native Wayland |
| `monitor` | `str` | Name of the monitor the window is on |
| `x`, `y` | `int` | Frame position relative to that monitor's work area |
| `width`, `height` | `int` | Frame size |
| `minimized` | `bool` | Iconified |
| `focused` | `bool` | Has keyboard focus |
| `visible` | `bool` | Mapped. `False` while the mod is still holding a new window back for the manager's placement, or after `set_window(visible=False)` |
| `monitors` | `list[Monitor]` | All monitors the game sees |

**`Monitor` attributes:**

| Attribute | Type | Description |
|---|---|---|
| `name` | `str` | Monitor name as the game reports it (RandR/wl_output name on Linux such as `DP-1`, adapter name on Windows) |
| `primary` | `bool` | Primary monitor |
| `x`, `y`, `width`, `height` | `int` | Full monitor rect in screen coordinates |
| `work_x`, `work_y`, `work_width`, `work_height` | `int` | Work area in screen coordinates |

```python
w = bot.window()
if w:
    utils.log(f"{w.monitor}: {w.width}x{w.height} at ({w.x}, {w.y}), minimized={w.minimized}")
    for m in w.monitors:
        utils.log(f"  {m.name}{' (primary)' if m.primary else ''}: work area {m.work_width}x{m.work_height}")
```

### `set_window(x=None, y=None, width=None, height=None, monitor="", minimized=None, visible=None, bot_name="")`

Move, resize, (un)minimize or show/hide the game window. Arguments left as `None` keep their
current value; changes are applied in the order hide, move/resize, show, minimize.

- `x`, `y` (`int`) - Frame position relative to the work area of `monitor`
- `width`, `height` (`int`) - Frame size
- `monitor` (`str`) - Target monitor name from `window().monitors`. Empty keeps the current monitor; with no `x`/`y` the window keeps its offset within the work area when moved to another monitor
- `minimized` (`bool`) - `True` iconifies, `False` restores
- `visible` (`bool`) - `False` unmaps the window entirely (no taskbar entry on most desktops; the game keeps running), `True` maps it again

**Returns:** the resulting `WindowState`, or `None` on timeout. Raises if the bot is not online.

Positioning is ignored (with a warning in the game log) on native Wayland and while the game is
fullscreen; resizing and minimizing still work.

```python
# Quarter of the primary monitor, top-right
w = bot.window()
primary = next(m for m in w.monitors if m.primary)
bot.set_window(x=primary.work_width // 2, y=0,
               width=primary.work_width // 2, height=primary.work_height // 2,
               monitor=primary.name)

# Park it out of the way
bot.set_window(minimized=True)
```

## Bot Control

### `start(bot_name="")`

Start the bot.

Launches are serialized: the manager sends one launch command to Prism at a
time and only sends the next once the previous bot has connected to the
manager (or failed - crashed, could not launch, or hit the startup timeout).
Calling `start()` on many bots in a loop is fine; each one is queued and shown
as `Queued #n` in the manager until its turn. Prism refreshes the account
token on every launch, and concurrent refreshes fail, which is why a burst of
simultaneous launches is never sent.

**Parameters:**

- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found

```python
bot.start()  # Start current bot
bot.start("bot2")  # Start specific bot

# Start a fleet - they launch one after another
for name in ["bot1", "bot2", "bot3"]:
    bot.start(name)
for name in ["bot1", "bot2", "bot3"]:
    bot.wait_for_online(timeout=300, bot_name=name)
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

### `wait_for_online(timeout=60.0, bot_name="")`

Block until the bot is Online. `bot.start()` returns as soon as the launch is
queued; use this to wait for the game client to actually connect. A launch
that never connects is marked Error 2 minutes after its launch command is
sent to Prism - time spent waiting in the launch queue does not count, so a
bot behind several others may need a `timeout` well above 60 seconds.

**Parameters:**

- `timeout` (`float`, optional) - Seconds to wait, default 60
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Returns:** `bool` - `True` when online; `False` on timeout, on a startup
Error, or if the script is stopped while waiting

**Raises:** `RuntimeError` if bot not found

```python
bot.start("Repairer")
if bot.wait_for_online(timeout=120, bot_name="Repairer"):
    comms.send("Repairer", {"cmd": "collect_pickaxes"})
else:
    utils.error("Repairer failed to come online")
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

### `connect(address, bot_name="")`

Connect the bot to a Minecraft server. If the bot is already on a server it leaves that one first.

**Parameters:**

- `address` (`str`) - Server address, `host` or `host:port`
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online, if `address` is empty, or if the bot's proxy is unreachable

```python
bot.connect("play.example.com")
bot.connect("127.0.0.1:25565", "bot2")
```

### `disconnect(reason="", bot_name="")`

Disconnect the bot from its current server. The bot process keeps running and can `connect()` again. When the bot is not on a server the client replies `Not connected to any server` in the bot console; nothing is raised.

**Parameters:**

- `reason` (`str`, optional) - Shown on the client's disconnect screen
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
bot.disconnect()
bot.disconnect("Done for today")
```

### `manager_command(command, bot_name="")`

Send a raw manager console command. Every console command has a typed function, so prefer those: `chat`, `connect`, `disconnect`, `rotate`, `select_slot`, `use_item`, `drop_item`, `stop`, `world.look_at`, `world.look_at_entity` and the `meteor` module. Typed functions validate their arguments and raise on a bad call; this one only logs a warning. The command line is split on spaces with shell-style quoting, so quotes in the text are consumed.

**Parameters:**

- `command` (`str`) - Manager command line, as typed in the bot console
- `bot_name` (`str`, optional) - Bot name, defaults to current bot

**Raises:** `RuntimeError` if bot not found or not online

```python
bot.manager_command("hotbar 3")  # Same as bot.select_slot(3)
bot.manager_command("meteor list", "bot2")  # Send to specific bot
```
