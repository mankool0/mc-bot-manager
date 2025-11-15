# API Reference

## Modules

| Module | Purpose |
|--------|---------|
| [`bot`](bot.md) | Bot state queries and basic commands |
| [`baritone`](baritone.md) | Pathfinding and automation |
| [`meteor`](meteor.md) | Client module control |
| [`utils`](utils.md) | Logging utilities |

## Multi-Bot

Most functions accept optional `bot_name` parameter:

```python
# Current bot (default)
bot.health()

# Specific bot
bot.health("bot1")

# Control another bot
baritone.goto(0, 64, 0, bot="bot2")
```
