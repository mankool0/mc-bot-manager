package mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Connection;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.network.chat.Component;
import net.minecraft.network.protocol.common.ClientboundDisconnectPacket;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.util.VersionCompat;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ConnectionHandler extends BaseInboundHandler {
    private static final Logger LOGGER = LoggerFactory.getLogger(ConnectionHandler.class);

    private String pendingConnectMessageId;
    private Commands.ConnectToServerCommand pendingConnect;

    public ConnectionHandler(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    public void tick() {
        if (pendingConnect == null || !client.isGameLoadFinished()) return;
        Commands.ConnectToServerCommand command = pendingConnect;
        pendingConnect = null;
        handleConnectToServer(pendingConnectMessageId, command);
    }

    public void handleConnectToServer(String messageId, Commands.ConnectToServerCommand command) {
        // The manager auto-connects right after the handshake, which lands while the loading
        // overlay is still up; connecting then would have the initial title screen replace the
        // connect screen once loading ends.
        if (!client.isGameLoadFinished()) {
            if (pendingConnect != null) {
                sendFailure(pendingConnectMessageId, "Superseded by a newer connect command");
            }
            LOGGER.info("Game still loading, deferring connect to {}", command.getServerAddress());
            pendingConnectMessageId = messageId;
            pendingConnect = command;
            return;
        }

        String serverAddress = command.getServerAddress();
        LOGGER.info("Connect to server: {}", serverAddress);

        try {
            // Disconnect from current server if connected
            if (client.getConnection() != null) {
                VersionCompat.disconnectLevel(client.level);
                client.disconnect(null, false);
            }

            // Parse the server address
            net.minecraft.client.multiplayer.resolver.ServerAddress address =
                net.minecraft.client.multiplayer.resolver.ServerAddress.parseString(serverAddress);

            // Create server info
            ServerData serverInfo = new ServerData(serverAddress, serverAddress, ServerData.Type.OTHER);

            // Connect to the server
            net.minecraft.client.gui.screens.ConnectScreen.startConnecting(
                null, // parent screen
                client,
                address,
                serverInfo,
                false, // quickPlay
                null // cookieStorage
            );

            sendSuccess(messageId, "Connecting to " + serverAddress);
        } catch (Exception e) {
            LOGGER.error("Failed to connect to server: {}", e.getMessage());
            sendFailure(messageId, "Failed to connect: " + e.getMessage());
        }
    }

    public void handleDisconnect(String messageId, Commands.DisconnectCommand command) {
        String reason = command.getReason();
        LOGGER.info("Disconnect requested: {}", reason);

        try {
            if (client.getConnection() != null) {
                client.getConnection().handleDisconnect(new ClientboundDisconnectPacket(Component.literal(reason)));
                sendSuccess(messageId, "Disconnected: " + reason);
            } else {
                sendFailure(messageId, "Not connected to any server");
            }
        } catch (Exception e) {
            LOGGER.error("Failed to disconnect: {}", e.getMessage());
            sendFailure(messageId, "Failed to disconnect: " + e.getMessage());
        }
    }

    public void handleHandshakeReject(Connection.HandshakeReject reject) {
        String reason = reject.getReason();
        LOGGER.error("Handshake rejected by manager: {} (manager version {}, mod version {})",
            reason, reject.getManagerVersion(), reject.getModVersion());

        client.destroy();
    }

    public void handleShutdown(String messageId, Commands.ShutdownCommand command) {
        LOGGER.info("Shutdown requested: {}", command.getReason());
        sendSuccess(messageId, "Shutting down");
        client.destroy();
    }
}
