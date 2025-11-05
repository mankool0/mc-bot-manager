package mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.network.chat.Component;
import net.minecraft.network.protocol.common.ClientboundDisconnectPacket;
import mankool.mcBotClient.connection.PipeConnection;

public class ConnectionHandler extends BaseInboundHandler {

    public ConnectionHandler(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleConnectToServer(String messageId, Commands.ConnectToServerCommand command) {
        String serverAddress = command.getServerAddress();
        System.out.println("Connect to server: " + serverAddress);

        try {
            // Disconnect from current server if connected
            if (client.getConnection() != null) {
                client.level.disconnect(Component.literal("Connecting to another server"));
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
            System.err.println("Failed to connect to server: " + e.getMessage());
            sendFailure(messageId, "Failed to connect: " + e.getMessage());
        }
    }

    public void handleDisconnect(String messageId, Commands.DisconnectCommand command) {
        String reason = command.getReason();
        System.out.println("Disconnect requested: " + reason);

        try {
            if (client.getConnection() != null) {
                client.getConnection().handleDisconnect(new ClientboundDisconnectPacket(Component.literal(reason)));
                sendSuccess(messageId, "Disconnected: " + reason);
            } else {
                sendFailure(messageId, "Not connected to any server");
            }
        } catch (Exception e) {
            System.err.println("Failed to disconnect: " + e.getMessage());
            sendFailure(messageId, "Failed to disconnect: " + e.getMessage());
        }
    }

    public void handleShutdown(String messageId, Commands.ShutdownCommand command) {
        System.out.println("Shutdown requested: " + command.getReason());
        sendSuccess(messageId, "Shutting down");
        client.destroy();
    }
}