package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import me.mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.MinecraftClient;

public class ConnectionHandler extends BaseInboundHandler {

    public ConnectionHandler(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleConnectToServer(String messageId, Commands.ConnectToServerCommand command) {
        // TODO: Implement server connection
        System.out.println("Connect to server: " + command.getServerAddress());
        sendFailure(messageId, "Not implemented yet");
    }

    public void handleDisconnect(String messageId, Commands.DisconnectCommand command) {
        // TODO: Implement disconnect when we have proper access
        System.out.println("Disconnect requested: " + command.getReason());
        sendFailure(messageId, "Not implemented yet");
    }

    public void handleShutdown(String messageId, Commands.ShutdownCommand command) {
        System.out.println("Shutdown requested: " + command.getReason());
        sendSuccess(messageId, "Shutting down");
        client.stop();
    }
}