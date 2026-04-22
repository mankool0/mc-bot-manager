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
import net.minecraft.world.entity.Pose;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.shapes.VoxelShape;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import mankool.mcBotClient.util.VersionCompat;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Optional;
import java.util.UUID;

public class WorldInteractionHandler extends BaseInboundHandler {

    private static final Logger LOGGER = LoggerFactory.getLogger(WorldInteractionHandler.class);

    private static WorldInteractionHandler instance;

    private boolean holdingAttack = false;
    private int holdAttackTicksRemaining = 0;  // 0 = indefinite

    public static boolean isHoldingAttack() {
        return instance != null && instance.holdingAttack;
    }

    public WorldInteractionHandler(Minecraft client, PipeConnection connection) {
        super(client, connection);
        instance = this;
    }

    public void handleGetHoldAttackStatus(String messageId) {
        World.HoldAttackStatusResponse response = World.HoldAttackStatusResponse.newBuilder()
            .setCommandId(messageId)
            .setEnabled(holdingAttack)
            .build();
        Protocol.ClientToManagerMessage msg = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setHoldAttackStatusResponse(response)
            .build();
        connection.sendMessage(msg);
    }

    public void handleHoldAttack(World.HoldAttackCommand command) {
        holdingAttack = command.getEnabled();
        holdAttackTicksRemaining = command.getDurationTicks();
        if (!holdingAttack && client.gameMode != null) {
            client.gameMode.stopDestroyBlock();
        }
    }

    public void tick() {
        if (holdAttackTicksRemaining > 0) {
            holdAttackTicksRemaining--;
            if (holdAttackTicksRemaining == 0) {
                holdingAttack = false;
                if (client.gameMode != null) client.gameMode.stopDestroyBlock();
                return;
            }
        }
        if (!holdingAttack || client.player == null || client.gameMode == null || client.level == null) return;
        if (client.hitResult instanceof BlockHitResult bhr) {
            BlockPos pos = bhr.getBlockPos();
            if (!client.level.getBlockState(pos).isAir()) {
                Direction face = bhr.getDirection();
                if (client.gameMode.continueDestroyBlock(pos, face)) {
                    VersionCompat.addBreakingBlockEffect(client.level, pos, face);
                    client.player.swing(InteractionHand.MAIN_HAND);
                }
                return;
            }
        }
        client.gameMode.stopDestroyBlock();
    }

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

        boolean wasSneaking = player.isShiftKeyDown();
        if (command.getSneak() != wasSneaking) {
            player.setShiftKeyDown(command.getSneak());
        }

        try {
            var ctx = BaritoneAPI.getProvider().getPrimaryBaritone().getPlayerContext();
            double blockReachDistance = ctx.playerController().getBlockReachDistance();

            BlockHitResult hitResult;
            Common.BlockFace protoFace = command.getFace();
            if (protoFace != null && protoFace != Common.BlockFace.FACE_AUTO) {
                Direction dir = BlockFaceUtil.protoFaceToDirection(protoFace);
                Vec3 faceCenter = Vec3.atCenterOf(blockPos).add(
                    dir.getStepX() * 0.5, dir.getStepY() * 0.5, dir.getStepZ() * 0.5);
                hitResult = new BlockHitResult(faceCenter, dir, blockPos, false);
            } else {
                Optional<BlockHitResult> hitOpt = rayTraceBlock(level, player, player.getEyePosition(), blockPos, blockReachDistance);
                if (hitOpt.isEmpty()) {
                    sendFailure(messageId, "Block not reachable");
                    return;
                }
                hitResult = hitOpt.get();
            }

            if (command.getLookAtBlock()) {
                BlockFaceUtil.applyRotationToward(player, player.getEyePosition(), hitResult.getLocation());
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

            Common.BlockPos from = command.hasFromPosition() ? command.getFromPosition() : null;
            double baseX = from != null ? from.getX() + 0.5 : player.getX();
            double baseY = from != null ? from.getY()       : player.getY();
            double baseZ = from != null ? from.getZ() + 0.5 : player.getZ();
            Vec3 eyePos = new Vec3(baseX, baseY + player.getEyeHeight(command.getSneak() ? Pose.CROUCHING : Pose.STANDING), baseZ);

            int faceOrdinal = command.getFace().getNumber();
            boolean reachable;
            if (faceOrdinal == 0) { // AUTO - check all faces
                reachable = rayTraceBlock(level, player, eyePos, blockPos, blockReachDistance).isPresent();
            } else {
                reachable = rayTraceBlockFace(level, player, eyePos, blockPos, blockReachDistance, faceOrdinal);
            }
            sendCanReachBlockResponse(messageId, reachable);

        } catch (Exception e) {
            LOGGER.error("Error checking reachability", e);
            sendCanReachBlockResponse(messageId, false);
        }
    }

    private Optional<BlockHitResult> rayTraceBlock(ClientLevel level, LocalPlayer player, Vec3 eyePos, BlockPos blockPos, double reachDistance) {
        VoxelShape shape = level.getBlockState(blockPos).getShape(level, blockPos);
        if (shape.isEmpty()) return Optional.empty();
        AABB aabb = shape.bounds();
        for (Direction face : Direction.values()) {
            for (Vec3 target : BlockFaceUtil.faceCandidates(blockPos, face, aabb)) {
                if (eyePos.distanceTo(target) > reachDistance) continue;
                Vec3 end = BlockFaceUtil.extendRay(eyePos, target);
                BlockHitResult hit = level.clip(new ClipContext(
                    eyePos, end,
                    ClipContext.Block.OUTLINE,
                    ClipContext.Fluid.NONE,
                    player
                ));
                if (hit.getType() == HitResult.Type.BLOCK && hit.getBlockPos().equals(blockPos)) {
                    return Optional.of(hit);
                }
            }
        }
        return Optional.empty();
    }

    private boolean rayTraceBlockFace(ClientLevel level, LocalPlayer player, Vec3 eyePos, BlockPos blockPos, double reachDistance, int faceOrdinal) {
        Direction face = BlockFaceUtil.faceFromOrdinal(faceOrdinal);
        if (face == null) return false;
        VoxelShape shape = level.getBlockState(blockPos).getShape(level, blockPos);
        if (shape.isEmpty()) return false;
        AABB aabb = shape.bounds();
        for (Vec3 target : BlockFaceUtil.faceCandidates(blockPos, face, aabb)) {
            if (eyePos.distanceTo(target) > reachDistance) continue;
            Vec3 end = BlockFaceUtil.extendRay(eyePos, target);
            BlockHitResult hit = level.clip(new ClipContext(
                eyePos, end,
                ClipContext.Block.OUTLINE,
                ClipContext.Fluid.NONE,
                player
            ));
            if (hit.getType() == HitResult.Type.BLOCK && hit.getBlockPos().equals(blockPos) && hit.getDirection() == face) {
                return true;
            }
        }
        return false;
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