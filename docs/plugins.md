# Client Plugins

A client plugin is Java source a script sends to a bot. The client compiles and loads it on the fly, with no jar and no game restart, and runs it inside the game on every tick. Plugins are for work that has to happen at tick precision or on packets, which a script cannot do over the pipe. Scripts keep the decisions and talk to the plugin with messages.

## Writing one

The main class implements `mankool.mcBotClient.plugin.ClientPlugin` and has a public no-argument constructor. `onLoad` registers its hooks on the `PluginContext` it is given:

```java
package demo;

import mankool.mcBotClient.plugin.ClientPlugin;
import mankool.mcBotClient.plugin.PluginContext;
import net.minecraft.network.protocol.game.ClientboundPlayerPositionPacket;

public class SetBacks implements ClientPlugin {
    private int count;

    @Override
    public void onLoad(PluginContext ctx) {
        ctx.onPacketReceived(packet -> {
            if (packet instanceof ClientboundPlayerPositionPacket) {
                count++;
                ctx.sendMessage("set_back", "{\"count\":" + count + "}");
            }
            return true;
        });
        ctx.onMessage("count", json -> "{\"count\":" + count + "}");
        ctx.onMessage("reset", json -> {
            count = 0;
            return null;
        });
    }
}
```

| hook | when |
|---|---|
| `onTickStart(Runnable)` | head of the client tick, before vanilla handles the keys |
| `onTickEnd(Runnable)` | end of the client tick |
| `onMove(MoveHook)` | the player's own step for the tick: given the vector vanilla would move it by, return the one to move by |
| `beforeMovePacket(Runnable)` | just before the tick's movement packet is built, after Baritone's own turn for the tick |
| `onPacketReceived(Predicate<Packet<?>>)` | a server packet, before the client handles it; return false to drop it. A bundle shows the packets inside it one by one |
| `receivePacket(Packet<?>)` | handle a packet as if the server had sent it, right away, skipping every `onPacketReceived` hook |
| `onPacketSent(Predicate<Packet<?>>)` | a packet the game thread is about to send; return false to drop it |
| `sendPacket(Packet<?>)` | send a packet that skips every `onPacketSent` hook |
| `onMessage(channel, handler)` | a script's message; for `plugin_request` the handler's return value (JSON) is the reply |
| `sendMessage(channel, json)` | a message to the scripts, delivered as the `plugin_message` event |

- **Threading:** every callback runs on the game thread.
- **Changing a packet:** most packets cannot be modified, so drop the packet and pass a new one to `receivePacket` or `sendPacket`. Holding packets and passing them on later delays them.
- **Failures:** a throw from any hook unloads the plugin and reports it on channel `mcbot:failed` with `{"error": ...}`. An endless loop still freezes the bot.
- **Names:** write against Mojang's names, the ones the mod itself uses, plus Baritone, Meteor, Gson and anything else the game loads.

## Loading one

A script sends the source and names the main class. Diagnostics report the paths given in `sources`:

```python
import bot, utils

source = open("/path/to/SetBacks.java").read()
result = bot.load_plugin("setbacks", {"demo/SetBacks.java": source}, "demo.SetBacks")
if result is None:
    utils.log("the client did not answer")
elif not result["ok"]:
    for d in result["diagnostics"]:
        utils.log("%s %s:%d: %s" % (d["kind"], d["path"], d["line"], d["message"]))
    utils.log(result["error"])
```

- Loading a name that is already loaded replaces that plugin, but only once the new source compiles. Until then the old one keeps running.
- Plugins live as long as the game runs. A manager reconnect does not unload them, and loading again at startup simply replaces them.
- `bot.unload_plugin(name)` stops one, and `bot.list_plugins()` shows what is loaded.

## On 1.21.x

Before 26.1 the game runs under intermediary names (`class_746` for `LocalPlayer`), so source written with Mojang's names cannot compile against it directly. On those versions the first load does three things:

1. Asks the manager for Mojang's mappings for the version. The manager downloads them from Mojang once per machine, checks them against the checksum Mojang publishes, keeps them in its cache, and sends them compressed over the pipe.
2. Writes renamed copies of the game and mod jars, holding declarations only, to compile against, into `<game dir>/mcbot/plugins/<version>/`.
3. Indexes the class hierarchy.

Copies of jars the game no longer loads are deleted, and so are the copies for other game versions, once an instance changes version. The compiled classes are renamed back to the runtime names before they run. Members are found up the class hierarchy, so calling a method through a subclass works. On 26.1 and later, none of this happens, and the first load deletes copies left from 1.21.x.

## Limits

- A plugin cannot add mixins: they apply when a class first loads, long before the plugin. It can only use the hooks above.
- Source is compiled per game version. Where Mojang renamed something between versions, a plugin needs a version of its own.
- `onPacketReceived` only sees packets the client handles on the game thread, which is nearly all of play. Keep-alives, disconnects, chunk batch markers, pongs and everything during login are handled on the network thread and never pass.
- `onPacketSent` only sees packets the game thread sends. The client answers keep-alives and chunk batches from the network thread, so those replies pass unseen.
