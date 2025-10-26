package me.mankool.mcBotClient;

import me.mankool.mcBotClient.connection.PipeConnection;
import me.mankool.mcBotClient.handler.MessageHandler;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.MinecraftClient;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.lang.invoke.MethodHandles;

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

        // Register lambda factory for Orbit event bus (required for @EventHandler annotations)
        try {
            meteordevelopment.meteorclient.MeteorClient.EVENT_BUS.registerLambdaFactory(
                "me.mankool.mcBotClient",
                (lookupInMethod, klass) -> (MethodHandles.Lookup) lookupInMethod.invoke(null, klass, MethodHandles.lookup())
            );
            LOGGER.info("Registered Orbit lambda factory for event handlers");
        } catch (Exception e) {
            LOGGER.error("Failed to register Orbit lambda factory", e);
        }

        // Register lifecycle events
        ClientLifecycleEvents.CLIENT_STARTED.register(this::onClientStarted);
        ClientLifecycleEvents.CLIENT_STOPPING.register(this::onClientStopping);

        // Register tick event for connection management
        ClientTickEvents.END_CLIENT_TICK.register(this::onClientTick);
    }

    private void onClientStarted(MinecraftClient client) {
        LOGGER.info("Client started, initializing pipe connection");
        initializeConnection(client);
    }

    private void onClientStopping(MinecraftClient client) {
        LOGGER.info("Client stopping, disconnecting pipe");
        disconnect();
    }

    private void onClientTick(MinecraftClient client) {
        // Check if we're in a world and need to connect
        if (!initialized && client.player != null && client.world != null) {
            initializeConnection(client);
        }

        // Check if connection was lost and try to reconnect
        if (initialized && pipeConnection != null && !pipeConnection.isConnected()) {
            LOGGER.warn("Connection lost, attempting to reconnect");
            initializeConnection(client);
        }
    }

    private void initializeConnection(MinecraftClient client) {
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
                client.stop();
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