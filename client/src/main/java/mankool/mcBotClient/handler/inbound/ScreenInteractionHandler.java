package mankool.mcBotClient.handler.inbound;

import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.handler.outbound.ScreenOutbound;
import mankool.mcBotClient.util.VersionCompat;
import mankool.mcbot.protocol.Commands;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ScreenInteractionHandler extends BaseInboundHandler {
    private static final Logger LOGGER = LoggerFactory.getLogger(ScreenInteractionHandler.class);

    private final ScreenOutbound screenOutbound;

    public ScreenInteractionHandler(Minecraft client, PipeConnection connection, ScreenOutbound screenOutbound) {
        super(client, connection);
        this.screenOutbound = screenOutbound;
    }

    public void handleClickScreenPosition(String messageId, Commands.ClickScreenPositionCommand command) {
        String expectedScreenId = command.getScreenId();
        String currentScreenId = screenOutbound.getCurrentScreenId();

        if (!expectedScreenId.equals(currentScreenId)) {
            sendFailure(messageId, "Screen changed: expected id=" + expectedScreenId + " but current is id=" + currentScreenId);
            return;
        }

        Screen screen = client.screen;
        if (screen == null) {
            sendFailure(messageId, "No screen open");
            return;
        }

        double x = command.getX();
        double y = command.getY();
        int button = command.getButton();

        client.execute(() -> {
            try {
                VersionCompat.screenMouseClicked(screen, x, y, button);

                sendSuccess(messageId, "Clicked screen at (" + (int)x + "," + (int)y + ")");
            } catch (Exception e) {
                LOGGER.error("Failed to click screen position: {}", e.getMessage(), e);
                sendFailure(messageId, "Failed to click: " + e.getMessage());
            }
        });
    }

    public void handleTypeText(String messageId, Commands.TypeTextCommand command) {
        String expectedScreenId = command.getScreenId();
        String currentScreenId = screenOutbound.getCurrentScreenId();
        if (!expectedScreenId.equals(currentScreenId)) {
            sendFailure(messageId, "Screen changed: expected id=" + expectedScreenId + " but current is id=" + currentScreenId);
            return;
        }

        Screen screen = client.screen;
        if (screen == null) {
            sendFailure(messageId, "No screen open");
            return;
        }

        String text = command.getText();
        client.execute(() -> {
            try {
                text.codePoints().forEach(cp ->
                    VersionCompat.screenCharTyped(screen, cp)
                );

                sendSuccess(messageId, "Typed " + text.length() + " character(s)");
            } catch (Exception e) {
                LOGGER.error("Failed to type text: {}", e.getMessage(), e);
                sendFailure(messageId, "Failed to type text: " + e.getMessage());
            }
        });
    }

    public void handlePressKey(String messageId, Commands.PressKeyCommand command) {
        String expectedScreenId = command.getScreenId();
        String currentScreenId = screenOutbound.getCurrentScreenId();
        if (!expectedScreenId.equals(currentScreenId)) {
            sendFailure(messageId, "Screen changed: expected id=" + expectedScreenId + " but current is id=" + currentScreenId);
            return;
        }

        Screen screen = client.screen;
        if (screen == null) {
            sendFailure(messageId, "No screen open");
            return;
        }

        int key = command.getKeyCode();
        int modifiers = command.getModifiers();

        client.execute(() -> {
            try {
                VersionCompat.screenKeyPressed(screen, key, modifiers);
                VersionCompat.screenKeyReleased(screen, key, modifiers);

                sendSuccess(messageId, "Pressed key " + key);
            } catch (Exception e) {
                LOGGER.error("Failed to press key: {}", e.getMessage(), e);
                sendFailure(messageId, "Failed to press key: " + e.getMessage());
            }
        });
    }

    public void handleOpenGameMenu(String messageId) {
        client.execute(() -> {
            if (client.screen != null) {
                sendFailure(messageId, "A screen is already open: " + client.screen.getClass().getSimpleName());
                return;
            }
            if (client.level == null) {
                sendFailure(messageId, "Not in a world");
                return;
            }
            client.pauseGame(false);
            sendSuccess(messageId, "Game menu opened");
        });
    }

    public void handleClickScreenWidget(String messageId, Commands.ClickScreenWidgetCommand command) {
        String expectedScreenId = command.getScreenId();
        String currentScreenId = screenOutbound.getCurrentScreenId();

        if (!expectedScreenId.equals(currentScreenId)) {
            sendFailure(messageId, "Screen changed: expected id=" + expectedScreenId + " but current is id=" + currentScreenId);
            return;
        }

        Screen screen = client.screen;
        if (screen == null) {
            sendFailure(messageId, "No screen open");
            return;
        }

        int widgetIndex = command.getWidgetIndex();
        int button = command.getButton();

        client.execute(() -> {
            try {
                ScreenOutbound.ClickTarget target = screenOutbound.getWidgetClickTarget(widgetIndex);
                if (target == null) {
                    sendFailure(messageId, "Widget index " + widgetIndex + " not found");
                    return;
                }

                if (!target.active()) {
                    sendFailure(messageId, "Widget at index " + widgetIndex + " is not active");
                    return;
                }

                double clickX = target.centerX();
                double clickY = target.centerY();

                VersionCompat.screenMouseClicked(screen, clickX, clickY, button);

                sendSuccess(messageId, "Clicked widget " + widgetIndex + " at (" + (int)clickX + "," + (int)clickY + ")");
            } catch (Exception e) {
                LOGGER.error("Failed to click screen widget: {}", e.getMessage(), e);
                sendFailure(messageId, "Failed to click widget: " + e.getMessage());
            }
        });
    }
}
