package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.McBotClient;
import mankool.mcBotClient.handler.MessageHandler;
import mankool.mcBotClient.handler.outbound.ServerOutbound;
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

    @Unique
    private volatile boolean statusReported = false;

    @Inject(method = "handleDisconnect", at = @At("HEAD"))
    private void onCommonDisconnect(ClientboundDisconnectPacket packet, CallbackInfo ci) {
        handledByPacket = true;
        String reason = packet.reason().getString();
        if (reason.toLowerCase().contains("invalid session")) {
            ServerOutbound outbound = mcbot$serverOutbound();
            if (outbound != null) {
                outbound.sendInvalidSessionStatus();
                statusReported = true;
            }
        }
    }

    @Inject(method = "onDisconnect", at = @At("HEAD"))
    private void onAnyDisconnect(DisconnectionDetails details, CallbackInfo ci) {
        // Every disconnect has to reach the manager, since that is what makes it drop
        // the world state cached for this session. A kick arrives as a packet and used
        // to be reported only when it named an invalid session, which left the manager
        // believing the bot was still playing.
        if (!statusReported) {
            ServerOutbound outbound = mcbot$serverOutbound();
            if (outbound != null) {
                if (handledByPacket) {
                    outbound.sendDisconnectedStatus(details.reason().getString());
                } else {
                    // No disconnect packet was received - this is an abrupt connection
                    // drop, which may indicate a proxy failure
                    outbound.sendNetworkDropStatus();
                }
            }
        }
        handledByPacket = false;
        statusReported = false;
    }

    @Unique
    private ServerOutbound mcbot$serverOutbound() {
        McBotClient instance = McBotClient.getInstance();
        if (instance == null) {
            return null;
        }
        MessageHandler handler = instance.getMessageHandler();
        return handler != null ? handler.getServerOutbound() : null;
    }
}
