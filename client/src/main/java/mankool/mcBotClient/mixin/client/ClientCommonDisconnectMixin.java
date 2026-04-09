package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.McBotClient;
import mankool.mcBotClient.handler.MessageHandler;
import net.minecraft.client.multiplayer.ClientCommonPacketListenerImpl;
import net.minecraft.network.DisconnectionDetails;
import net.minecraft.network.protocol.common.ClientboundDisconnectPacket;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ClientCommonPacketListenerImpl.class)
public class ClientCommonDisconnectMixin {

    @Unique
    private volatile boolean handledByPacket = false;

    @Inject(method = "handleDisconnect", at = @At("HEAD"))
    private void onCommonDisconnect(ClientboundDisconnectPacket packet, CallbackInfo ci) {
        handledByPacket = true;
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

    @Inject(method = "onDisconnect", at = @At("HEAD"))
    private void onAnyDisconnect(DisconnectionDetails details, CallbackInfo ci) {
        if (!handledByPacket) {
            // No disconnect packet was received - this is an abrupt connection drop,
            // which may indicate a proxy failure
            McBotClient instance = McBotClient.getInstance();
            if (instance != null) {
                MessageHandler handler = instance.getMessageHandler();
                if (handler != null) {
                    handler.getServerOutbound().sendNetworkDropStatus();
                }
            }
        }
        handledByPacket = false;
    }
}
