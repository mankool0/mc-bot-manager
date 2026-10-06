package mankool.mcBotClient.plugin;

/**
 * A client plugin: Java source a script sends with {@code bot.load_plugin}, compiled and loaded by
 * the client while it runs. The main class needs a public no-argument constructor. Every callback
 * runs on the game thread.
 */
public interface ClientPlugin {
    /** Register hooks and message handlers. A throw fails the load. */
    void onLoad(PluginContext ctx) throws Exception;

    /** Unloaded, replaced or failed. Its hooks are already gone. */
    default void onUnload() {}
}
