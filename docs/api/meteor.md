# meteor module

!!! note "Requires a Meteor-enabled bot"
    Every function in this module requires the bot's mod build to include Meteor support (the
    `meteor` capability). A connected bot whose mod was built without Meteor, or that is running in
    a game with no Meteor Client installed, raises `RuntimeError: Bot does not support meteor`.
    Wrap calls in `try/except` to keep scripts portable across bot builds.

    A bot only reports its capabilities while connected, so for an offline or unknown bot the read
    functions (`get_setting`, `get_module`, `list_modules`) keep their usual "no data" return
    instead of raising - there is nothing to check against until the mod connects.

## Module Control

### `toggle(module, bot_name="")`

Toggle module on/off.

**Parameters:**

- `module` (`str`) - Module name
- `bot_name` (`str`, optional) - Bot name

```python
meteor.toggle("auto-totem")
```

### `enable(module, bot_name="")`

Enable module.

**Parameters:**

- `module` (`str`) - Module name
- `bot_name` (`str`, optional) - Bot name

```python
meteor.enable("auto-totem")
```

### `disable(module, bot_name="")`

Disable module.

**Parameters:**

- `module` (`str`) - Module name
- `bot_name` (`str`, optional) - Bot name

```python
meteor.disable("auto-totem")
```

## Settings

### `set_setting(module, setting, value, bot_name="")`

Set module setting.

**Parameters:**

- `module` (`str`) - Module name
- `setting` (`str`) - Setting path (use dots for nested settings)
- `value` - Setting value
- `bot_name` (`str`, optional) - Bot name

```python
meteor.set_setting("auto-totem", "health", 10)
meteor.set_setting("kill-aura", "Targeting.range", 4.5)
```

### `get_setting(module, setting, bot_name="")`

Get module setting value.

**Parameters:**

- `module` (`str`) - Module name
- `setting` (`str`) - Setting path
- `bot_name` (`str`, optional) - Bot name

**Returns:** Setting value

```python
health = meteor.get_setting("auto-totem", "health")
```

### Packet list settings

Packet list settings (e.g. Packet Canceller's `s2c-packets`) hold packet names whose format
depends on the Meteor build the bot is running:

- Older Meteor builds (1.21.x): class names like `HandSwingC2SPacket` or
  `PlayerMoveC2SPacket.PositionAndOnGround`
- Newer Meteor builds (PacketType-based, 26.1.2+): flow-prefixed ids like
  `serverbound/minecraft:swing`

Treat the names as opaque strings: read the current list, modify it, and write it back
unchanged. There is no cross-version translation - names from one Meteor generation
silently resolve to nothing on the other (unknown names are dropped without error), so
scripts that hardcode packet names are tied to the Meteor generation they were written for.

## Module Info

### `list_modules(bot_name="")`

List all available Meteor modules.

**Parameters:**

- `bot_name` (`str`, optional) - Bot name

**Returns:** `list[str]` of module names

```python
modules = meteor.list_modules()
for module in modules:
    print(module)
```

### `get_module(module, bot_name="")`

Get module information.

**Parameters:**

- `module` (`str`) - Module name
- `bot_name` (`str`, optional) - Bot name

**Returns:** `dict` with module information

```python
info = meteor.get_module("auto-totem")
```

## Friends

Meteor keeps its own friends list per instance - the list `kill-aura` and every other friend-aware
module reads - and it persists in that instance's `meteor-client/friends.nbt`.

Prefer these to `bot.chat(".friends add <player>")`: the command prints its own chat feedback on
every call, including `Already friends with that player.`, and gives a script no way to know which
of those it would be.

### `friends(bot_name="")`

The bot's friends, by name.

**Returns:** `list[str]`, or `None` if the bot has not reported its friends yet (a bot that has none
returns an empty list). The mod re-reads Meteor's list once a second and sends it whenever it
changed, so edits made in Meteor's own GUI show up here too. The last reported list is kept while
the bot is offline.

```python
friends = meteor.friends("bot2")
if friends is not None and "bot1" not in friends:
    meteor.add_friend("bot1", "bot2")
```

### `add_friend(player, bot_name="")`

Add a player to the bot's Meteor friends. A player who is already a friend is left alone - there is
no error and nothing is printed in the bot's chat. The name is matched case-insensitively, and the
friend's UUID is taken from the tab list when the player is on it.

Fire-and-forget: the bot applies the change and sends its new list back, so `friends()` catches up a
moment later.

### `remove_friend(player, bot_name="")`

Remove a player from the bot's Meteor friends. A player who is not a friend is left alone.
