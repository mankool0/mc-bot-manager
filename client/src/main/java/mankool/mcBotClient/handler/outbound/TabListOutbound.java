package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Connection;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.PlayerInfo;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.UUID;

public class TabListOutbound extends BaseOutbound {
    private final Map<UUID, PlayerSnapshot> knownPlayers = new HashMap<>();

    private record PlayerSnapshot(String name, int ping, int gamemode, String displayName) {}

    public TabListOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(Minecraft client) {
        if (client.getConnection() == null) {
            if (!knownPlayers.isEmpty()) {
                var removeBuilder = Connection.TabListPlayerRemove.newBuilder();
                for (UUID known : knownPlayers.keySet()) {
                    removeBuilder.addUuids(known.toString());
                }
                connection.sendMessage(Protocol.ClientToManagerMessage.newBuilder()
                    .setMessageId(UUID.randomUUID().toString())
                    .setTimestamp(System.currentTimeMillis())
                    .setTabListRemove(removeBuilder.build())
                    .build());
                knownPlayers.clear();
            }
            return;
        }
        computeAndSendDelta(client);
    }

    private void computeAndSendDelta(Minecraft client) {
        var current = client.getConnection().getListedOnlinePlayers();

        var updateBuilder = Connection.TabListPlayerUpdate.newBuilder();
        var seenUuids = new HashSet<UUID>();

        for (PlayerInfo info : current) {
            UUID uuid = info.getProfile().id();
            seenUuids.add(uuid);
            int ping = info.getLatency();
            int gamemode = info.getGameMode().getId();
            String dn = info.getTabListDisplayName() != null
                        ? info.getTabListDisplayName().getString() : "";
            String name = info.getProfile().name();

            PlayerSnapshot prev = knownPlayers.get(uuid);
            if (prev == null || prev.ping() != ping || prev.gamemode() != gamemode
                    || !prev.displayName().equals(dn) || !prev.name().equals(name)) {
                updateBuilder.addPlayers(Connection.TabListPlayer.newBuilder()
                    .setName(name)
                    .setUuid(uuid.toString())
                    .setPing(ping)
                    .setGamemodeValue(gamemode)
                    .setDisplayName(dn)
                    .build());
                knownPlayers.put(uuid, new PlayerSnapshot(name, ping, gamemode, dn));
            }
        }

        var removeBuilder = Connection.TabListPlayerRemove.newBuilder();
        var toRemove = new ArrayList<UUID>();
        for (UUID known : knownPlayers.keySet()) {
            if (!seenUuids.contains(known)) {
                removeBuilder.addUuids(known.toString());
                toRemove.add(known);
            }
        }
        toRemove.forEach(knownPlayers::remove);

        if (updateBuilder.getPlayersCount() > 0) {
            connection.sendMessage(Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setTabListUpdate(updateBuilder.build())
                .build());
        }
        if (removeBuilder.getUuidsCount() > 0) {
            connection.sendMessage(Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setTabListRemove(removeBuilder.build())
                .build());
        }
    }
}
