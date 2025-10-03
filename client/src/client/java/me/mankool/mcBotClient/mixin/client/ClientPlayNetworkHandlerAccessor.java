package me.mankool.mcBotClient.mixin.client;

import net.minecraft.client.network.ClientPlayNetworkHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(ClientPlayNetworkHandler.class)
public interface ClientPlayNetworkHandlerAccessor {
    @Invoker("isSecureChatEnforced")
    boolean callIsSecureChatEnforced();
}