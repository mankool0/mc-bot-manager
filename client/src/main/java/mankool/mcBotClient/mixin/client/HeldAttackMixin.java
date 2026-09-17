package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.handler.inbound.WorldInteractionHandler;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;

/**
 * Reports a held attack from the manager as the attack button being down, so vanilla digs it.
 *
 * <p>Driving {@code continueDestroyBlock} from the handler instead does not work: the same tick's
 * {@code continueAttack(false)} aborts the dig, restarting the break every tick.
 */
@Mixin(Minecraft.class)
public class HeldAttackMixin {

    @ModifyVariable(method = "continueAttack(Z)V", at = @At("HEAD"), argsOnly = true, ordinal = 0)
    private boolean mcbot$holdAttack(boolean down) {
        return down || WorldInteractionHandler.isHoldingAttack();
    }
}
