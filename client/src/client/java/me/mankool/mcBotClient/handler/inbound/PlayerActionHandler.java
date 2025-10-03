package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Common;
import me.mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;

public class PlayerActionHandler extends BaseInboundHandler {

    public PlayerActionHandler(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleMoveTo(String messageId, Commands.MoveToCommand command) {
        ClientPlayerEntity player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        Common.Vec3d target = command.getTargetPosition();
        // TODO: Implement pathfinding/movement to target
        System.out.printf("Move to: %.2f, %.2f, %.2f%n",
            target.getX(), target.getY(), target.getZ());
        sendFailure(messageId, "Pathfinding not implemented yet");
    }

    public void handleLookAt(String messageId, Commands.LookAtCommand command) {
        ClientPlayerEntity player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            if (command.hasPosition()) {
                Common.Vec3d pos = command.getPosition();
                // Calculate yaw/pitch to look at position
                double dx = pos.getX() - player.getX();
                double dy = pos.getY() - (player.getY() + player.getEyeHeight(player.getPose()));
                double dz = pos.getZ() - player.getZ();

                double distance = Math.sqrt(dx * dx + dz * dz);
                float yaw = (float) (Math.atan2(-dx, dz) * 180 / Math.PI);
                float pitch = (float) (Math.atan2(-dy, distance) * 180 / Math.PI);

                player.setYaw(yaw);
                player.setPitch(pitch);
                sendSuccess(messageId, String.format("Looking at %.2f, %.2f, %.2f", pos.getX(), pos.getY(), pos.getZ()));
            } else if (command.hasEntityId()) {
                // TODO: Look at entity by ID
                System.out.println("Look at entity: " + command.getEntityId());
                sendFailure(messageId, "Look at entity not implemented yet");
            } else {
                sendFailure(messageId, "No target specified");
            }
        } catch (Exception e) {
            sendFailure(messageId, "Failed to look at target: " + e.getMessage());
        }
    }

    public void handleSetRotation(String messageId, Commands.SetRotationCommand command) {
        ClientPlayerEntity player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            player.setYaw(command.getYaw());
            player.setPitch(command.getPitch());
            sendSuccess(messageId, String.format("Rotation set to yaw=%.2f, pitch=%.2f", command.getYaw(), command.getPitch()));
        } catch (Exception e) {
            sendFailure(messageId, "Failed to set rotation: " + e.getMessage());
        }
    }
}