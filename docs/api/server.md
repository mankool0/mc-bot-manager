# server module

Server metadata and tab list.

## Functions

### `get_info(bot_name="")`

Get server metadata.

**Returns:** `ServerInfo` object or `None` if bot is offline

**`ServerInfo` attributes:**

| Attribute | Type | Description |
|-----------|------|-------------|
| `address` | `str` | Server IP/address |
| `motd` | `str` | Message of the day |
| `ping` | `int` | Bot's own latency in ms |
| `version` | `str` | Server version string (e.g. `"Velocity 1.7.2-26.1.2"`) |
| `players_online` | `int` | Current online player count |
| `players_max` | `int` | Maximum player slots |

**Example:**

```python
import server
import utils

info = server.get_info()
if info:
    utils.log(f"{info.address} - {info.motd}")
    utils.log(f"Ping: {info.ping}ms | Players: {info.players_online}/{info.players_max}")
    utils.log(f"Version: {info.version}")
```

---

### `get_player_list(bot_name="")`

Get the full server tab list. Includes all online players regardless of render distance.

**Returns:** `list[TabListPlayer]` - empty list if bot is offline or tab list not yet received

**`TabListPlayer` attributes:**

| Attribute | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Player name |
| `uuid` | `str` | Player UUID string |
| `ping` | `int` | Player latency in ms |
| `gamemode` | `Gamemode` | Player gamemode |
| `display_name` | `str` | Custom tab-list display name (empty if not set by server) |

### `Gamemode` enum

| Value | Int |
|-------|-----|
| `server.Gamemode.SURVIVAL` | 0 |
| `server.Gamemode.CREATIVE` | 1 |
| `server.Gamemode.ADVENTURE` | 2 |
| `server.Gamemode.SPECTATOR` | 3 |

**Example:**

```python
import server
import utils

players = server.get_player_list()
utils.log(f"{len(players)} players online:")
for p in players:
    name = p.display_name if p.display_name else p.name
    utils.log(f"  {name} ({p.gamemode}) - {p.ping}ms")
```

---

### `get_stats(bot_name="")`

Get the bot's Minecraft statistics for the server/world it is currently on (the same
data shown on the vanilla Statistics screen: blocks mined, distance walked, playtime,
deaths, mob kills, items crafted/used/dropped/picked up, and so on).

Statistics are not streamed continuously. This call asks the server for a copy and
blocks until the reply arrives, normally only a few milliseconds. If the server does not
respond within 5 seconds the call times out and falls back to the cache (see Returns).
It is cheap enough for periodic use (e.g. a custom column), but every call blocks for one
round-trip, so avoid calling it in a tight loop.

**Returns:** nested `dict` `{category: {key: value}}`, or `None` if the bot is offline
(or the request times out before any snapshot has ever been fetched). If a request times
out but an earlier call succeeded, the last cached snapshot is returned.

- `category` - the stat type id, one of `minecraft:custom`, `minecraft:mined`,
  `minecraft:crafted`, `minecraft:used`, `minecraft:broken`, `minecraft:picked_up`,
  `minecraft:dropped`, `minecraft:killed`, `minecraft:killed_by`.
- `key` - the value id within that category, e.g. `minecraft:play_time`,
  `minecraft:stone`, `minecraft:zombie`. See the [Minecraft Wiki][mc-stats] for the full
  list of keys per category.
- `value` - an `int`. Only non-zero statistics are included.

[mc-stats]: https://minecraft.wiki/w/Statistics

!!! note "Custom stat units"
    `minecraft:custom` values use Minecraft's raw internal units: times such as
    `minecraft:play_time` are in **ticks** (20 ticks = 1 second), distances such as
    `minecraft:walk_one_cm` are in **centimetres**, and `minecraft:damage_dealt` /
    `minecraft:damage_taken` are scaled by 10 (so 15.0 hearts of damage reads as `150`).

**Example:**

```python
import server
import utils

stats = server.get_stats()
if stats:
    custom = stats.get("minecraft:custom", {})
    play_ticks = custom.get("minecraft:play_time", 0)
    utils.log(f"Playtime: {play_ticks / 20 / 3600:.1f} hours")
    utils.log(f"Deaths: {custom.get('minecraft:deaths', 0)}")
    utils.log(f"Distance walked: {custom.get('minecraft:walk_one_cm', 0) / 100:.0f} m")

    # Top 5 mined blocks
    mined = stats.get("minecraft:mined", {})
    for block, count in sorted(mined.items(), key=lambda kv: kv[1], reverse=True)[:5]:
        utils.log(f"  {block}: {count}")
```
