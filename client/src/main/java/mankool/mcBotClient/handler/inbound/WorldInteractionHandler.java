package mankool.mcBotClient.handler.inbound;

import baritone.api.BaritoneAPI;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcbot.protocol.Common;
import mankool.mcbot.protocol.Protocol;
import mankool.mcbot.protocol.World;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.phys.shapes.VoxelShape;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Optional;
import java.util.UUID;

public class WorldInteractionHandler extends BaseInboundHandler {

    private static final Logger LOGGER = LoggerFactory.getLogger(WorldInteractionHandler.class);

    public WorldInteractionHandler(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    /**
     * Handle block interaction command (right-click on a block).
     */
    public void handleInteractWithBlock(String messageId, World.InteractWithBlockCommand command) {
        LocalPlayer player = client.player;
        ClientLevel level = client.level;

        if (player == null || level == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        Common.BlockPos protoPos = command.getPosition();
        BlockPos blockPos = new BlockPos(protoPos.getX(), protoPos.getY(), protoPos.getZ());

        InteractionHand hand = command.getHand() == Common.Hand.MAIN_HAND
            ? InteractionHand.MAIN_HAND
            : InteractionHand.OFF_HAND;

        if (level.isOutsideBuildHeight(blockPos)) {
            sendFailure(messageId, "Block position is outside world height");
            return;
        }

        // Set sneaking state if requested
        boolean wasSneaking = player.isShiftKeyDown();
        if (command.getSneak() != wasSneaking) {
            player.setShiftKeyDown(command.getSneak());
        }

        try {
            var ctx = BaritoneAPI.getProvider().getPrimaryBaritone().getPlayerContext();
            double blockReachDistance = ctx.playerController().getBlockReachDistance();

            Optional<BlockHitResult> hitOpt = rayTraceBlock(level, player, player.getEyePosition(), blockPos, blockReachDistance);
            if (hitOpt.isEmpty()) {
                sendFailure(messageId, "Block not reachable");
                return;
            }
            BlockHitResult hitResult = hitOpt.get();

            if (command.getLookAtBlock()) {
                Vec3 eye = player.getEyePosition();
                Vec3 hit = hitResult.getLocation();
                double dx = hit.x - eye.x, dy = hit.y - eye.y, dz = hit.z - eye.z;
                player.setYRot((float) Math.toDegrees(Math.atan2(-dx, dz)));
                player.setXRot((float) Math.toDegrees(-Math.atan2(dy, Math.sqrt(dx * dx + dz * dz))));
            }

            LOGGER.debug("Interacting with {} - hit: {}, face: {}, looked: {}",
                blockPos.toShortString(), hitResult.getLocation(), hitResult.getDirection(), command.getLookAtBlock());

            var interactionResult = client.gameMode.useItemOn(player, hand, hitResult);

            if (interactionResult.consumesAction()) {
                sendSuccess(messageId, "Interacted with block at " + blockPos.toShortString());
            } else {
                sendFailure(messageId, "Block interaction had no effect");
            }

        } catch (Exception e) {
            LOGGER.error("Error interacting with block", e);
            sendFailure(messageId, "Error: " + e.getMessage());
        } finally {
            if (command.getSneak() != wasSneaking) {
                player.setShiftKeyDown(wasSneaking);
            }
        }
    }

    public void handleCanReachBlock(String messageId, World.CanReachBlockCommand command) {
        LocalPlayer player = client.player;
        ClientLevel level = client.level;

        if (player == null || level == null) {
            sendCanReachBlockResponse(messageId, false);
            return;
        }

        Common.BlockPos protoPos = command.getPosition();
        BlockPos blockPos = new BlockPos(protoPos.getX(), protoPos.getY(), protoPos.getZ());

        try {
            var ctx = BaritoneAPI.getProvider().getPrimaryBaritone().getPlayerContext();
            double blockReachDistance = ctx.playerController().getBlockReachDistance();

            Vec3 eyePos;
            if (command.hasFromPosition()) {
                Common.BlockPos from = command.getFromPosition();
                double eyeY = command.getSneak() ? from.getY() + 1.27 : from.getY() + 1.62;
                eyePos = new Vec3(from.getX() + 0.5, eyeY, from.getZ() + 0.5);
            } else {
                eyePos = player.getEyePosition();
            }

            sendCanReachBlockResponse(messageId, rayTraceBlock(level, player, eyePos, blockPos, blockReachDistance).isPresent());

        } catch (Exception e) {
            LOGGER.error("Error checking reachability", e);
            sendCanReachBlockResponse(messageId, false);
        }
    }

    private Optional<BlockHitResult> rayTraceBlock(ClientLevel level, LocalPlayer player, Vec3 eyePos, BlockPos blockPos, double reachDistance) {
        VoxelShape shape = level.getBlockState(blockPos).getShape(level, blockPos);
        double bx = blockPos.getX(), by = blockPos.getY(), bz = blockPos.getZ();
        double minX = bx + shape.min(Direction.Axis.X), maxX = bx + shape.max(Direction.Axis.X);
        double minY = by + shape.min(Direction.Axis.Y), maxY = by + shape.max(Direction.Axis.Y);
        double minZ = bz + shape.min(Direction.Axis.Z), maxZ = bz + shape.max(Direction.Axis.Z);
        double midX = (minX + maxX) / 2, midY = (minY + maxY) / 2, midZ = (minZ + maxZ) / 2;
        Vec3[] candidates = {
            new Vec3(midX, midY, midZ), // center
            new Vec3(midX, minY, midZ), // down face
            new Vec3(midX, maxY, midZ), // up face
            new Vec3(midX, midY, minZ), // north face
            new Vec3(midX, midY, maxZ), // south face
            new Vec3(minX, midY, midZ), // west face
            new Vec3(maxX, midY, midZ), // east face
        };

        for (Vec3 target : candidates) {
            if (eyePos.distanceTo(target) > reachDistance) continue;
            BlockHitResult hit = level.clip(new ClipContext(
                eyePos, target,
                ClipContext.Block.OUTLINE,
                ClipContext.Fluid.NONE,
                player
            ));
            if (hit.getType() == HitResult.Type.BLOCK && hit.getBlockPos().equals(blockPos)) {
                return Optional.of(hit);
            }
        }
        return Optional.empty();
    }

    private void sendCanReachBlockResponse(String messageId, boolean reachable) {
        World.CanReachBlockResponse response = World.CanReachBlockResponse.newBuilder()
            .setCommandId(messageId)
            .setReachable(reachable)
            .build();
        Protocol.ClientToManagerMessage msg = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setCanReachBlockResponse(response)
            .build();
        connection.sendMessage(msg);
    }
}