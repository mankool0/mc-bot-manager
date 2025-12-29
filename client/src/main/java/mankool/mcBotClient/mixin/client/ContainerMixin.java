package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.handler.outbound.ContainerOutbound;
import mankool.mcBotClient.handler.outbound.InventoryOutbound;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.protocol.game.*;
import net.minecraft.world.inventory.MenuType;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ClientPacketListener.class)
public abstract class ContainerMixin {

    @Inject(method = "handleOpenScreen", at = @At("TAIL"))
    private void onOpenScreen(ClientboundOpenScreenPacket packet, CallbackInfo ci) {
        ContainerOutbound handler = ContainerOutbound.getInstance();
        if (handler != null) {
            int containerId = packet.getContainerId();
            MenuType<?> menuType = packet.getType();
            String containerType = BuiltInRegistries.MENU.getKey(menuType).toString();

            handler.onContainerOpened(containerId, containerType, null);
        }
    }

    @Inject(method = "handleContainerClose", at = @At("TAIL"))
    private void onContainerClose(ClientboundContainerClosePacket packet, CallbackInfo ci) {
        ContainerOutbound handler = ContainerOutbound.getInstance();
        if (handler != null) {
            handler.onContainerClosed(packet.getContainerId());
        }
    }

    @Inject(method = "handleContainerContent", at = @At("TAIL"))
    private void onContainerContent(ClientboundContainerSetContentPacket packet, CallbackInfo ci) {
        ContainerOutbound handler = ContainerOutbound.getInstance();
        if (handler != null) {
            handler.onContainerContentSet(packet.containerId());
        }
    }

    @Inject(method = "handleContainerSetSlot", at = @At("TAIL"))
    private void onContainerSetSlot(ClientboundContainerSetSlotPacket packet, CallbackInfo ci) {
        ContainerOutbound handler = ContainerOutbound.getInstance();
        if (handler != null) {
            handler.onContainerSlotChanged(packet.getContainerId(), packet.getSlot());
        }
    }
}
