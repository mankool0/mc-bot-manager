package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Connection;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.util.VersionCompat;
import mankool.mcBotClient.mixin.client.ClientPlayNetworkHandlerAccessor;
import net.minecraft.SharedConstants;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.Minecraft;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.util.UUID;

public class ServerOutbound extends BaseOutbound {
    private static final Logger LOGGER = LoggerFactory.getLogger(ServerOutbound.class);
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
        String playerUuid = session.getProfileId().toString();

        Runtime runtime = Runtime.getRuntime();
        long maxMemory = runtime.maxMemory();

        // Get version info
        int dataVersion = VersionCompat.getDataVersion();
        String versionName = VersionCompat.getVersionName();
        String versionSeries = VersionCompat.getVersionSeries();
        boolean isSnapshot = VersionCompat.isVersionSnapshot();

        Connection.ConnectionInfo infoBuilder = Connection.ConnectionInfo.newBuilder()
            .setClientVersion(versionName)
            .setModVersion(modVersion)
            .setPlayerName(playerName)
            .setPlayerUuid(playerUuid)
            .setStartupTime(System.currentTimeMillis())
            .setProcessId((int) ProcessHandle.current().pid())
            .setMaxMemory(maxMemory)
            .setDataVersion(dataVersion)
            .setVersionSeries(versionSeries)
            .setVersionIsSnapshot(isSnapshot)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setConnectionInfo(infoBuilder)
            .build();

        connection.sendMessage(message);
        LOGGER.info("Connection info sent to manager");
        LOGGER.info("  Version: {} (data version: {}, series: {}, snapshot: {})", versionName, dataVersion, versionSeries, isSnapshot);
    }

    public void sendNetworkDropStatus() {
        Connection.ServerConnectionStatus status = Connection.ServerConnectionStatus.newBuilder()
            .setStatus(Connection.ServerConnectionStatus.Status.INITIAL)
            .setServerAddress("Disconnected")
            .setDisconnectReason("NETWORK_DROP")
            .build();
        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(java.util.UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setServerStatus(status)
            .build();
        connection.sendMessage(message);
        LOGGER.info("Sent NETWORK_DROP status to manager");
    }

    public void sendInvalidSessionStatus() {
        Connection.ServerConnectionStatus status = Connection.ServerConnectionStatus.newBuilder()
            .setStatus(Connection.ServerConnectionStatus.Status.INITIAL)
            .setServerAddress("Disconnected")
            .setDisconnectReason("INVALID_SESSION")
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(java.util.UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setServerStatus(status)
            .build();

        connection.sendMessage(message);
        LOGGER.info("Sent INVALID_SESSION status to manager");
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


            serverStatus = Connection.ServerConnectionStatus.Status.SUCCESSFUL;

            // Get server metadata
            var serverInfo = networkHandler.getServerData();
            if (serverInfo != null) {
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