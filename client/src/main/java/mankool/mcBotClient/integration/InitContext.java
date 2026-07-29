package mankool.mcBotClient.integration;

import net.minecraft.client.Minecraft;

/** Context passed to {@link ClientIntegration#onInitialize} at mod init time. */
public interface InitContext {
    /** May be null this early in startup; integrations should not rely on it. */
    Minecraft client();
}
