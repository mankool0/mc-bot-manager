package mankool.mcBotClient.mixin.client;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import mankool.mcBotClient.plugin.PluginHost;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.network.PacketListener;
import net.minecraft.network.protocol.Packet;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

@Mixin(ClientPacketListener.class)
public class PacketBundleMixin {

    // A bundle's packets are handled straight from the bundle, never queued on their own.
    @WrapOperation(
        method = "handleBundlePacket",
        at = @At(value = "INVOKE", target = "Lnet/minecraft/network/protocol/Packet;handle(Lnet/minecraft/network/PacketListener;)V")
    )
    private void mcbot$receivedInBundle(Packet<?> packet, PacketListener listener, Operation<Void> original) {
        if (PluginHost.get().allowReceive(packet)) {
            original.call(packet, listener);
        }
    }
}
