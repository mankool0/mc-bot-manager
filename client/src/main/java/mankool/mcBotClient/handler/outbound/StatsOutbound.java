package mankool.mcBotClient.handler.outbound;

import it.unimi.dsi.fastutil.objects.Object2IntMap;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcbot.protocol.Protocol;
import mankool.mcbot.protocol.Stats;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.protocol.game.ClientboundAwardStatsPacket;
import net.minecraft.network.protocol.game.ServerboundClientCommandPacket;
import net.minecraft.stats.Stat;
import net.minecraft.stats.StatType;
import net.minecraft.stats.StatsCounter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

public class StatsOutbound extends BaseOutbound {
    private static final Logger LOGGER = LoggerFactory.getLogger(StatsOutbound.class);

    private static StatsOutbound instance;

    // FIFO queue of manager request ids that have sent a REQUEST_STATS to the server and are
    // waiting for its reply. The server answers each REQUEST_STATS with exactly one award-stats
    // packet, in order, so when a packet arrives we pop the oldest id and reply to it.
    private final List<String> pendingRequestIds = Collections.synchronizedList(new ArrayList<>());

    // Identity of the StatsCounter last reported. A different instance means a new
    // world/connection, so the next report must be a full snapshot rather than a delta.
    private StatsCounter lastCounter;

    public StatsOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
        instance = this;
    }

    public static StatsOutbound getInstance() {
        return instance;
    }

    @Override
    protected void onClientTick(Minecraft client) {
    }

    /** Manager -> Client: ask the server for the player's stats. */
    public void handleRequestStatistics(String messageId) {
        var networkHandler = client.getConnection();
        if (client.player == null || networkHandler == null) {
            sendResponse(messageId, List.of(), false);
            return;
        }
        pendingRequestIds.add(messageId);
        networkHandler.send(new ServerboundClientCommandPacket(ServerboundClientCommandPacket.Action.REQUEST_STATS));
    }

    /** Called from ClientPacketListenerMixin once a ClientboundAwardStatsPacket has been applied. */
    public void onStatsReceived(ClientboundAwardStatsPacket packet) {
        String id;
        synchronized (pendingRequestIds) {
            id = pendingRequestIds.isEmpty() ? "" : pendingRequestIds.remove(0);
        }
        LocalPlayer player = client.player;
        if (player == null) {
            sendResponse(id, List.of(), false);
            return;
        }

        StatsCounter counter = player.getStats();
        boolean full = counter != lastCounter;
        lastCounter = counter;

        List<Stats.StatEntry> entries = full ? collectFull(counter) : collectDelta(packet);
        LOGGER.debug("Sending {} stat {} for request '{}'", entries.size(), full ? "entries (full)" : "deltas", id);
        sendResponse(id, entries, full);
    }

    // Full snapshot: iterate the whole accumulated counter.
    private List<Stats.StatEntry> collectFull(StatsCounter counter) {
        List<Stats.StatEntry> entries = new ArrayList<>();
        for (StatType<?> type : BuiltInRegistries.STAT_TYPE) {
            var categoryId = BuiltInRegistries.STAT_TYPE.getKey(type);
            if (categoryId == null) continue;
            collectType(counter, type, categoryId.toString(), entries);
        }
        return entries;
    }

    // Wildcard capture into T so registry.getKey(stat.getValue()) resolves with matched generics.
    private <T> void collectType(StatsCounter counter, StatType<T> type, String category, List<Stats.StatEntry> out) {
        Registry<T> registry = type.getRegistry();
        // Iterating the StatType yields only its interned Stats: for block/item/entity types
        // these are exactly the ones the server sent us; for custom stats it is all of them.
        for (Stat<T> stat : type) {
            int amount = counter.getValue(stat);
            if (amount == 0) continue;
            var keyId = registry.getKey(stat.getValue());
            if (keyId == null) continue;
            out.add(entry(category, keyId.toString(), amount));
        }
    }

    // Delta: only the stats carried by this award packet (changed since the last request).
    private List<Stats.StatEntry> collectDelta(ClientboundAwardStatsPacket packet) {
        List<Stats.StatEntry> entries = new ArrayList<>();
        for (Object2IntMap.Entry<Stat<?>> e : packet.stats().object2IntEntrySet()) {
            addStat(entries, e.getKey(), e.getIntValue());
        }
        return entries;
    }

    private <T> void addStat(List<Stats.StatEntry> out, Stat<T> stat, int value) {
        var categoryId = BuiltInRegistries.STAT_TYPE.getKey(stat.getType());
        var keyId = stat.getType().getRegistry().getKey(stat.getValue());
        if (categoryId == null || keyId == null) return;
        out.add(entry(categoryId.toString(), keyId.toString(), value));
    }

    private Stats.StatEntry entry(String category, String key, int value) {
        return Stats.StatEntry.newBuilder()
            .setCategory(category)
            .setKey(key)
            .setValue(value)
            .build();
    }

    private void sendResponse(String messageId, List<Stats.StatEntry> entries, boolean full) {
        Stats.PlayerStatisticsResponse response = Stats.PlayerStatisticsResponse.newBuilder()
            .setCommandId(messageId)
            .addAllEntries(entries)
            .setFull(full)
            .build();
        Protocol.ClientToManagerMessage msg = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setPlayerStatisticsResponse(response)
            .build();
        connection.sendMessage(msg);
    }
}
