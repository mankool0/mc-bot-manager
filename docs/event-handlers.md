# Event Handlers

## Basics

Event handlers are functions decorated with `@on(event_name)` that execute when specific events occur.

```python
@on("health_change")
def on_health(old_health, new_health):
    print(f"Health: {old_health} -> {new_health}")
```

## Multiple Handlers

A script can register multiple event handlers:

```python
import bot
import utils

@on("chat_message")
def chat_handler(sender, message, msg_type):
    utils.log(f"Chat: {sender}: {message}")

@on("health_change")
def health_handler(old_health, new_health):
    if new_health < old_health:
        utils.log("Took damage!")

@on("hunger_change")
def hunger_handler(old_hunger, new_hunger):
    if new_hunger < 6:
        bot.chat("Need food!")
```

## Handler Registration

Handlers are registered when the script loads:

1. Enable script (check checkbox)
2. Script code executes once
3. `@on` decorators register handlers
4. Handlers remain active until script is disabled

## Available Events

See [Events](events.md) for complete list.

Common events:

- `chat_message` - Chat received
- `health_change` - Health changed
- `hunger_change` - Hunger changed
- `player_state` - State update
- `inventory_update` - Inventory updated

## Error Handling

If a handler crashes, other handlers continue running:

```python
@on("chat_message")
def handler(sender, message, msg_type):
    try:
        # Your code
        pass
    except Exception as e:
        utils.error(f"Handler failed: {e}")
```

## Combining Event-Driven and Imperative

Scripts can use both approaches:

```python
import bot
import utils

# Runs once when script loads
initial_pos = bot.position()
utils.log(f"Started at {initial_pos}")

# Runs when events fire
@on("chat_message")
def handler(sender, message, msg_type):
    if message == "!status":
        bot.chat("Online and ready!")
```
