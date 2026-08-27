package mankool.mcBotClient.util;

import net.minecraft.client.Minecraft;
import net.minecraft.client.User;
import org.lwjgl.glfw.GLFW;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Locale;

/**
 * Makes the bot's game window identifiable and keeps it from grabbing focus.
 *
 * <p>Every bot window carries a window class of {@code mcbot-<account>} ({@code app_id} on Wayland,
 * {@code WM_CLASS} on X11) and the account name in its title, so compositor rules and the manager can
 * tell bots apart. The window is also created with the GLFW focus hints off, which stops it from
 * stealing focus on Windows and X11; Wayland compositors decide focus themselves, so there a window
 * rule keyed on the class is needed as well.
 *
 * <p>Called from {@code Minecraft}'s constructor before the mod's entrypoint runs, so this may only
 * read state that exists by then: {@code Minecraft.instance} and its user.
 */
public final class BotWindow {
    private static final Logger LOGGER = LoggerFactory.getLogger(BotWindow.class);

    /** Set {@code -Dmcbot.window.focus=true} to keep GLFW's default focus-on-create behaviour. */
    private static final String FOCUS_PROPERTY = "mcbot.window.focus";
    /** Set {@code -Dmcbot.window.nativeWayland=true} to let GLFW pick Wayland over X11 on Linux. */
    private static final String NATIVE_WAYLAND_PROPERTY = "mcbot.window.nativeWayland";
    private static final String APP_ID_PREFIX = "mcbot-";
    private static final String FALLBACK_NAME = "bot";

    private BotWindow() {}

    /** The account name the window is labelled with, or {@value FALLBACK_NAME} if unavailable. */
    public static String accountName() {
        Minecraft minecraft = Minecraft.getInstance();
        User user = minecraft != null ? minecraft.getUser() : null;
        String name = user != null ? user.getName() : null;
        return name == null || name.isBlank() ? FALLBACK_NAME : name;
    }

    /** Window class / Wayland app id: {@code mcbot-<account>}, lower-cased. */
    public static String appId() {
        return APP_ID_PREFIX + accountName().toLowerCase(Locale.ROOT);
    }

    /**
     * Prefers X11 (XWayland) over native Wayland when both are available. Wayland clients cannot
     * position their own windows, so the manager's window placement would be a no-op there; under
     * XWayland it works like on any X11 desktop. Minecraft 26.1+ makes the same choice on its own,
     * this extends it to 1.21.x. Must run before {@code glfwInit}.
     */
    public static void applyInitHints() {
        if (Boolean.getBoolean(NATIVE_WAYLAND_PROPERTY)) return;
        if (System.getenv("WAYLAND_DISPLAY") == null || System.getenv("DISPLAY") == null) return;
        if (!GLFW.glfwPlatformSupported(GLFW.GLFW_PLATFORM_WAYLAND) || !GLFW.glfwPlatformSupported(GLFW.GLFW_PLATFORM_X11)) return;
        GLFW.glfwInitHint(GLFW.GLFW_PLATFORM, GLFW.GLFW_PLATFORM_X11);
        LOGGER.info("Preferring X11 over Wayland for the game window (opt out with -D{}=true)", NATIVE_WAYLAND_PROPERTY);
    }

    /**
     * Sets the per-bot GLFW window hints. Must run after the game's own hints (they start with
     * {@code glfwDefaultWindowHints}) and before {@code glfwCreateWindow}. Platform-specific hints
     * are ignored by GLFW on other platforms.
     */
    public static void applyCreationHints() {
        String appId = appId();
        GLFW.glfwWindowHintString(GLFW.GLFW_WAYLAND_APP_ID, appId);
        GLFW.glfwWindowHintString(GLFW.GLFW_X11_CLASS_NAME, appId);
        GLFW.glfwWindowHintString(GLFW.GLFW_X11_INSTANCE_NAME, appId);

        boolean keepFocus = Boolean.getBoolean(FOCUS_PROPERTY);
        if (!keepFocus) {
            GLFW.glfwWindowHint(GLFW.GLFW_FOCUSED, GLFW.GLFW_FALSE);
            GLFW.glfwWindowHint(GLFW.GLFW_FOCUS_ON_SHOW, GLFW.GLFW_FALSE);
        }
        LOGGER.info("Bot window class '{}', focus on create {}", appId,
            keepFocus ? "kept (" + FOCUS_PROPERTY + ")" : "disabled");
    }

    /** Appends the account name to the game's window title. */
    public static String decorateTitle(String title) {
        return title + " [" + accountName() + "]";
    }
}
