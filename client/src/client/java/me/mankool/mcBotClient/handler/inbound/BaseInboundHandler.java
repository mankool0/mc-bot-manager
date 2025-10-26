package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Protocol;
import me.mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.MinecraftClient;

import java.util.UUID;

public abstract class BaseInboundHandler {
    protected final MinecraftClient client;
    protected final PipeConnection connection;

    public BaseInboundHandler(MinecraftClient client, PipeConnection connection) {
        this.client = client;
        this.connection = connection;
    }

    protected void sendResponse(String messageId, Commands.CommandResponse.Status status, String message) {
        Commands.CommandResponse response = Commands.CommandResponse.newBuilder()
            .setCommandId(messageId)
            .setStatus(status)
            .setMessage(message)
            .build();

        Protocol.ClientToManagerMessage responseMessage = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setCommandResponse(response)
            .build();

        connection.sendMessage(responseMessage);
    }

    protected void sendSuccess(String messageId, String message) {
        sendResponse(messageId, Commands.CommandResponse.Status.SUCCESS, message);
    }

    protected void sendFailure(String messageId, String message) {
        sendResponse(messageId, Commands.CommandResponse.Status.FAILED, message);
    }

    protected void sendInProgress(String messageId, String message) {
        sendResponse(messageId, Commands.CommandResponse.Status.IN_PROGRESS, message);
    }
}