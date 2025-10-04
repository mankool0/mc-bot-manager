package me.mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Connection;
import mankool.mcbot.protocol.Protocol;
import me.mankool.mcBotClient.connection.PipeConnection;
import me.mankool.mcBotClient.mixin.client.ClientPlayNetworkHandlerAccessor;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.SharedConstants;
import net.minecraft.client.MinecraftClient;

import java.util.UUID;

public class ServerOutbound extends BaseOutbound {
    private int tickCounter = 0;

    public ServerOutbound(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(MinecraftClient client) {
        if (client.player == null || client.world == null) {
            return;
        }

        tickCounter++;

        // Send server status every 100 ticks (5 seconds)
        if (tickCounter % 100 == 0) {
            sendStatusUpdate();
        }
    }

    public void sendConnectionInfo() {
        String modVersion = FabricLoader.getInstance()
            .getModContainer("mc-bot-client")
            .map(mod -> mod.getMetadata().getVersion().getFriendlyString())
            .orElse("unknown");

        // Get player info from session (available even before joining a server)
        var session = client.getSession();
        String playerName = session.getUsername();
        String playerUuid = session.getUuidOrNull() != null ?
            session.getUuidOrNull().toString() : "unknown";

        Runtime runtime = Runtime.getRuntime();
        long maxMemory = runtime.maxMemory();

        Connection.ConnectionInfo infoBuilder = Connection.ConnectionInfo.newBuilder()
            .setClientVersion(client.getGameVersion())
            .setModVersion(modVersion)
            .setPlayerName(playerName)
            .setPlayerUuid(playerUuid)
            .setStartupTime(System.currentTimeMillis())
            .setProcessId((int) ProcessHandle.current().pid())
            .setMaxMemory(maxMemory)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setConnectionInfo(infoBuilder)
            .build();

        connection.sendMessage(message);
        System.out.println("Connection info sent to manager");
    }

    public void sendStatusUpdate() {
        var networkHandler = client.getNetworkHandler();
        boolean connected = networkHandler != null;

        String serverAddress = "Unknown";
        String serverName = "Unknown";
        Connection.ServerConnectionStatus.Status serverStatus = Connection.ServerConnectionStatus.Status.INITIAL;
        int playerCount = 0;
        int maxPlayers = 0;
        String motd = "";
        String version = "";
        int ping = 0;
        boolean enforcesSecureChat = false;

        if (connected) {
            // Determine server address and name
            if (client.getCurrentServerEntry() != null) {
                serverAddress = client.getCurrentServerEntry().address;
                serverName = client.getCurrentServerEntry().name;
            } else if (client.isInSingleplayer()) {
                serverAddress = "Singleplayer";
                serverName = "Singleplayer";
            } else {
                serverAddress = "Multiplayer";
                serverName = "Multiplayer";
            }

            // Get player count from the current world
            if (client.world != null) {
                playerCount = client.world.getPlayers().size();
            }

            // Get server metadata
            var serverInfo = networkHandler.getServerInfo();
            if (serverInfo != null) {
                serverStatus = Connection.ServerConnectionStatus.Status.forNumber(serverInfo.getStatus().ordinal());

                if (serverInfo.label != null) {
                    motd = serverInfo.label.getString();
                }

                if (serverInfo.version != null) {
                    version = serverInfo.version.getString();
                }
            }

            // Get player ping
            if (client.player != null) {
                var playerListEntry = networkHandler.getPlayerListEntry(client.player.getUuid());
                if (playerListEntry != null) {
                    ping = playerListEntry.getLatency();
                }
            }

            // Check if server enforces secure chat
            enforcesSecureChat = ((ClientPlayNetworkHandlerAccessor) networkHandler).callIsSecureChatEnforced();
        } else {
            serverAddress = "Disconnected";
            serverName = "Disconnected";
        }

        Connection.ServerConnectionStatus status = Connection.ServerConnectionStatus.newBuilder()
            .setStatus(serverStatus)
            .setServerAddress(serverAddress)
            .setServerName(serverName)
            .setMotd(motd)
            .setPing(ping)
            .setVersionName(version)
            .setProtocolVersion(SharedConstants.getProtocolVersion())
            .setPlayersOnline(playerCount)
            .setPlayersMax(maxPlayers)
            .setServerType(Connection.ServerConnectionStatus.ServerType.OTHER)
            .setEnforcesSecureChat(enforcesSecureChat)
            .setDisconnectReason("null")
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setServerStatus(status)
            .build();

        connection.sendMessage(message);
    }
}