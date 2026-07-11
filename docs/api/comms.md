# comms module

The `comms` module lets scripts talk to each other - across bots and to/from
global scripts. It supports two patterns that share one message bus:

- **Pub/sub broadcast**: `comms.emit(topic, data)` delivers to every running
  script that called `comms.subscribe(topic)`, anywhere in the manager.
- **Addressed send**: `comms.send(bot_name, data)` delivers to the listening
  scripts of one bot (or of the Global Scripts tab via `"_global"`).

Together with `manager.run_script()` this enables orchestrator patterns: a
global script that hands out tasks, reacts to worker status reports, and
starts/stops worker scripts on bots.

## Receiving messages

A script receives messages in one of two ways:

- **Handler**: register `@on("script_message")` and stay event-driven. The
  handler receives each message as it arrives (see
  [`script_message`](../events.md#script_message)).
- **Blocking receive**: call `comms.receive(timeout)` inside your loop.
  Messages queue up (bounded at 1000, oldest dropped) while the script is busy.

A script uses one or the other, never both: **if an `@on("script_message")`
handler is registered, messages go to the handler and `receive()` gets
nothing.**

!!! note "Listeners"
    A script is reachable by `comms.send` once it has opted in to messaging in
    any form: an `@on("script_message")` handler, any `comms.subscribe(...)`
    call, or a `comms.receive(...)` call. `comms.emit` is stricter - it only
    reaches scripts subscribed to that exact topic.

## Message format

Every message is a dict:

| Key | Type | Description |
|-----|------|-------------|
| `topic` | `str` | Topic passed to `emit`/`send` (`""` for a plain `send`) |
| `data` | any | The payload (deep copy of what the sender passed) |
| `sender_scope` | `str` | Sender's bot name, or `"_global"` for a global script |
| `sender_script` | `str` | Sender's script filename |
| `timestamp` | `float` | Send time, epoch seconds |

Replying is `comms.send(msg["sender_scope"], reply_data)`.

Payloads must be **plain data**: `None`, `bool`, `int`, `float`, `str`, and
`list`/`tuple`/`dict` of those (dict keys must be strings). Anything else
raises `TypeError`. Payloads are deep-copied on send, so the receiver can never
mutate the sender's objects.

## Functions

### `emit(topic, data=None)`

Broadcast to every running script subscribed to `topic`.

**Parameters:**

- `topic` (`str`) - Topic name (must be non-empty)
- `data` (optional) - Plain-data payload

**Returns:** `int` - number of scripts the message was delivered to at send
time (delivery itself is asynchronous). `0` means nobody was subscribed.

```python
comms.emit("inventory_full", {"bot": "Miner1", "pos": bot.position()})
```

### `send(bot_name, data=None, topic="")`

Send to every listening script of one bot, regardless of topics. Use
`"_global"` to reach global scripts.

**Parameters:**

- `bot_name` (`str`) - Target bot name, or `"_global"`
- `data` (optional) - Plain-data payload
- `topic` (`str`, optional) - Topic label to include in the message

**Returns:** `int` - number of recipient scripts. `0` means no script on that
bot is listening (unknown bot names also return `0`) - the sender can react,
e.g. start the script and resend:

```python
if comms.send("Miner1", {"cmd": "mine", "block": "diamond_ore"}) == 0:
    manager.run_script("miner.py", "Miner1")
    # give the script a moment to start and subscribe, then resend
```

### `subscribe(topic="")`

Subscribe this script to a topic for `emit` broadcasts. With no topic, just
opens the mailbox so addressed `send` messages are received. Subscriptions
last until the script stops (they do not survive a re-run).

```python
comms.subscribe("tasks")
```

### `unsubscribe(topic="")`

Remove a topic subscription. With no topic, closes the mailbox opened by
`subscribe()`/`receive()`.

### `receive(timeout=-1.0)`

Wait for the next queued message.

**Parameters:**

- `timeout` (`float`) - Seconds to wait. Negative waits forever; `0` polls.

**Returns:** the message dict, or `None` on timeout (also `None` if the script
is being stopped while waiting).

```python
comms.subscribe("tasks")
while True:
    msg = comms.receive(timeout=5.0)
    if msg is None:
        continue  # timeout - do housekeeping, check state, etc.
    utils.log(f"task from {msg['sender_scope']}: {msg['data']}")
```

### `pending()`

**Returns:** `int` - number of messages waiting in this script's `receive()`
queue.

## Semantics and caveats

- **Recipient counts are best-effort**: the count says how many scripts the
  message was handed to when you sent it, not that they have processed it.
- **No queueing for stopped scripts**: a message sent while a script is not
  running (or before it subscribed) is not delivered later. The
  `send() == 0` -> `run_script()` -> retry pattern above covers the startup
  race.
- **A script never receives its own messages.**
- **Ordering** is first-in-first-out per receiving script; there is no
  ordering guarantee across different receivers.
- **Inbox bound**: each script queues at most 1000 messages; beyond that the
  oldest are dropped (a warning is logged once per overflow burst).
- Blocking in an `@on(...)` handler with `receive()` is discouraged - it
  stalls all event delivery for that bot's scripts.
