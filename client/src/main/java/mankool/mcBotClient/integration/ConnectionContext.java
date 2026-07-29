package mankool.mcBotClient.integration;

import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.Minecraft;

import java.util.function.Consumer;

/** Context passed to {@link ClientIntegration#onConnectionSetup} once per pipe connection. */
public interface ConnectionContext {
    Minecraft client();

    PipeConnection connection();

    void registerHandler(Protocol.ManagerToClientMessage.PayloadCase payloadCase,
                         Consumer<Protocol.ManagerToClientMessage> handler);

    /** Run a callback every END_CLIENT_TICK while this connection is active. */
    void registerTick(Runnable tickHook);
}
