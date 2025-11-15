package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Chat;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import meteordevelopment.meteorclient.MeteorClient;
import meteordevelopment.meteorclient.events.packets.PacketEvent;
import meteordevelopment.orbit.EventHandler;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.PlayerInfo;
import net.minecraft.network.chat.Component;
import net.minecraft.network.protocol.game.ClientboundPlayerChatPacket;
import net.minecraft.network.protocol.game.ClientboundSystemChatPacket;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.UUID;

public class ChatOutbound {
    private static final Logger LOGGER = LoggerFactory.getLogger(ChatOutbound.class);
    private static ChatOutbound instance;

    private final Minecraft client;
    private final PipeConnection connection;

    public ChatOutbound(Minecraft client, PipeConnection connection) {
        this.client = client;
        this.connection = connection;
        instance = this;

        MeteorClient.EVENT_BUS.subscribe(this);
    }

    public static ChatOutbound getInstance() {
        return instance;
    }

    @EventHandler
    private void onPacketReceive(PacketEvent.Receive event) {
        if (event.packet instanceof ClientboundPlayerChatPacket packet) {
            try {
                UUID senderUuid = packet.sender();
                String senderUuidStr = senderUuid.toString();

                String senderName = "";
                PlayerInfo playerInfo = client.getConnection().getPlayerInfo(senderUuid);
                if (playerInfo != null) {
                    senderName = playerInfo.getProfile().getName();
                }

                String content = packet.body().content();
                Component message = Component.literal(content);
                boolean isSigned = packet.signature() != null;

                onChatMessage(message, senderName, senderUuidStr, false, isSigned);
            } catch (Exception e) {
                LOGGER.error("Failed to process player chat packet", e);
            }
        } else if (event.packet instanceof ClientboundSystemChatPacket packet) {
            try {
                onChatMessage(packet.content(), "SYSTEM", null, true, false);
            } catch (Exception e) {
                LOGGER.error("Failed to process system chat packet", e);
            }
        }
    }

    public void onChatMessage(Component message, String sender, String senderUuid, boolean isSystem, boolean isSigned) {
        try {
            String content = message.getString();

            Chat.ChatMessage.Type type;
            if (isSystem) {
                type = Chat.ChatMessage.Type.SYSTEM_MESSAGE;
            } else {
                type = Chat.ChatMessage.Type.PLAYER_CHAT;
            }

            Chat.ChatMessage.Builder builder = Chat.ChatMessage.newBuilder()
                    .setType(type)
                    .setContent(content)
                    .setSender(sender != null ? sender : "SYSTEM")
                    .setTimestamp(System.currentTimeMillis())
                    .setIsSigned(isSigned);

            if (senderUuid != null && !senderUuid.isEmpty()) {
                builder.setSenderUuid(senderUuid);
            }

            Chat.ChatMessage chatMessage = builder.build();

            Protocol.ClientToManagerMessage outboundMessage = Protocol.ClientToManagerMessage.newBuilder()
                    .setMessageId(UUID.randomUUID().toString())
                    .setTimestamp(System.currentTimeMillis())
                    .setChat(chatMessage)
                    .build();

            connection.sendMessage(outboundMessage);

            LOGGER.debug("Sent chat message to manager: {} from {}", content, sender);
        } catch (Exception e) {
            LOGGER.error("Failed to send chat message to manager", e);
        }
    }

}