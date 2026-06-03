# meteor module

## Module Control

### `toggle(module, bot_name="")`

Toggle module on/off.

**Parameters:**

- `module` (`str`) - Module name
- `bot_name` (`str`, optional) - Bot name

```python
meteor.toggle("auto-totem")
```

### `enable(module, bot_name="")`

Enable module.

**Parameters:**

- `module` (`str`) - Module name
- `bot_name` (`str`, optional) - Bot name

```python
meteor.enable("auto-totem")
```

### `disable(module, bot_name="")`

Disable module.

**Parameters:**

- `module` (`str`) - Module name
- `bot_name` (`str`, optional) - Bot name

```python
meteor.disable("auto-totem")
```

## Settings

### `set_setting(module, setting, value, bot_name="")`

Set module setting.

**Parameters:**

- `module` (`str`) - Module name
- `setting` (`str`) - Setting path (use dots for nested settings)
- `value` - Setting value
- `bot_name` (`str`, optional) - Bot name

```python
meteor.set_setting("auto-totem", "health", 10)
meteor.set_setting("kill-aura", "Targeting.range", 4.5)
```

### `get_setting(module, setting, bot_name="")`

Get module setting value.

**Parameters:**

- `module` (`str`) - Module name
- `setting` (`str`) - Setting path
- `bot_name` (`str`, optional) - Bot name

**Returns:** Setting value

```python
health = meteor.get_setting("auto-totem", "health")
```

## Module Info

### `list_modules(bot_name="")`

List all available Meteor modules.

**Parameters:**

- `bot_name` (`str`, optional) - Bot name

**Returns:** `list[str]` of module names

```python
modules = meteor.list_modules()
for module in modules:
    print(module)
```

### `get_module(module, bot_name="")`

Get module information.

**Parameters:**

- `module` (`str`) - Module name
- `bot_name` (`str`, optional) - Bot name

**Returns:** `dict` with module information

```python
info = meteor.get_module("auto-totem")
```
