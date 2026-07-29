package mankool.mcBotClient.integration;

import net.fabricmc.loader.api.FabricLoader;
import net.fabricmc.loader.api.entrypoint.EntrypointContainer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;

/**
 * Discovers {@link ClientIntegration}s via the {@code mcbot:integration} Fabric entrypoint and exposes
 * their aggregate capabilities. Each integration is isolated so a broken one cannot take down the others.
 */
public final class IntegrationRegistry {
    private static final Logger LOGGER = LoggerFactory.getLogger(IntegrationRegistry.class);
    private static final String ENTRYPOINT_KEY = "mcbot:integration";

    private static List<ClientIntegration> integrations;

    private IntegrationRegistry() {}

    public static synchronized List<ClientIntegration> integrations() {
        if (integrations == null) {
            List<ClientIntegration> found = new ArrayList<>();
            for (EntrypointContainer<ClientIntegration> container :
                    FabricLoader.getInstance().getEntrypointContainers(ENTRYPOINT_KEY, ClientIntegration.class)) {
                try {
                    ClientIntegration integration = container.getEntrypoint();
                    if (!integration.isAvailable()) {
                        LOGGER.info("Integration {} is compiled in but its mod is not loaded - skipping",
                                integration.getClass().getName());
                        continue;
                    }
                    found.add(integration);
                    LOGGER.info("Discovered integration {} (capabilities: {})",
                            integration.getClass().getName(), integration.capabilities());
                } catch (Throwable t) {
                    LOGGER.error("Failed to load integration from mod {}",
                            container.getProvider().getMetadata().getId(), t);
                }
            }
            integrations = List.copyOf(found);
        }
        return integrations;
    }

    public static List<String> capabilities() {
        List<String> caps = new ArrayList<>();
        for (ClientIntegration integration : integrations()) {
            caps.addAll(integration.capabilities());
        }
        return caps;
    }

    public static void initializeAll(InitContext ctx) {
        for (ClientIntegration integration : integrations()) {
            try {
                integration.onInitialize(ctx);
            } catch (Throwable t) {
                LOGGER.error("Integration {} failed onInitialize", integration.getClass().getName(), t);
            }
        }
    }
}
