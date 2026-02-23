package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Player;
import mankool.mcbot.protocol.Protocol;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.util.ProtoUtil;
import java.util.UUID;

public class PlayerOutbound extends BaseOutbound {

    public PlayerOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(Minecraft client) {
        if (client.player == null || client.level == null) {
            return;
        }
        sendUpdate();
    }

    private void sendUpdate() {
        LocalPlayer player = client.player;
        if (player == null) return;

        Player.PlayerStateUpdate update = Player.PlayerStateUpdate.newBuilder()
            .setUuid(player.getUUID().toString())
            .setName(player.getName().getString())
            .setPosition(ProtoUtil.toProtoVec3d(player.getX(), player.getY(), player.getZ()))
            .setVelocity(ProtoUtil.toProtoVec3d(player.getDeltaMovement()))
            .setYaw(player.getYRot())
            .setPitch(player.getXRot())
            .setOnGround(player.onGround())
            .setHealth(player.getHealth())
            .setFoodLevel(player.getFoodData().getFoodLevel())
            .setSaturation(player.getFoodData().getSaturationLevel())
            .setAir(player.getAirSupply())
            .setExperienceLevel(player.experienceLevel)
            .setExperienceProgress(player.experienceProgress)
            .setTotalExperience(player.totalExperience)
            .setSprinting(player.isSprinting())
            .setCrouching(player.isShiftKeyDown())
            .setSwimming(player.isSwimming())
            .setFlying(player.getAbilities().flying)
            .setBurning(player.isOnFire())
            .setAbsorption(player.getAbsorptionAmount())
            .setDimension(player.level().dimension().identifier().toString())
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setPlayerState(update)
            .build();

        connection.sendMessage(message);
    }
}