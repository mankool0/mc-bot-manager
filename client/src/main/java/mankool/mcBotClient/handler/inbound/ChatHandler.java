package mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import mankool.mcBotClient.connection.PipeConnection;

public class ChatHandler extends BaseInboundHandler {

    public ChatHandler(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleSendChat(String messageId, Commands.SendChatCommand command) {
        LocalPlayer player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            String message = command.getMessage();
            if (message.startsWith("/")) {
                // It's a command - remove the leading slash
                String commandWithoutSlash = message.substring(1);
                player.connection.sendCommand(commandWithoutSlash);
                sendSuccess(messageId, "Command sent: " + message);
            } else {
                // Regular chat message
                player.connection.sendChat(message);
                sendSuccess(messageId, "Chat message sent");
            }
        } catch (Exception e) {
            sendFailure(messageId, "Failed to send message: " + e.getMessage());
        }
    }
}