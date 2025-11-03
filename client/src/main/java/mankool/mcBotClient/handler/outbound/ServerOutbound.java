package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Connection;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.mixin.client.ClientPlayNetworkHandlerAccessor;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.SharedConstants;
import net.minecraft.client.Minecraft;
import java.util.UUID;

public class ServerOutbound extends BaseOutbound {
    private int tickCounter = 0;

    public ServerOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(Minecraft client) {
        if (client.player == null || client.level == null) {
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
        var session = client.getUser();
        String playerName = session.getName();
        String playerUuid = session.getProfileId() != null ?
            session.getProfileId().toString() : "unknown";

        Runtime runtime = Runtime.getRuntime();
        long maxMemory = runtime.maxMemory();

        Connection.ConnectionInfo infoBuilder = Connection.ConnectionInfo.newBuilder()
            .setClientVersion(client.getLaunchedVersion())
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
        var networkHandler = client.getConnection();
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
            if (client.getCurrentServer() != null) {
                serverAddress = client.getCurrentServer().ip;
                serverName = client.getCurrentServer().name;
            } else if (client.isLocalServer()) {
                serverAddress = "Singleplayer";
                serverName = "Singleplayer";
            } else {
                serverAddress = "Multiplayer";
                serverName = "Multiplayer";
            }

            // Get player count from the current world
            if (client.level != null) {
                playerCount = client.level.players().size();
            }

            // Get server metadata
            var serverInfo = networkHandler.getServerData();
            if (serverInfo != null) {
                serverStatus = Connection.ServerConnectionStatus.Status.forNumber(serverInfo.state().ordinal());

                if (serverInfo.motd != null) {
                    motd = serverInfo.motd.getString();
                }

                if (serverInfo.version != null) {
                    version = serverInfo.version.getString();
                }
            }

            // Get player ping
            if (client.player != null) {
                var playerListEntry = networkHandler.getPlayerInfo(client.player.getUUID());
                if (playerListEntry != null) {
                    ping = playerListEntry.getLatency();
                }
            }

            // Check if server enforces secure chat
            enforcesSecureChat = ((ClientPlayNetworkHandlerAccessor) networkHandler).callEnforcesSecureChat();
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