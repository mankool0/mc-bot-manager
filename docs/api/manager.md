# manager module

The `manager` module is for **global scripts** - scripts that are not tied to a
single bot. They live in the top-level **Global Scripts** tab (not a bot's own
Scripts tab) and are the right place for cross-bot logic and manager-wide
features.

Its main use today is **custom columns** in the bot instances table. You give a
column a name and a function; the manager calls that function once per bot on a
per-column interval you choose and shows the returned value in that bot's row.

!!! note "Global scripts run without a current bot"
    In a global script, unqualified calls like `bot.health()` have no bot to
    target. Always pass a bot name, e.g. `bot.health(bot_name)`. Inside a column
    provider the current bot is set to the bot being computed, so both
    `bot.inventory()` and `bot.inventory(bot_name)` work there.

!!! warning "Global scripts only"
    The column functions are only available from global scripts. Calling
    `add_column`, `remove_column`, or `@manager.column` from a bot's own
    script raises `RuntimeError`.

## Custom Columns

### `column(name, interval=1.0)`

Decorator that registers a custom column. The decorated function is called with
a single `bot_name` argument for each bot; its return value (any value, shown via
`str()`) is displayed in that bot's cell. Return `None` to show `-`.

**Parameters:**

- `name` (`str`) - Column header text (also its identity for show/hide and persistence)
- `interval` (`float`) - How often the column recomputes, in seconds. Defaults
  to `1.0`; values below `0.05` (50 ms) are clamped up to it. Pick the slowest
  rate that still feels live.

```python
import manager
import bot

@manager.column("Good Pickaxes", interval=60)
def good_pickaxes(bot_name):
    items = bot.inventory(bot_name)
    if items is None:
        return None
    count = 0
    for item in items:
        if "pickaxe" not in item["item_id"]:
            continue
        max_damage = item.get("max_damage", 0)
        damage = item.get("damage", 0)
        if max_damage > 0 and (max_damage - damage) > 100:
            count += 1
    return count
```

### `add_column(name, provider, interval=1.0)`

Non-decorator form of [`column`](#columnname-interval10). Registers
`provider(bot_name)` under the given column name. Registering the same name
again replaces the provider (and its interval).

**Parameters:**

- `name` (`str`) - Column header text
- `provider` (`callable`) - Function taking `bot_name` and returning the cell value
- `interval` (`float`) - Recompute period in seconds (default `1.0`, floored at `0.05`)

```python
import manager
import server

def ping_cell(bot_name):
    info = server.get_info(bot_name)
    return f"{info.ping} ms" if info else None

manager.add_column("Ping", ping_cell, interval=5)
```

### `remove_column(name)`

Remove a previously added custom column.

**Parameters:**

- `name` (`str`) - Column name to remove

```python
manager.remove_column("Ping")
```

## How columns update

- Each column recomputes on its own `interval` (default one second, minimum
  50 ms). A fast health column and a slow once-a-minute inventory column
  coexist without the slow one being recomputed needlessly.
- A column's next run is scheduled from when its previous run *finished*, so a
  provider slower than its own interval never runs back-to-back.
- Providers run on a shared background thread, so a slow provider never freezes
  the UI - but it does delay other columns that come due while it runs. Faster
  columns are computed first each pass, and their values appear as soon as they
  are done; still, keep providers quick if you also run a near-realtime column.
- A newly registered column fills immediately, then settles into its interval.
- If a provider raises, the cell shows `!` and the error is written to the
  Global Scripts output console.
- Show/hide a custom column from the instances-table header right-click menu.
  Visibility and width are remembered per column name across restarts.

## Notes

- Global scripts are stored under `scripts/_global/` and have their own
  autorun checkbox, editor, Run/Stop buttons, and output console in the Global
  Scripts tab, just like per-bot scripts.
- A script that registers columns stays **running** (shown active) even after
  its body finishes - its columns keep updating in the background. Click **Stop**
  to remove its columns and end it. Reloading, renaming, or deleting the script
  (or calling `remove_column`) also removes them, and a script that stops with
  an error takes its columns with it. Tip: set the script to autorun so its
  columns come back automatically on the next launch.
- `utils.log()` and `utils.error()` from a global script go to the Global
  Scripts output console.
- Event handlers (`@on(...)`) are not delivered to global scripts; events are
  routed per bot. Use a bot's own Scripts tab for event-driven automation.
