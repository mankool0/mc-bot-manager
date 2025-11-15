# utils module

## Logging

### `log(message)`

Log informational message to the bot console.

**Parameters:**

- `message` (`str`) - Message to log

```python
utils.log("Script started")
utils.log(f"Position: {pos}")
```

### `error(message)`

Log error message to the bot console.

**Parameters:**

- `message` (`str`) - Error message to log

```python
utils.error("Failed to connect")
```

## Notes

- Log messages appear in the bot's console tab
- Errors are highlighted in red
