package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Entities;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.util.ProjectileOwnerAccess;
import mankool.mcBotClient.util.ProtoUtil;
import mankool.mcBotClient.util.VersionCompat;
import com.google.protobuf.ByteString;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.network.chat.Component;
import net.minecraft.world.entity.Display;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.Interaction;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.decoration.BlockAttachedEntity;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.entity.vehicle.VehicleEntity;
import net.minecraft.world.phys.Vec3;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

public class EntityOutbound extends BaseOutbound {

    private static class EntitySnapshot {
        final double x, y, z;
        final float yaw, pitch;
        final float health;

        EntitySnapshot(Entity e) {
            this.x = e.getX();
            this.y = e.getY();
            this.z = e.getZ();
            this.yaw = e.getYRot();
            this.pitch = e.getXRot();
            this.health = healthOf(e);
        }

        // The server sends health 0 about a second before it removes a dying entity, and that
        // is the only thing telling a death apart from leaving view.
        boolean dying() {
            return health <= 0;
        }
    }

    private final Map<Integer, EntitySnapshot> lastSnapshot = new HashMap<>();
    private ClientLevel snapshotLevel;
    private String snapshotDimension;

    // How often an entity's full NBT is saved again to see whether it changed, in ticks. What
    // is on a frame or worn by an armor stand changes rarely and is the point of keeping it; a
    // mob's changes every tick and is kept only as it was last seen.
    private static final int NBT_FIXTURE_TICKS = 20;
    private static final int NBT_LIVING_TICKS = 200;

    private static class NbtState {
        int hash;
        long due;
    }

    private final Map<Integer, NbtState> nbtStates = new HashMap<>();
    private long tick;

    public EntityOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(Minecraft client) {
        ClientLevel level = client.level;
        if (level != snapshotLevel) {
            // Disconnecting or changing dimension takes everything out of view, under the old
            // level's dimension; ids from the next level must not match these snapshots.
            if (!lastSnapshot.isEmpty()) {
                Entities.EntityUpdate.Builder left = Entities.EntityUpdate.newBuilder()
                        .setDimension(snapshotDimension);
                addRemovals(left, new ArrayList<>(lastSnapshot.keySet()));
                send(left);
            }
            snapshotLevel = level;
            snapshotDimension = level != null ? VersionCompat.keyId(level.dimension()) : null;
            nbtStates.clear();
        }
        tick++;
        if (level == null) {
            return;
        }

        Entities.EntityUpdate.Builder builder = Entities.EntityUpdate.newBuilder()
                .setDimension(snapshotDimension);

        Set<Integer> currentIds = new HashSet<>();

        for (Entity entity : level.entitiesForRendering()) {
            int id = entity.getId();
            currentIds.add(id);

            EntitySnapshot snap = lastSnapshot.get(id);
            boolean isNew = (snap == null);
            boolean changed = false;

            if (!isNew) {
                changed = entity.getX() != snap.x || entity.getY() != snap.y || entity.getZ() != snap.z
                     || entity.getYRot() != snap.yaw || entity.getXRot() != snap.pitch
                     || healthOf(entity) != snap.health;
            }

            byte[] nbt = nbtIfChanged(entity, id);
            if (isNew || changed || nbt != null) {
                Entities.EntityData data = buildEntityData(entity, nbt);
                if (data != null) {
                    builder.addUpserted(data);
                    lastSnapshot.put(id, new EntitySnapshot(entity));
                }
            }
        }

        Set<Integer> removed = new HashSet<>(lastSnapshot.keySet());
        removed.removeAll(currentIds);
        addRemovals(builder, removed);

        send(builder);
    }

    // The entity's NBT when it is due a look and differs from what was last sent, else null.
    // Players and short-lived entities have none: a player's is not the world's, and an item
    // or an arrow is gone by the time anyone reads it.
    private byte[] nbtIfChanged(Entity entity, int id) {
        if (entity instanceof Player || isShortLived(entity)) {
            return null;
        }
        NbtState state = nbtStates.get(id);
        if (state != null && tick < state.due) {
            return null;
        }
        boolean first = state == null;
        if (first) {
            state = new NbtState();
            nbtStates.put(id, state);
        }
        // Due again whether or not this one could be saved: one that cannot is not retried
        // every tick.
        state.due = tick + (entity instanceof LivingEntity ? NBT_LIVING_TICKS : NBT_FIXTURE_TICKS);
        byte[] nbt = saveNbt(entity);
        if (nbt == null) {
            return null;
        }
        int hash = Arrays.hashCode(nbt);
        if (!first && hash == state.hash) {
            return null;
        }
        state.hash = hash;
        return nbt;
    }

    private static byte[] saveNbt(Entity entity) {
        try {
            CompoundTag tag = VersionCompat.saveEntity(entity);
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            tag.write(new DataOutputStream(bytes));
            return bytes.toByteArray();
        } catch (Exception e) {
            // Some entity's client-side state cannot be saved; it goes up without NBT.
            return null;
        }
    }

    private void addRemovals(Entities.EntityUpdate.Builder builder, Collection<Integer> ids) {
        for (int id : ids) {
            nbtStates.remove(id);
            if (lastSnapshot.remove(id).dying()) {
                builder.addDiedIds(id);
            } else {
                builder.addGoneFromViewIds(id);
            }
        }
    }

    private void send(Entities.EntityUpdate.Builder builder) {
        if (builder.getUpsertedCount() == 0 && builder.getGoneFromViewIdsCount() == 0
                && builder.getDiedIdsCount() == 0) {
            return;
        }
        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setEntityUpdate(builder.build())
                .build();
        connection.sendMessage(message);
    }

    // Non-living entities never die, so 1 keeps them from reading as dying.
    private static float healthOf(Entity entity) {
        return entity instanceof LivingEntity living ? living.getHealth() : 1f;
    }

    // Mobs, vehicles, item frames, paintings and displays stay where they were last seen; the
    // rest (items, XP, projectiles, falling blocks) is usually gone soon after leaving view.
    private static boolean isShortLived(Entity entity) {
        return !(entity instanceof LivingEntity || entity instanceof VehicleEntity
                || entity instanceof BlockAttachedEntity || entity instanceof Display
                || entity instanceof Interaction);
    }

    private static float normalizeYaw(float yaw) {
        float result = yaw % 360f;
        if (result < -180f) result += 360f;
        if (result > 180f)  result -= 360f;
        return result;
    }

    private Entities.EntityData buildEntityData(Entity entity, byte[] nbt) {
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
                .setVelZ(vel.z)
                .setShortLived(isShortLived(entity));
        if (nbt != null) {
            builder.setNbt(ByteString.copyFrom(nbt));
        }

        Component customName = entity.getCustomName();
        if (customName != null) {
            builder.setCustomName(customName.getString());
        }

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

        if (entity instanceof Projectile projectile) {
            ProjectileOwnerAccess owner = (ProjectileOwnerAccess) projectile;
            int ownerId = owner.mcbot$ownerEntityId();
            builder.setOwnerEntityId(ownerId);

            UUID ownerUuid = owner.mcbot$ownerUuid();
            // Vanilla resolves the owner id once, on the spawn packet, and never again.
            // Retrying picks up an owner who has since come into range.
            if (ownerUuid == null && ownerId != 0 && client.level != null) {
                Entity resolved = client.level.getEntity(ownerId);
                if (resolved != null) {
                    ownerUuid = resolved.getUUID();
                    owner.mcbot$setOwnerUuid(ownerUuid);
                }
            }
            if (ownerUuid != null) {
                builder.setOwnerUuid(ownerUuid.toString());
            }
        }

        return builder.build();
    }
}
