package mankool.mcBotClient.handler.outbound;

import mankool.mcBotClient.connection.PipeConnection;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.Minecraft;

public abstract class BaseOutbound {
    protected final Minecraft client;
    protected final PipeConnection connection;

    public BaseOutbound(Minecraft client, PipeConnection connection) {
        this.client = client;
        this.connection = connection;

        // Auto-register tick event
        ClientTickEvents.END_CLIENT_TICK.register(this::onClientTick);
    }

    /**
     * Called every client tick. Override this to implement your tick logic.
     * @param client The Minecraft client instance
     */
    protected abstract void onClientTick(Minecraft client);
}