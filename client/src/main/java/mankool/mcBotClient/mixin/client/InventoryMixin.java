package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.handler.outbound.InventoryOutbound;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Inventory.class)
public class InventoryMixin {

    @Shadow
    public Player player;

    @Inject(method = "setItem", at = @At("TAIL"))
    private void onSetItem(int slot, ItemStack itemStack, CallbackInfo ci) {
        notifyInventoryChange();
    }

    @Inject(method = "setSelectedSlot", at = @At("TAIL"))
    private void onSetSelectedSlot(int slot, CallbackInfo ci) {
        notifyInventoryChange();
    }

    @Inject(method = "removeItem(II)Lnet/minecraft/world/item/ItemStack;", at = @At("TAIL"))
    private void onRemoveItem(int slot, int count, CallbackInfoReturnable<ItemStack> cir) {
        notifyInventoryChange();
    }

    @Inject(method = "removeFromSelected", at = @At("TAIL"))
    private void onRemoveFromSelected(boolean dropAll, CallbackInfoReturnable<ItemStack> cir) {
        notifyInventoryChange();
    }

    @Unique
    private void notifyInventoryChange() {
        Minecraft mc = Minecraft.getInstance();
        if (mc.player != null && this.player == mc.player) {
            InventoryOutbound handler = InventoryOutbound.getInstance();
            if (handler != null) {
                handler.queueUpdate();
            }
        }
    }
}
