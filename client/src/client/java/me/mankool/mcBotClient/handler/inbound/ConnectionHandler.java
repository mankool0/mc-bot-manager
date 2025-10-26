package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import me.mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ServerInfo;
import net.minecraft.network.packet.s2c.common.DisconnectS2CPacket;
import net.minecraft.text.MutableText;
import net.minecraft.text.Text;

public class ConnectionHandler extends BaseInboundHandler {

    public ConnectionHandler(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleConnectToServer(String messageId, Commands.ConnectToServerCommand command) {
        String serverAddress = command.getServerAddress();
        System.out.println("Connect to server: " + serverAddress);

        try {
            // Disconnect from current server if connected
            if (client.getNetworkHandler() != null) {
                client.world.disconnect(Text.literal("Connecting to another server"));
                client.disconnect(null, false);
            }

            // Parse the server address
            net.minecraft.client.network.ServerAddress address =
                net.minecraft.client.network.ServerAddress.parse(serverAddress);

            // Create server info
            ServerInfo serverInfo = new ServerInfo(serverAddress, serverAddress, ServerInfo.ServerType.OTHER);

            // Connect to the server
            net.minecraft.client.gui.screen.multiplayer.ConnectScreen.connect(
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
            if (client.getNetworkHandler() != null) {
                client.getNetworkHandler().onDisconnect(new DisconnectS2CPacket(Text.literal(reason)));
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
        client.stop();
    }
}