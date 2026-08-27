package mankool.mcBotClient.mixin.client;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.mojang.blaze3d.platform.Window;
import mankool.mcBotClient.util.BotWindow;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Applies {@link BotWindow#applyCreationHints()} right before the game window is created.
 *
 * <p>The {@code glfwCreateWindow} call lives in the {@code Window} constructor on 1.21.x and in the
 * static {@code createGlfwWindow} on 26.1+; both follow the game's own hint setup, so wrapping the
 * call itself is the one spot that is after {@code glfwDefaultWindowHints} on every version. A
 * static {@code @WrapOperation} handler is valid for both an instance and a static target, and a
 * target name that does not exist on the current version simply matches nothing.
 */
@Mixin(Window.class)
public class WindowMixin {

    @WrapOperation(
        method = {"<init>", "createGlfwWindow"},
        at = @At(value = "INVOKE", target = "Lorg/lwjgl/glfw/GLFW;glfwCreateWindow(IILjava/lang/CharSequence;JJ)J")
    )
    private static long mcbot$createWindow(int width, int height, CharSequence title, long monitor, long share,
                                           Operation<Long> original) {
        BotWindow.applyCreationHints();
        return original.call(width, height, title, monitor, share);
    }
}
