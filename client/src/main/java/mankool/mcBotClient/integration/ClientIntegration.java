package mankool.mcBotClient.integration;

import java.util.List;

/**
 * A pluggable client integration
 *
 * Implementations are discovered via the {@code mcbot:integration} Fabric entrypoint and are only
 * present when their optional source set was compiled in (see build.gradle enable_* flags).
 */
public interface ClientIntegration {

    /** Capability strings advertised to the manager, which gates optional features on them. */
    List<String> capabilities();

    /**
     * Whether the backing mod is actually installed. An integration can be compiled into the jar yet
     * absent from the game, in which case the registry skips it entirely.
     */
    default boolean isAvailable() {
        return true;
    }

    /** Called once per JVM at mod init, for JVM-global setup (event buses, install-once hooks). */
    default void onInitialize(InitContext ctx) {}

    /** Called once per pipe connection, to build connection-scoped handlers and tick hooks. */
    void onConnectionSetup(ConnectionContext ctx);
}
