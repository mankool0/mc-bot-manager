# API Reference

## Modules

| Module | Purpose |
|--------|---------|
| [`bot`](bot.md) | Bot state queries and basic commands |
| [`baritone`](baritone.md) | Pathfinding and automation |
| [`meteor`](meteor.md) | Client module control |
| [`world`](world.md) | World data queries and block interaction |
| [`utils`](utils.md) | Logging utilities |
| [`server`](server.md) | Server info and tab list |
| [`manager`](manager.md) | Custom instance-table columns and script lifecycle |
| [`comms`](comms.md) | Messaging between scripts (pub/sub and addressed) |

## Multi-Bot

Most functions accept optional `bot_name` parameter:

```python
# Current bot (default)
bot.health()

# Specific bot
bot.health("bot1")

# Control another bot
baritone.goto(0, 64, 0, bot_name="bot2")
```
