package mankool.mcBotClient.handler.outbound;

import com.mojang.blaze3d.platform.InputConstants;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.util.VersionCompat;
import mankool.mcbot.protocol.Input;
import mankool.mcbot.protocol.Protocol;
import net.minecraft.client.Minecraft;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public class HotkeyOutbound extends BaseOutbound {
    private static final Logger LOGGER = LoggerFactory.getLogger(HotkeyOutbound.class);

    private static final int MOD_SHIFT = 1;
    private static final int MOD_CONTROL = 2;
    private static final int MOD_ALT = 4;
    private static final int MOD_SUPER = 8;
    // Caps and num lock are toggle state rather than a held key, so a watch cannot ask for them.
    private static final int MOD_SUPPORTED = MOD_SHIFT | MOD_CONTROL | MOD_ALT | MOD_SUPER;

    private static final int KEY_LEFT_SHIFT = 340;
    private static final int KEY_LEFT_CONTROL = 341;
    private static final int KEY_LEFT_ALT = 342;
    private static final int KEY_LEFT_SUPER = 343;
    private static final int KEY_RIGHT_SHIFT = 344;
    private static final int KEY_RIGHT_CONTROL = 345;
    private static final int KEY_RIGHT_ALT = 346;
    private static final int KEY_RIGHT_SUPER = 347;

    private static class Watch {
        final String id;
        final int keyCode;
        final int modifiers;
        final boolean inScreens;
        boolean wasDown;

        Watch(Input.HotkeyWatch proto) {
            this.id = proto.getHotkeyId();
            this.keyCode = proto.getKeyCode();
            this.modifiers = proto.getModifiers() & MOD_SUPPORTED;
            this.inScreens = proto.getInScreens();
        }
    }

    private volatile List<Watch> watches = List.of();

    public HotkeyOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleSetHotkeys(Input.SetHotkeysCommand command) {
        List<Watch> parsed = new ArrayList<>();
        for (Input.HotkeyWatch watch : command.getHotkeysList()) {
            if (watch.getKeyCode() == InputConstants.UNKNOWN.getValue()) {
                LOGGER.warn("Ignoring hotkey '{}': no key code", watch.getHotkeyId());
                continue;
            }
            parsed.add(new Watch(watch));
        }
        watches = List.copyOf(parsed);
        LOGGER.info("Watching {} hotkey(s)", parsed.size());
    }

    @Override
    protected void onClientTick(Minecraft client) {
        List<Watch> current = watches;
        if (current.isEmpty()) {
            return;
        }

        // GLFW keeps the last key state it was told about, so an unfocused window can still read a
        // key as down - without this every bot on the machine reports the same press.
        if (!client.isWindowActive()) {
            for (Watch watch : current) {
                watch.wasDown = false;
            }
            return;
        }

        int held = heldModifiers(client);
        boolean inScreen = client.screen != null;

        for (Watch watch : current) {
            boolean down = isDown(client, watch.keyCode) && (held & watch.modifiers) == watch.modifiers;
            boolean wasDown = watch.wasDown;
            // Before the screen check, or a key held while a screen closes reads as a fresh press.
            watch.wasDown = down;
            if (down && !wasDown && (watch.inScreens || !inScreen)) {
                send(watch);
            }
        }
    }

    private int heldModifiers(Minecraft client) {
        int held = 0;
        if (isDown(client, KEY_LEFT_SHIFT) || isDown(client, KEY_RIGHT_SHIFT)) held |= MOD_SHIFT;
        if (isDown(client, KEY_LEFT_CONTROL) || isDown(client, KEY_RIGHT_CONTROL)) held |= MOD_CONTROL;
        if (isDown(client, KEY_LEFT_ALT) || isDown(client, KEY_RIGHT_ALT)) held |= MOD_ALT;
        if (isDown(client, KEY_LEFT_SUPER) || isDown(client, KEY_RIGHT_SUPER)) held |= MOD_SUPER;
        return held;
    }

    private boolean isDown(Minecraft client, int keyCode) {
        return VersionCompat.isKeyDown(client.getWindow(), keyCode);
    }

    private void send(Watch watch) {
        Input.HotkeyPressed event = Input.HotkeyPressed.newBuilder()
            .setHotkeyId(watch.id)
            .setKeyCode(watch.keyCode)
            .setModifiers(watch.modifiers)
            .build();
        Protocol.ClientToManagerMessage msg = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setHotkeyPressed(event)
            .build();
        connection.sendMessage(msg);
    }
}
