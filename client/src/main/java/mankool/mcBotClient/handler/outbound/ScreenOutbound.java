package mankool.mcBotClient.handler.outbound;

import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcbot.protocol.Protocol;
import mankool.mcbot.protocol.Screen;
import net.minecraft.client.Minecraft;

import java.util.UUID;

public class ScreenOutbound extends BaseOutbound {
    private String previousScreenClass = "";

    public ScreenOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(Minecraft client) {
        // Get current screen class name
        String currentScreenClass = client.screen != null ? client.screen.getClass().getName() : "";

        // Only send update if screen changed
        if (!currentScreenClass.equals(previousScreenClass)) {
            sendScreenUpdate(currentScreenClass);
            previousScreenClass = currentScreenClass;
        }
    }

    private void sendScreenUpdate(String screenClass) {
        Screen.ScreenUpdate screenUpdate = Screen.ScreenUpdate.newBuilder()
                .setScreenClass(screenClass)
                .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setScreen(screenUpdate)
                .build();

        connection.sendMessage(message);
    }
}
