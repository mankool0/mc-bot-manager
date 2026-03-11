package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.McBotClient;
import mankool.mcBotClient.handler.MessageHandler;
import net.minecraft.client.multiplayer.ClientHandshakePacketListenerImpl;
import net.minecraft.network.DisconnectionDetails;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ClientHandshakePacketListenerImpl.class)
public class LoginDisconnectMixin {

    @Inject(method = "onDisconnect", at = @At("HEAD"))
    private void onLoginDisconnect(DisconnectionDetails details, CallbackInfo ci) {
        String reason = details.reason().getString();
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
