# Python Scripting API

Automate Minecraft bots using Python scripts with access to bot state, Baritone, and Meteor client.

## Basic Example

```python
import bot
import baritone

@on("chat_message")
def handle_commands(sender, message, msg_type):
    if message == "!goto spawn":
        baritone.goto(0, 64, 0)
        bot.chat("Going to spawn!")
```

## Documentation

- [Getting Started](getting-started.md) - Create your first script
- [API Reference](api/index.md) - Complete API documentation
- [Examples](examples.md) - Code examples
- [Events](events.md) - Available events
