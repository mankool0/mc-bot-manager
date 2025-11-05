package mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Common;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import mankool.mcBotClient.connection.PipeConnection;

public class PlayerActionHandler extends BaseInboundHandler {

    public PlayerActionHandler(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleMoveTo(String messageId, Commands.MoveToCommand command) {
        LocalPlayer player = client.player;
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
        LocalPlayer player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            if (command.hasPosition()) {
                Common.Vec3d pos = command.getPosition();
                BlockPos blockPos = new BlockPos((int) pos.getX(), (int) pos.getY(), (int) pos.getZ());

                Vec3 eyePos = player.getEyePosition();

                Vec3[] faceCenters = new Vec3[]{
                    new Vec3(blockPos.getX() + 0.5, blockPos.getY(), blockPos.getZ() + 0.5),      // Down
                    new Vec3(blockPos.getX() + 0.5, blockPos.getY() + 1, blockPos.getZ() + 0.5),  // Up
                    new Vec3(blockPos.getX() + 0.5, blockPos.getY() + 0.5, blockPos.getZ()),      // North
                    new Vec3(blockPos.getX() + 0.5, blockPos.getY() + 0.5, blockPos.getZ() + 1),  // South
                    new Vec3(blockPos.getX(), blockPos.getY() + 0.5, blockPos.getZ() + 0.5),      // West
                    new Vec3(blockPos.getX() + 1, blockPos.getY() + 0.5, blockPos.getZ() + 0.5)   // East
                };

                Direction[] directions = new Direction[]{
                    Direction.DOWN, Direction.UP, Direction.NORTH, Direction.SOUTH, Direction.WEST, Direction.EAST
                };

                Vec3 targetPos = null;
                Direction bestFace = null;

                for (int i = 0; i < faceCenters.length; i++) {
                    Vec3 faceCenter = faceCenters[i];

                    Vec3 direction = faceCenter.subtract(eyePos).normalize();
                    Vec3 extendedTarget = eyePos.add(direction.scale(eyePos.distanceTo(faceCenter) + 0.5));

                    BlockHitResult hitResult = player.level().clip(new net.minecraft.world.level.ClipContext(
                        eyePos,
                        extendedTarget,
                        net.minecraft.world.level.ClipContext.Block.OUTLINE,
                        net.minecraft.world.level.ClipContext.Fluid.NONE,
                        player
                    ));

                    if (hitResult.getType() == HitResult.Type.BLOCK && hitResult.getBlockPos().equals(blockPos)) {
                        targetPos = faceCenter;
                        bestFace = directions[i];
                        break;
                    }
                }

                if (targetPos == null) {
                    targetPos = Vec3.atCenterOf(blockPos);
                    bestFace = null;
                }

                double dx = targetPos.x - eyePos.x;
                double dy = targetPos.y - eyePos.y;
                double dz = targetPos.z - eyePos.z;

                double distance = Math.sqrt(dx * dx + dz * dz);
                float yaw = (float) (Math.atan2(-dx, dz) * 180 / Math.PI);
                float pitch = (float) (Math.atan2(-dy, distance) * 180 / Math.PI);

                player.setYRot(yaw);
                player.setXRot(pitch);

                if (bestFace != null) {
                    sendSuccess(messageId, String.format("Looking at block %.0f, %.0f, %.0f (%s face)",
                        pos.getX(), pos.getY(), pos.getZ(), bestFace.getSerializedName()));
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
        LocalPlayer player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            player.setYRot(command.getYaw());
            player.setXRot(command.getPitch());
            sendSuccess(messageId, String.format("Rotation set to yaw=%.2f, pitch=%.2f", command.getYaw(), command.getPitch()));
        } catch (Exception e) {
            sendFailure(messageId, "Failed to set rotation: " + e.getMessage());
        }
    }
}