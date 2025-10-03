package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import me.mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;

public class ChatHandler extends BaseInboundHandler {

    public ChatHandler(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleSendChat(String messageId, Commands.SendChatCommand command) {
        ClientPlayerEntity player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            String message = command.getMessage();
            if (message.startsWith("/")) {
                // It's a command
                player.networkHandler.sendChatCommand(message);
                sendSuccess(messageId, "Command sent: " + message);
            } else {
                // Regular chat message
                player.networkHandler.sendChatMessage(message);
                sendSuccess(messageId, "Chat message sent");
            }
        } catch (Exception e) {
            sendFailure(messageId, "Failed to send message: " + e.getMessage());
        }
    }
}