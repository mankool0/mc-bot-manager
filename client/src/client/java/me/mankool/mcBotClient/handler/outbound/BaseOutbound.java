package me.mankool.mcBotClient.handler.outbound;

import me.mankool.mcBotClient.connection.PipeConnection;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.MinecraftClient;

public abstract class BaseOutbound {
    protected final MinecraftClient client;
    protected final PipeConnection connection;

    public BaseOutbound(MinecraftClient client, PipeConnection connection) {
        this.client = client;
        this.connection = connection;

        // Auto-register tick event
        ClientTickEvents.END_CLIENT_TICK.register(this::onClientTick);
    }

    /**
     * Called every client tick. Override this to implement your tick logic.
     * @param client The Minecraft client instance
     */
    protected abstract void onClientTick(MinecraftClient client);
}