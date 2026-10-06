package mankool.mcBotClient.plugin;

import java.util.Map;

/**
 * One load of one plugin. A reload gets a new loader, so the old classes go once nothing holds
 * the old plugin. Everything else - the game, the mods, this mod - comes from the parent.
 */
final class PluginClassLoader extends ClassLoader {
    private final Map<String, byte[]> classes;

    PluginClassLoader(String plugin, Map<String, byte[]> classes, ClassLoader parent) {
        super("mcbot-plugin-" + plugin, parent);
        this.classes = classes;
    }

    @Override
    protected Class<?> findClass(String name) throws ClassNotFoundException {
        byte[] bytes = classes.get(name);
        if (bytes == null) {
            throw new ClassNotFoundException(name);
        }
        return defineClass(name, bytes, 0, bytes.length);
    }
}
