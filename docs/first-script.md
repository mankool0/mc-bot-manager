# Your First Script

## Simple Chat Command

Create a script that responds to chat commands.

**Script:** `chat_commands.py`

```python
import bot
import baritone
import utils

@on("chat_message")
def handle_commands(sender, message, msg_type):
    if message == "!pos":
        pos = bot.position()
        bot.chat(f"Position: {pos['x']:.1f}, {pos['y']:.1f}, {pos['z']:.1f}")

    elif message == "!goto spawn":
        baritone.goto(0, 64, 0)
        bot.chat("Going to spawn!")

    elif message == "!stop":
        baritone.cancel()
        bot.chat("Stopped")
```

## How it Works

1. `@on("chat_message")` - Registers handler for chat messages
2. `handle_commands(sender, message, msg_type)` - Function called when chat received
3. Checks message content and responds accordingly

## Running

1. Save the script
2. Enable it in the Scripts tab
3. Click Run to start the script
4. Type `!pos`, `!goto spawn`, or `!stop` in-game

## Debugging

Use `utils.log()` to debug:

```python
@on("chat_message")
def handle_commands(sender, message, msg_type):
    utils.log(f"Received: {message} from {sender}")

    if message == "!pos":
        # ...
```

Logs appear in the bots console tab.
