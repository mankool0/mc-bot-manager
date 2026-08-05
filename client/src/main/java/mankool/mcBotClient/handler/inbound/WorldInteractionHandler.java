package mankool.mcBotClient.handler.inbound;

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

import java.util.List;
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
            .setRequestId(messageId)
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
            double blockReachDistance = VersionCompat.getBlockReachDistance(player);

            Common.BlockFace protoFace = command.getFace();
            Optional<BlockHitResult> hitOpt = (protoFace != Common.BlockFace.FACE_AUTO)
                ? rayTraceBlockFace(level, player, player.getEyePosition(), blockPos, blockReachDistance, protoFace.getNumber())
                : rayTraceBlock(level, player, player.getEyePosition(), blockPos, blockReachDistance);
            if (hitOpt.isEmpty()) {
                sendFailure(messageId, "Block not reachable");
                return;
            }
            BlockHitResult hitResult = hitOpt.get();

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

    // Eye position a query is traced from: its own from_position when set (centered on the block
    // in X/Z), otherwise the supplied player origin. Sneak selects the crouching eye height.
    private Vec3 reachEyePosition(LocalPlayer player, Vec3 origin, World.CanReachBlockCommand query) {
        Common.BlockPos from = query.hasFromPosition() ? query.getFromPosition() : null;
        double baseX = from != null ? from.getX() + 0.5 : origin.x;
        double baseY = from != null ? from.getY() : origin.y;
        double baseZ = from != null ? from.getZ() + 0.5 : origin.z;
        Pose pose = query.getSneak() ? Pose.CROUCHING : Pose.STANDING;
        return new Vec3(baseX, baseY + player.getEyeHeight(pose), baseZ);
    }

    private boolean evaluateReach(ClientLevel level, LocalPlayer player, Vec3 origin,
                                  double reachDistance, World.CanReachBlockCommand query) {
        Common.BlockPos protoPos = query.getPosition();
        BlockPos blockPos = new BlockPos(protoPos.getX(), protoPos.getY(), protoPos.getZ());
        try {
            Vec3 eyePos = reachEyePosition(player, origin, query);
            int faceOrdinal = query.getFace().getNumber();
            if (faceOrdinal == 0) { // AUTO - check all faces
                return rayTraceBlock(level, player, eyePos, blockPos, reachDistance).isPresent();
            }
            return rayTraceBlockFace(level, player, eyePos, blockPos, reachDistance, faceOrdinal).isPresent();
        } catch (Exception e) {
            LOGGER.error("Error checking reachability at {}", blockPos.toShortString(), e);
            return false;
        }
    }

    public void handleCanReachBlocks(String messageId, World.CanReachBlocksCommand command) {
        final int count = command.getQueriesCount();
        if (count == 0) {
            sendCanReachBlocksResponse(messageId, new boolean[0], 0);
            return;
        }

        LocalPlayer player = client.player;
        ClientLevel level = client.level;
        if (player == null || level == null) {
            sendCanReachBlocksResponse(messageId, new boolean[0], 0);
            return;
        }

        Vec3 origin = new Vec3(player.getX(), player.getY(), player.getZ());

        double reachDistance;
        try {
            reachDistance = VersionCompat.getBlockReachDistance(player);
        } catch (Exception e) {
            LOGGER.error("Could not determine block reach distance", e);
            sendCanReachBlocksResponse(messageId, new boolean[0], 0);
            return;
        }

        boolean[] results = new boolean[count];
        List<World.CanReachBlockCommand> queries = command.getQueriesList();
        for (int i = 0; i < count; i++) {
            results[i] = evaluateReach(level, player, origin, reachDistance, queries.get(i));
        }

        sendCanReachBlocksResponse(messageId, results, count);
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

    private Optional<BlockHitResult> rayTraceBlockFace(ClientLevel level, LocalPlayer player, Vec3 eyePos, BlockPos blockPos, double reachDistance, int faceOrdinal) {
        Direction face = BlockFaceUtil.faceFromOrdinal(faceOrdinal);
        if (face == null) return Optional.empty();
        VoxelShape shape = level.getBlockState(blockPos).getShape(level, blockPos);
        if (shape.isEmpty()) return Optional.empty();
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
                return Optional.of(hit);
            }
        }
        return Optional.empty();
    }

    private void sendCanReachBlocksResponse(String messageId, boolean[] reachable, int count) {
        World.CanReachBlocksResponse.Builder response = World.CanReachBlocksResponse.newBuilder()
            .setRequestId(messageId);
        for (int i = 0; i < count; i++) {
            response.addReachable(reachable[i]);
        }
        Protocol.ClientToManagerMessage msg = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setCanReachBlocksResponse(response.build())
            .build();
        connection.sendMessage(msg);
    }
}
