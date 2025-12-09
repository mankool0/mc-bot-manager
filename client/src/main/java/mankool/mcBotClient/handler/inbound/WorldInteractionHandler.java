package mankool.mcBotClient.handler.inbound;

import baritone.api.BaritoneAPI;
import baritone.api.utils.RayTraceUtils;
import baritone.api.utils.Rotation;
import baritone.api.utils.RotationUtils;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcbot.protocol.Common;
import mankool.mcbot.protocol.World;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Optional;

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

            Optional<Rotation> rot = RotationUtils.reachableOffset(
                ctx,
                blockPos,
                Vec3.atCenterOf(blockPos),
                blockReachDistance,
                command.getSneak()
            );

            if (rot.isEmpty()) {
                sendFailure(messageId, "Block not reachable");
                return;
            }

            HitResult rayTraceResult = RayTraceUtils.rayTraceTowards(player, rot.get(), blockReachDistance, command.getSneak());
            if (!(rayTraceResult instanceof BlockHitResult hitResult)) {
                sendFailure(messageId, "Could not ray trace to block");
                return;
            }

            LOGGER.debug("Interacting with {} - hit: {}, face: {}",
                blockPos.toShortString(), hitResult.getLocation(), hitResult.getDirection());

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
}