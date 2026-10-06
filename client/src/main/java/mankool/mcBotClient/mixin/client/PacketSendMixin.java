package mankool.mcBotClient.mixin.client;

import com.llamalad7.mixinextras.sugar.Local;
import mankool.mcBotClient.plugin.PluginHost;
import net.minecraft.network.Connection;
import net.minecraft.network.protocol.Packet;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Connection.class)
public class PacketSendMixin {

    // Every send funnels into the three-argument send; its listener type changed after 1.21.4.
    @Inject(method = {
        "send(Lnet/minecraft/network/protocol/Packet;Lnet/minecraft/network/PacketSendListener;Z)V",
        "send(Lnet/minecraft/network/protocol/Packet;Lio/netty/channel/ChannelFutureListener;Z)V"
    }, at = @At("HEAD"), cancellable = true)
    private void mcbot$send(CallbackInfo ci, @Local(argsOnly = true) Packet<?> packet) {
        if (!PluginHost.get().allowSend(packet)) {
            ci.cancel();
        }
    }
}
