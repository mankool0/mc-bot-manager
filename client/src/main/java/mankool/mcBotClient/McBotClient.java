package mankool.mcBotClient;

import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.handler.MessageHandler;
import mankool.mcBotClient.integration.IntegrationRegistry;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.Minecraft;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class McBotClient implements ClientModInitializer {
    public static final String MOD_ID = "mc-bot-client";
    public static final Logger LOGGER = LoggerFactory.getLogger(MOD_ID);

    private static McBotClient instance;
    private PipeConnection pipeConnection;
    private MessageHandler messageHandler;
    private boolean initialized = false;

    @Override
    public void onInitializeClient() {
        instance = this;
        LOGGER.info("Initializing Minecraft Bot Client");

        IntegrationRegistry.initializeAll(Minecraft::getInstance);

        // Register lifecycle events
        ClientLifecycleEvents.CLIENT_STARTED.register(this::onClientStarted);
        ClientLifecycleEvents.CLIENT_STOPPING.register(this::onClientStopping);

        // Register tick event for connection management
        ClientTickEvents.END_CLIENT_TICK.register(this::onClientTick);
    }

    private void onClientStarted(Minecraft client) {
        LOGGER.info("Client started, initializing pipe connection");
        initializeConnection(client);
    }

    private void onClientStopping(Minecraft client) {
        LOGGER.info("Client stopping, disconnecting pipe");
        disconnect();
    }

    private void onClientTick(Minecraft client) {
        // Check if we're in a world and need to connect
        if (!initialized && client.player != null && client.level != null) {
            initializeConnection(client);
        }

        // Reconnect if the connection dropped, unless the manager rejected the handshake (a
        // version mismatch), in which case the client is shutting down and must not reconnect.
        if (initialized && pipeConnection != null && !pipeConnection.isConnected()
                && !pipeConnection.isHandshakeRejected()) {
            LOGGER.warn("Connection lost, attempting to reconnect");
            initializeConnection(client);
        }
    }

    private void initializeConnection(Minecraft client) {
        if (pipeConnection != null && pipeConnection.isConnected()) {
            return;
        }

        try {
            // Generate unique client ID
            String clientId = "minecraft_" + System.currentTimeMillis();

            // Create and connect pipe
            pipeConnection = new PipeConnection(clientId);
            if (pipeConnection.connect()) {
                LOGGER.info("Successfully connected to manager pipe");

                // Create and start message handler
                messageHandler = new MessageHandler(pipeConnection, client);
                messageHandler.start();

                initialized = true;
            } else {
                LOGGER.error("Failed to connect to manager pipe");
                pipeConnection = null;
                client.destroy();
            }
        } catch (Exception e) {
            LOGGER.error("Error initializing connection", e);
        }
    }

    private void disconnect() {
        if (messageHandler != null) {
            messageHandler.stop();
            messageHandler = null;
        }

        if (pipeConnection != null) {
            pipeConnection.disconnect();
            pipeConnection = null;
        }

        initialized = false;
    }

    public static McBotClient getInstance() {
        return instance;
    }

    public PipeConnection getPipeConnection() {
        return pipeConnection;
    }

    public MessageHandler getMessageHandler() {
        return messageHandler;
    }
}