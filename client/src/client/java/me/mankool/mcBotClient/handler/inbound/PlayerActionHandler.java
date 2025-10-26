package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Common;
import me.mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.hit.HitResult;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;

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
                BlockPos blockPos = new BlockPos((int) pos.getX(), (int) pos.getY(), (int) pos.getZ());

                Vec3d eyePos = player.getEyePos();

                Vec3d[] faceCenters = new Vec3d[]{
                    new Vec3d(blockPos.getX() + 0.5, blockPos.getY(), blockPos.getZ() + 0.5),      // Down
                    new Vec3d(blockPos.getX() + 0.5, blockPos.getY() + 1, blockPos.getZ() + 0.5),  // Up
                    new Vec3d(blockPos.getX() + 0.5, blockPos.getY() + 0.5, blockPos.getZ()),      // North
                    new Vec3d(blockPos.getX() + 0.5, blockPos.getY() + 0.5, blockPos.getZ() + 1),  // South
                    new Vec3d(blockPos.getX(), blockPos.getY() + 0.5, blockPos.getZ() + 0.5),      // West
                    new Vec3d(blockPos.getX() + 1, blockPos.getY() + 0.5, blockPos.getZ() + 0.5)   // East
                };

                Direction[] directions = new Direction[]{
                    Direction.DOWN, Direction.UP, Direction.NORTH, Direction.SOUTH, Direction.WEST, Direction.EAST
                };

                Vec3d targetPos = null;
                Direction bestFace = null;

                for (int i = 0; i < faceCenters.length; i++) {
                    Vec3d faceCenter = faceCenters[i];

                    Vec3d direction = faceCenter.subtract(eyePos).normalize();
                    Vec3d extendedTarget = eyePos.add(direction.multiply(eyePos.distanceTo(faceCenter) + 0.5));

                    BlockHitResult hitResult = player.getWorld().raycast(new net.minecraft.world.RaycastContext(
                        eyePos,
                        extendedTarget,
                        net.minecraft.world.RaycastContext.ShapeType.OUTLINE,
                        net.minecraft.world.RaycastContext.FluidHandling.NONE,
                        player
                    ));

                    if (hitResult.getType() == HitResult.Type.BLOCK && hitResult.getBlockPos().equals(blockPos)) {
                        targetPos = faceCenter;
                        bestFace = directions[i];
                        break;
                    }
                }

                if (targetPos == null) {
                    targetPos = Vec3d.ofCenter(blockPos);
                    bestFace = null;
                }

                double dx = targetPos.x - eyePos.x;
                double dy = targetPos.y - eyePos.y;
                double dz = targetPos.z - eyePos.z;

                double distance = Math.sqrt(dx * dx + dz * dz);
                float yaw = (float) (Math.atan2(-dx, dz) * 180 / Math.PI);
                float pitch = (float) (Math.atan2(-dy, distance) * 180 / Math.PI);

                player.setYaw(yaw);
                player.setPitch(pitch);

                if (bestFace != null) {
                    sendSuccess(messageId, String.format("Looking at block %.0f, %.0f, %.0f (%s face)",
                        pos.getX(), pos.getY(), pos.getZ(), bestFace.asString()));
                } else {
                    sendSuccess(messageId, String.format("Looking at block %.0f, %.0f, %.0f (center, no face reachable)",
                        pos.getX(), pos.getY(), pos.getZ()));
                }
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