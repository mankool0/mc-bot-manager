package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.util.BotWindow;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Appends the account name to the window title. {@code createTitle} is what the game calls on
 * every title refresh (startup, connect, disconnect), so this covers all of them.
 */
@Mixin(Minecraft.class)
public class MinecraftTitleMixin {

    @Inject(method = "createTitle", at = @At("RETURN"), cancellable = true)
    private void mcbot$decorateTitle(CallbackInfoReturnable<String> cir) {
        cir.setReturnValue(BotWindow.decorateTitle(cir.getReturnValue()));
    }
}
