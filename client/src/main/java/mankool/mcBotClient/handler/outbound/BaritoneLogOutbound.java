package mankool.mcBotClient.handler.outbound;

import baritone.api.BaritoneAPI;
import baritone.api.Settings;
import mankool.mcbot.protocol.Baritone.BaritoneLogMessage;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.Minecraft;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.UUID;
import java.util.regex.Pattern;

// Forwards Baritone's client-local log output (chat log lines, toasts, desktop
// notifications) to the manager.
public class BaritoneLogOutbound {
    private static final Logger LOGGER = LoggerFactory.getLogger(BaritoneLogOutbound.class);

    // The text prefix Helper.getPrefix() produces
    private static final Pattern PREFIX = Pattern.compile("^\\[(Baritone|Baritoe|B)] ");

    // Handlers are recreated on every pipe reconnect, but the Baritone setting hooks must
    // be installed only once per JVM; they forward to the newest instance.
    private static volatile BaritoneLogOutbound instance;
    private static boolean hooksInstalled = false;

    private final PipeConnection connection;

    public BaritoneLogOutbound(Minecraft client, PipeConnection connection) {
        this.connection = connection;
        instance = this;
        installHooks();
    }

    private static synchronized void installHooks() {
        if (hooksInstalled) return;

        try {
            Settings settings = BaritoneAPI.getSettings();
            settings.logger.value = settings.logger.value.andThen(msg ->
                    forward(BaritoneLogMessage.Kind.CHAT, null, stripPrefix(msg.getString()), false));
            settings.toaster.value = settings.toaster.value.andThen((title, msg) ->
                    forward(BaritoneLogMessage.Kind.TOAST, title.getString(), msg.getString(), false));
            settings.notifier.value = settings.notifier.value.andThen((msg, error) ->
                    forward(BaritoneLogMessage.Kind.NOTIFICATION, null, msg, error));
            hooksInstalled = true;
            LOGGER.info("Installed Baritone log capture hooks");
        } catch (Exception e) {
            LOGGER.error("Failed to install Baritone log capture hooks", e);
        }
    }

    private static String stripPrefix(String text) {
        return PREFIX.matcher(text).replaceFirst("");
    }

    private static void forward(BaritoneLogMessage.Kind kind, String title, String content, boolean isError) {
        BaritoneLogOutbound self = instance;
        if (self == null) return;

        try {
            self.send(kind, title, content, isError);
        } catch (Exception e) {
            LOGGER.error("Failed to forward Baritone log message", e);
        }
    }

    private void send(BaritoneLogMessage.Kind kind, String title, String content, boolean isError) {
        BaritoneLogMessage.Builder builder = BaritoneLogMessage.newBuilder()
                .setKind(kind)
                .setContent(content)
                .setTimestamp(System.currentTimeMillis())
                .setIsError(isError);

        if (title != null) {
            builder.setTitle(title);
        }

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setBaritoneLog(builder.build())
                .build();

        connection.sendMessage(message);
    }
}
