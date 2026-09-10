package mankool.mcBotClient.util;

import java.util.UUID;

// Implemented on Projectile by ProjectileOwnerMixin, which keeps the owner the spawn
// packet named. Held on the entity so it dies with it: a side map would be keyed by an
// id the server later hands to something else.
public interface ProjectileOwnerAccess {

    // Owner entity id as the server sent it, or 0 if it named no owner.
    int mcbot$ownerEntityId();

    // Owner uuid if this client has ever resolved the id, otherwise null.
    UUID mcbot$ownerUuid();

    void mcbot$setOwnerUuid(UUID uuid);
}
