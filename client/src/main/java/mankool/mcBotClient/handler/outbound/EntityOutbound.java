package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Entities;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.util.ProtoUtil;
import mankool.mcBotClient.util.VersionCompat;
import net.minecraft.client.Minecraft;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

public class EntityOutbound extends BaseOutbound {

    private static class EntitySnapshot {
        final double x, y, z;
        final float yaw, pitch;

        EntitySnapshot(Entity e) {
            this.x = e.getX();
            this.y = e.getY();
            this.z = e.getZ();
            this.yaw = e.getYRot();
            this.pitch = e.getXRot();
        }
    }

    private final Map<Integer, EntitySnapshot> lastSnapshot = new HashMap<>();

    public EntityOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(Minecraft client) {
        if (client.level == null) {
            return;
        }

        Entities.EntityUpdate.Builder builder = Entities.EntityUpdate.newBuilder()
                .setDimension(VersionCompat.keyId(client.level.dimension()));

        Set<Integer> currentIds = new HashSet<>();

        for (Entity entity : client.level.entitiesForRendering()) {
            int id = entity.getId();
            currentIds.add(id);

            EntitySnapshot snap = lastSnapshot.get(id);
            boolean isNew = (snap == null);
            boolean changed = false;

            if (!isNew) {
                changed = entity.getX() != snap.x || entity.getY() != snap.y || entity.getZ() != snap.z
                     || entity.getYRot() != snap.yaw || entity.getXRot() != snap.pitch;
            }

            if (isNew || changed) {
                Entities.EntityData data = buildEntityData(entity);
                if (data != null) {
                    builder.addUpserted(data);
                    lastSnapshot.put(id, new EntitySnapshot(entity));
                }
            }
        }

        // Find removed entities
        Set<Integer> removed = new HashSet<>(lastSnapshot.keySet());
        removed.removeAll(currentIds);
        for (int removedId : removed) {
            builder.addRemovedIds(removedId);
            lastSnapshot.remove(removedId);
        }

        if (builder.getUpsertedCount() > 0 || builder.getRemovedIdsCount() > 0) {
            Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
                    .setMessageId(UUID.randomUUID().toString())
                    .setTimestamp(System.currentTimeMillis())
                    .setEntityUpdate(builder.build())
                    .build();
            connection.sendMessage(message);
        }
    }

    private static float normalizeYaw(float yaw) {
        float result = yaw % 360f;
        if (result < -180f) result += 360f;
        if (result > 180f)  result -= 360f;
        return result;
    }

    private Entities.EntityData buildEntityData(Entity entity) {
        String typeId = VersionCompat.registryGetKeyId(BuiltInRegistries.ENTITY_TYPE, entity.getType());
        if (typeId == null) {
            return null;
        }

        Vec3 vel = entity.getDeltaMovement();

        Entities.EntityData.Builder builder = Entities.EntityData.newBuilder()
                .setEntityId(entity.getId())
                .setUuid(entity.getUUID().toString())
                .setType(typeId)
                .setX(entity.getX())
                .setY(entity.getY())
                .setZ(entity.getZ())
                .setYaw(normalizeYaw(entity.getYRot()))
                .setPitch(entity.getXRot())
                .setVelX(vel.x)
                .setVelY(vel.y)
                .setVelZ(vel.z);

        if (entity instanceof LivingEntity living) {
            builder.setIsLiving(true)
                   .setHealth(living.getHealth())
                   .setMaxHealth(living.getMaxHealth());
        }

        if (entity instanceof ItemEntity itemEntity) {
            builder.setIsItem(true)
                   .setItemStack(ProtoUtil.buildItemStack(itemEntity.getItem(), 0));
        }

        if (entity instanceof Player player) {
            builder.setIsPlayer(true)
                   .setPlayerName(player.getName().getString());
        }

        return builder.build();
    }
}
