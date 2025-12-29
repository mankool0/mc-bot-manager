package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.handler.outbound.InventoryOutbound;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Inventory.class)
public class InventoryMixin {

    @Shadow
    public Player player;

    @Inject(method = "setItem", at = @At("TAIL"))
    private void onSetItem(int slot, ItemStack itemStack, CallbackInfo ci) {
        // Only send updates for the client player's inventory
        Minecraft mc = Minecraft.getInstance();
        if (mc.player != null && this.player == mc.player) {
            InventoryOutbound handler = InventoryOutbound.getInstance();
            if (handler != null) {
                handler.onInventorySlotChanged(slot);
            }
        }
    }
}
