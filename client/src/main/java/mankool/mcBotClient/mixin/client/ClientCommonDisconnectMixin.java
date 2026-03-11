package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.McBotClient;
import mankool.mcBotClient.handler.MessageHandler;
import net.minecraft.client.multiplayer.ClientCommonPacketListenerImpl;
import net.minecraft.network.protocol.common.ClientboundDisconnectPacket;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ClientCommonPacketListenerImpl.class)
public class ClientCommonDisconnectMixin {

    @Inject(method = "handleDisconnect", at = @At("HEAD"))
    private void onCommonDisconnect(ClientboundDisconnectPacket packet, CallbackInfo ci) {
        String reason = packet.reason().getString();
        if (reason.toLowerCase().contains("invalid session")) {
            McBotClient instance = McBotClient.getInstance();
            if (instance != null) {
                MessageHandler handler = instance.getMessageHandler();
                if (handler != null) {
                    handler.getServerOutbound().sendInvalidSessionStatus();
                }
            }
        }
    }
}
