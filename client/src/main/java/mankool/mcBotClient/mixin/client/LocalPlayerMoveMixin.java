package mankool.mcBotClient.mixin.client;

import com.llamalad7.mixinextras.sugar.Local;
import mankool.mcBotClient.plugin.PluginHost;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(LocalPlayer.class)
public class LocalPlayerMoveMixin {

    // The player's own step only: a piston's or a shulker's push moves it by what it moves it by.
    @ModifyVariable(method = "move", at = @At("HEAD"), argsOnly = true)
    private Vec3 mcbot$move(Vec3 movement, @Local(argsOnly = true) MoverType type) {
        return type == MoverType.SELF ? PluginHost.get().move((LocalPlayer) (Object) this, movement) : movement;
    }

    // Baritone turns the player for the packet after the move, so a turn for the packet goes here.
    @Inject(method = "sendPosition", at = @At("HEAD"))
    private void mcbot$beforeMovePacket(CallbackInfo ci) {
        PluginHost.get().beforeMovePacket();
    }
}
