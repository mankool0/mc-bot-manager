package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.util.ProjectileOwnerAccess;
import net.minecraft.network.protocol.game.ClientboundAddEntityPacket;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.projectile.Projectile;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.util.UUID;

@Mixin(Projectile.class)
public abstract class ProjectileOwnerMixin implements ProjectileOwnerAccess {

    @Unique
    private int mcbot$ownerEntityId = 0;

    @Unique
    private UUID mcbot$ownerUuid = null;

    // TAIL specifically: vanilla has just tried to resolve the packet's owner id against
    // this client's level, so getOwner() is non-null exactly when that succeeded. It
    // keeps nothing when it fails, which is why the raw id is worth storing.
    @Inject(method = "recreateFromPacket", at = @At("TAIL"))
    private void mcbot$captureOwner(ClientboundAddEntityPacket packet, CallbackInfo ci) {
        mcbot$ownerEntityId = packet.getData();

        Entity owner = ((Projectile) (Object) this).getOwner();
        if (owner != null) {
            mcbot$ownerUuid = owner.getUUID();
        }
    }

    @Override
    public int mcbot$ownerEntityId() {
        return mcbot$ownerEntityId;
    }

    @Override
    public UUID mcbot$ownerUuid() {
        return mcbot$ownerUuid;
    }

    @Override
    public void mcbot$setOwnerUuid(UUID uuid) {
        mcbot$ownerUuid = uuid;
    }
}
