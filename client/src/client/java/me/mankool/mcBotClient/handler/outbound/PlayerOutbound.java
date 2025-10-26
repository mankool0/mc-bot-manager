package me.mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Player;
import mankool.mcbot.protocol.Protocol;
import me.mankool.mcBotClient.connection.PipeConnection;
import me.mankool.mcBotClient.util.ProtoUtil;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;

import java.util.UUID;

public class PlayerOutbound extends BaseOutbound {

    public PlayerOutbound(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(MinecraftClient client) {
        if (client.player == null || client.world == null) {
            return;
        }
        sendUpdate();
    }

    private void sendUpdate() {
        ClientPlayerEntity player = client.player;
        if (player == null) return;

        Player.PlayerStateUpdate update = Player.PlayerStateUpdate.newBuilder()
            .setUuid(player.getUuid().toString())
            .setName(player.getName().getString())
            .setPosition(ProtoUtil.toProtoVec3d(player.getX(), player.getY(), player.getZ()))
            .setVelocity(ProtoUtil.toProtoVec3d(player.getVelocity()))
            .setYaw(player.getYaw())
            .setPitch(player.getPitch())
            .setOnGround(player.isOnGround())
            .setHealth(player.getHealth())
            .setFoodLevel(player.getHungerManager().getFoodLevel())
            .setSaturation(player.getHungerManager().getSaturationLevel())
            .setAir(player.getAir())
            .setExperienceLevel(player.experienceLevel)
            .setExperienceProgress(player.experienceProgress)
            .setTotalExperience(player.totalExperience)
            .setSprinting(player.isSprinting())
            .setCrouching(player.isSneaking())
            .setSwimming(player.isSwimming())
            .setFlying(player.getAbilities().flying)
            .setBurning(player.isOnFire())
            .setAbsorption(player.getAbsorptionAmount())
            .setDimension(player.getWorld().getRegistryKey().getValue().toString())
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setPlayerState(update)
            .build();

        connection.sendMessage(message);
    }
}