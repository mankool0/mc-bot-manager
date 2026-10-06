package mankool.mcBotClient.mixin.client;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import mankool.mcBotClient.plugin.PluginHost;
import net.minecraft.network.PacketListener;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.PacketUtils;
import net.minecraft.network.protocol.game.ClientboundBundlePacket;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

@Mixin(PacketUtils.class)
public class PacketReceiveMixin {

    // The task ensureRunningOnSameThread queues, where the game thread calls the handler; Mojang's
    // mappings leave the lambda unnamed. A bundle's packets go through PacketBundleMixin instead.
    @WrapOperation(
        method = "method_11072(Lnet/minecraft/network/PacketListener;Lnet/minecraft/network/protocol/Packet;)V",
        at = @At(value = "INVOKE", target = "Lnet/minecraft/network/protocol/Packet;handle(Lnet/minecraft/network/PacketListener;)V")
    )
    private static void mcbot$received(Packet<?> packet, PacketListener listener, Operation<Void> original) {
        if (packet instanceof ClientboundBundlePacket || PluginHost.get().allowReceive(packet)) {
            original.call(packet, listener);
        }
    }
}
