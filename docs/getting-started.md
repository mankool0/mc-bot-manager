# Getting Started

## Script Location

Scripts are stored in `scripts/<botname>/` directories.

Each bot has its own script directory created automatically.

## Creating a Script

1. Navigate to the Scripts tab
2. Click "New"
3. Enter script name (e.g., `my_script.py`)
4. Write your code in the editor
5. Click "Save"

## Running Scripts

**Auto-run (Event-driven):**
- Check the checkbox next to the script name
- Script loads and registers event handlers automatically

**Manual run (Imperative):**
- Select the script
- Click "Run"
- Script executes until completion or manual stop

## Script Structure

**Event-driven:**
```python
@on("chat_message")
def handler(sender, message, msg_type):
    # Runs when event fires
    pass
```

**Imperative:**
```python
import bot
import time

# Runs immediately
while True:
    pos = bot.position()
    time.sleep(5)
```

## Importing Modules

```python
import bot
import baritone
import meteor
import utils
```

## Next Steps

- [Write your first script](first-script.md)
- [Learn about events](event-handlers.md)
- [View examples](examples.md)
- [API reference](api/index.md)
