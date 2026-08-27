package mankool.mcBotClient.util;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;

/**
 * Maps Minecraft GUI class names back to their Mojang names.
 *
 * <p>Fabric runtime remaps Minecraft to intermediary, so {@code Class#getName()} on an
 * obfuscated version (1.21.x) reports {@code net.minecraft.class_419} rather than
 * {@code net.minecraft.client.gui.screens.DisconnectedScreen} - useless to the manager and to
 * scripts matching on screen class names. Loom's mappings exist only at build time, so
 * {@code generateGuiClassNames} bakes the GUI slice of them into
 * {@code assets/mc-bot-client/gui-class-names.properties} and this reads it back.
 *
 * <p>Unmapped names (mod screens, dev runs with named mappings, unobfuscated 26.1+) pass through
 * untouched, so every version reports the same Mojang names.
 */
public final class GuiClassNames {
    private static final Logger LOGGER = LoggerFactory.getLogger(GuiClassNames.class);
    private static final String RESOURCE = "/assets/mc-bot-client/gui-class-names.properties";

    private static final Map<String, String> NAMES = load();

    private GuiClassNames() {}

    /** Fully qualified Mojang name of the class, or its runtime name if unmapped. */
    public static String of(Class<?> cls) {
        return NAMES.getOrDefault(cls.getName(), cls.getName());
    }

    /**
     * Innermost class name only, e.g. {@code DisconnectedScreen}. Empty for anonymous classes,
     * matching {@link Class#getSimpleName()} so callers can keep their existing fallbacks.
     */
    public static String simpleOf(Class<?> cls) {
        if (cls.getSimpleName().isEmpty()) return "";
        String name = of(cls);
        int cut = Math.max(name.lastIndexOf('.'), name.lastIndexOf('$'));
        return cut < 0 ? name : name.substring(cut + 1);
    }

    private static Map<String, String> load() {
        Properties props = new Properties();
        try (InputStream in = GuiClassNames.class.getResourceAsStream(RESOURCE)) {
            if (in == null) {
                LOGGER.warn("{} is missing, GUI class names will stay obfuscated", RESOURCE);
                return Map.of();
            }
            props.load(in);
        } catch (IOException e) {
            LOGGER.warn("Failed to read {}: {}", RESOURCE, e.getMessage());
            return Map.of();
        }

        Map<String, String> names = new HashMap<>();
        for (String intermediary : props.stringPropertyNames()) {
            names.put(intermediary, props.getProperty(intermediary));
        }
        return names;
    }
}
