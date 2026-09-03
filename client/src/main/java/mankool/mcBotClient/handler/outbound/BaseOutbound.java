package mankool.mcBotClient.handler.outbound;

import mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.Minecraft;

public abstract class BaseOutbound {
    protected final Minecraft client;
    protected final PipeConnection connection;

    public BaseOutbound(Minecraft client, PipeConnection connection) {
        this.client = client;
        this.connection = connection;
    }

    // Driven by MessageHandler for as long as its connection is live
    public final void tick(Minecraft client) {
        onClientTick(client);
    }

    /**
     * Called every client tick. Override this to implement your tick logic.
     * @param client The Minecraft client instance
     */
    protected abstract void onClientTick(Minecraft client);
}