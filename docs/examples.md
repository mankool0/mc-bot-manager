# Examples

## Event Handler

Respond to chat messages:

```python
import bot

@on("chat_message")
def handle_chat(sender, message, msg_type):
    if message == "!hello":
        bot.chat(f"Hello {sender}!")
```

## Query Bot State

Get bot information:

```python
import bot

pos = bot.position()
health = bot.health()
hunger = bot.hunger()
dim = bot.dimension()

print(f"Position: {pos['x']}, {pos['y']}, {pos['z']}")
print(f"Health: {health}, Hunger: {hunger}")
print(f"Dimension: {dim}")
```

## Control Baritone

Navigate and mine:

```python
import baritone

# Go to coordinates
baritone.goto(100, 64, 200)

# Follow player
baritone.follow("PlayerName")

# Mine blocks
baritone.mine("diamond_ore")

# Cancel task
baritone.cancel()
```

## Control Meteor

Enable modules and configure settings:

```python
import meteor

# Enable module
meteor.enable("auto-totem")

# Configure setting
meteor.set_setting("auto-totem", "health", 10.0)

# Toggle module
meteor.toggle("kill-aura")

# Disable module
meteor.disable("auto-totem")
```

## Logging

Log messages to console:

```python
import utils

utils.log("Script started")
utils.error("Something went wrong")
```

## Combined Example

Full script using multiple features:

```python
import bot
import baritone
import meteor
import utils

# Enable modules on connect
@on("player_state")
def on_connect(state):
    meteor.enable("auto-totem")
    utils.log("auto-totem enabled")

# Handle chat commands
@on("chat_message")
def handle_commands(sender, message, msg_type):
    if message == "!follow":
        baritone.follow(sender)
        bot.chat(f"Following {sender}")

    elif message == "!stop":
        baritone.cancel()
        bot.chat("Stopped")

    elif message == "!status":
        pos = bot.position()
        health = bot.health()
        bot.chat(f"HP: {health} Pos: {pos['x']:.0f}, {pos['y']:.0f}, {pos['z']:.0f}")

# Monitor health
@on("health_change")
def health_monitor(old_health, new_health):
    if new_health < 10:
        baritone.cancel()
        utils.log("Low health!")
```
