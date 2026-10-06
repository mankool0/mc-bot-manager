package mankool.mcBotClient.plugin;

import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.protocol.Packet;
import net.minecraft.world.phys.Vec3;
import org.slf4j.Logger;

import java.util.function.Predicate;

/**
 * What a plugin can hook into. Everything is called on the game thread, and a throw from any hook
 * unloads the plugin and reports it to the scripts on channel {@code mcbot:failed}.
 */
public interface PluginContext {
    String name();

    Minecraft client();

    Logger logger();

    /** Head of the client tick, before vanilla handles the keys. */
    void onTickStart(Runnable hook);

    /** End of the client tick. */
    void onTickEnd(Runnable hook);

    /**
     * The player's own step for the tick, before vanilla moves it: given the vector vanilla would
     * move it by, return the one to move by. Not called for pushes (pistons, shulkers).
     */
    void onMove(MoveHook hook);

    /** Just before the tick's movement packet is built, after Baritone's own turn for the tick. */
    void beforeMovePacket(Runnable hook);

    /**
     * A packet from the server, before the client handles it; false drops it. A bundle shows the
     * packets inside it one by one. Packets the client handles off the game thread never pass:
     * keep-alives, disconnects, chunk batch markers, pongs, and everything during login.
     */
    void onPacketReceived(Predicate<Packet<?>> hook);

    /**
     * Handle a packet as if the server had sent it, right away and past every onPacketReceived
     * hook. Game thread only; does nothing outside a world.
     */
    void receivePacket(Packet<?> packet);

    /** A packet the game thread is about to send; false drops it. Packets sent from other threads pass unseen. */
    void onPacketSent(Predicate<Packet<?>> hook);

    /** Send a packet past every onPacketSent hook. */
    void sendPacket(Packet<?> packet);

    /**
     * A script's message on {@code channel}, its payload JSON. For {@code bot.plugin_request} the
     * return value is the reply, as JSON; null replies {@code null}.
     */
    void onMessage(String channel, MessageHandler handler);

    /** A message to the scripts, as the {@code plugin_message} event. */
    void sendMessage(String channel, String json);

    interface MoveHook {
        Vec3 modify(LocalPlayer player, Vec3 movement);
    }

    interface MessageHandler {
        String handle(String json) throws Exception;
    }
}
