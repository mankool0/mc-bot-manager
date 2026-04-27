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
