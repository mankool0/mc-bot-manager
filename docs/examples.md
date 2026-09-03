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

## Search the Saved World

Find an ender chest without loading a single chunk. The manager's autosave keeps every chunk a
bot has seen on this server, and `use_disk=True` searches it alongside memory (see
[Saved world](api/world.md#saved-world)). A bot that respawns at spawn with an empty inventory
picks the candidate farthest from `0, 0`, where builds near spawn are least likely to be picked
clean, and verifies it on arrival:

```python
import math
import baritone
import world
import utils

hits = world.find_block_entities(["minecraft:ender_chest"], 0, 0, 5000,
                                 dimension="minecraft:overworld", use_disk=True)
hits.sort(key=lambda be: -math.hypot(be.x, be.z))   # farthest from spawn first
for be in hits[:10]:
    utils.log(f"ender chest at {be.x} {be.y} {be.z}")

if hits:
    target = hits[0]
    baritone.goto(target.x, target.y, target.z)
    # ... wait for arrival, then confirm the chunk still holds it
    if world.get_block(target.x, target.y, target.z) == "minecraft:ender_chest":
        world.interact_block(target.x, target.y, target.z)
```

The block searches take the same flags. Nether portal frames along a bearing, from the save:

```python
frames = world.find_blocks("minecraft:obsidian", 0, 64, 0, radius=2000,
                           dimension="minecraft:the_nether", use_disk=True)
```

## Orchestrator and Workers

A global script (Global Scripts tab) that hands out mining tasks and reacts to
worker reports, plus a worker script that lives in each bot's Scripts tab. See
the [`comms`](api/comms.md) module for the messaging details.

Worker - `miner.py` in a bot's Scripts tab:

```python
import bot
import baritone
import comms
import utils

comms.subscribe()  # open the mailbox for addressed sends

while True:
    msg = comms.receive(timeout=5.0)
    if msg is None:
        continue

    cmd = msg["data"]["cmd"]
    if cmd == "mine":
        block = msg["data"]["block"]
        utils.log(f"Mining {block}")
        baritone.mine(block)
    elif cmd == "stop":
        baritone.cancel()

    # Report status back to whoever sent the command
    picks = sum(1 for i in bot.inventory() if "pickaxe" in i["item_id"])
    if picks == 0:
        comms.send(msg["sender_scope"], {"status": "no_pickaxes"}, topic="reports")
```

Orchestrator - global script:

```python
import bot
import comms
import manager
import utils

def command_bot(name, task):
    # Start the worker script (and the game client) on demand, then send.
    if not bot.is_online(name):
        bot.start(name)
        if not bot.wait_for_online(timeout=120, bot_name=name):
            utils.error(f"{name} failed to come online")
            return
    if comms.send(name, task) == 0:
        manager.run_script("miner.py", name)
        import time
        time.sleep(1)  # let it start and subscribe
        comms.send(name, task)

@on("script_message")
def on_report(msg):
    if msg["data"].get("status") == "no_pickaxes":
        worker = msg["sender_scope"]
        utils.log(f"{worker} is out of pickaxes - sending it to restock")
        comms.send(worker, {"cmd": "stop"})

@on("bot_disconnected")
def on_bot_lost(bot_name, uptime_seconds):
    utils.log(f"{bot_name} disconnected after {uptime_seconds:.0f}s")

command_bot("Miner1", {"cmd": "mine", "block": "diamond_ore"})
command_bot("Miner2", {"cmd": "mine", "block": "iron_ore"})
# The script's body is done, but the @on handlers keep it alive and reacting.
```
