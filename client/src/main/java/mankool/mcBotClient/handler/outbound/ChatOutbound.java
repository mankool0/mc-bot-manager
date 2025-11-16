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

                long timestamp = packet.body().timeStamp().toEpochMilli();

                String chatTypeStr = packet.chatType().chatType().unwrapKey()
                    .map(key -> key.location().toString())
                    .orElse("minecraft:chat");

                Chat.ChatMessage.MinecraftChatType chatType = switch (chatTypeStr) {
                    case "minecraft:chat" -> Chat.ChatMessage.MinecraftChatType.CHAT;
                    case "minecraft:msg_command_incoming" -> Chat.ChatMessage.MinecraftChatType.MSG_COMMAND_INCOMING;
                    case "minecraft:msg_command_outgoing" -> Chat.ChatMessage.MinecraftChatType.MSG_COMMAND_OUTGOING;
                    case "minecraft:emote_command" -> Chat.ChatMessage.MinecraftChatType.EMOTE_COMMAND;
                    case "minecraft:say_command" -> Chat.ChatMessage.MinecraftChatType.SAY_COMMAND;
                    case "minecraft:team_msg_command_incoming" -> Chat.ChatMessage.MinecraftChatType.TEAM_MSG_COMMAND_INCOMING;
                    case "minecraft:team_msg_command_outgoing" -> Chat.ChatMessage.MinecraftChatType.TEAM_MSG_COMMAND_OUTGOING;
                    default -> Chat.ChatMessage.MinecraftChatType.UNKNOWN;
                };

                onChatMessage(message, senderName, senderUuidStr, false, isSigned, chatType, timestamp);
            } catch (Exception e) {
                LOGGER.error("Failed to process player chat packet", e);
            }
        } else if (event.packet instanceof ClientboundSystemChatPacket packet) {
            try {
                // System messages don't have a timestamp in the packet, use current time
                long timestamp = System.currentTimeMillis();
                onChatMessage(packet.content(), "SYSTEM", null, true, false, null, timestamp);
            } catch (Exception e) {
                LOGGER.error("Failed to process system chat packet", e);
            }
        }
    }

    public void onChatMessage(Component message, String sender, String senderUuid, boolean isSystem, boolean isSigned, Chat.ChatMessage.MinecraftChatType minecraftChatType, long timestamp) {
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
                    .setTimestamp(timestamp)
                    .setIsSigned(isSigned);

            if (senderUuid != null && !senderUuid.isEmpty()) {
                builder.setSenderUuid(senderUuid);
            }

            if (minecraftChatType != null) {
                builder.setMinecraftChatType(minecraftChatType);
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