package mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Common;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.entity.Pose;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.shapes.VoxelShape;
import mankool.mcBotClient.connection.PipeConnection;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PlayerActionHandler extends BaseInboundHandler {
    private static final Logger LOGGER = LoggerFactory.getLogger(PlayerActionHandler.class);

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
        LOGGER.info("Move to: {}, {}, {}", target.getX(), target.getY(), target.getZ());
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

                Vec3 standingEyePos = player.getEyePosition();
                Vec3 sneakEyePos = new Vec3(player.getX(), player.getY() + player.getEyeHeight(Pose.CROUCHING), player.getZ());

                VoxelShape shape = player.level().getBlockState(blockPos).getShape(player.level(), blockPos);
                if (shape.isEmpty()) {
                    sendFailure(messageId, "Block at " + blockPos.toShortString() + " has no collision shape");
                    return;
                }
                AABB aabb = shape.bounds();

                Common.BlockFace requestedFace = command.getFace();
                Direction[] facesToTry = (requestedFace != null && requestedFace != Common.BlockFace.FACE_AUTO)
                    ? new Direction[]{BlockFaceUtil.protoFaceToDirection(requestedFace)}
                    : new Direction[]{Direction.DOWN, Direction.UP, Direction.NORTH, Direction.SOUTH, Direction.WEST, Direction.EAST};

                Vec3[] eyePositions = command.getSneak()
                    ? new Vec3[]{sneakEyePos}
                    : new Vec3[]{standingEyePos, sneakEyePos};

                Vec3 targetPos = null;
                Vec3 resolvedEyePos = eyePositions[0];
                outer:
                for (Vec3 eyePos : eyePositions) {
                    for (Direction faceDir : facesToTry) {
                        for (Vec3 candidate : BlockFaceUtil.faceCandidates(blockPos, faceDir, aabb)) {
                            Vec3 end = BlockFaceUtil.extendRay(eyePos, candidate);
                            BlockHitResult hit = player.level().clip(new net.minecraft.world.level.ClipContext(
                                eyePos, end,
                                net.minecraft.world.level.ClipContext.Block.OUTLINE,
                                net.minecraft.world.level.ClipContext.Fluid.NONE,
                                player
                            ));
                            if (hit.getType() == HitResult.Type.BLOCK && hit.getBlockPos().equals(blockPos) && hit.getDirection() == faceDir) {
                                targetPos = candidate;
                                resolvedEyePos = eyePos;
                                break outer;
                            }
                        }
                    }
                }

                if (targetPos == null) {
                    targetPos = Vec3.atCenterOf(blockPos);
                }

                BlockFaceUtil.applyRotationToward(player, resolvedEyePos, targetPos);

                sendSuccess(messageId, String.format("Looking at block %d, %d, %d", blockPos.getX(), blockPos.getY(), blockPos.getZ()));
            } else if (command.hasEntityId()) {
                // TODO: Look at entity by ID
                LOGGER.info("Look at entity: {}", command.getEntityId());
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