package mankool.mcBotClient.mixin.client;

import com.mojang.blaze3d.platform.GLX;
import mankool.mcBotClient.util.BotWindow;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Runs {@link BotWindow#applyInitHints()} before the game calls {@code glfwInit}, so the GLFW
 * platform choice is made before it is fixed for the process.
 */
@Mixin(GLX.class)
public class GlfwInitMixin {

    @Inject(method = "_initGlfw", at = @At("HEAD"))
    private static void mcbot$beforeGlfwInit(CallbackInfoReturnable<?> cir) {
        BotWindow.applyInitHints();
    }
}
