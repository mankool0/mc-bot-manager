package mankool.mcBotClient.handler.outbound;

import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcbot.protocol.Common;
import mankool.mcbot.protocol.Protocol;
import mankool.mcbot.protocol.Registry;
import mankool.mcbot.protocol.World;
import net.minecraft.SharedConstants;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.commands.arguments.blocks.BlockStateParser;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.world.item.Item;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.chunk.LevelChunkSection;
import net.minecraft.world.level.chunk.PalettedContainerRO;
import net.minecraft.core.Holder;
import net.minecraft.world.level.lighting.LevelLightEngine;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.LightLayer;
import net.minecraft.world.level.chunk.DataLayer;
import net.minecraft.core.SectionPos;
import net.minecraft.network.protocol.game.ClientboundLightUpdatePacketData;
import com.google.protobuf.ByteString;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.util.*;

/**
 * Handles sending world data (chunks, block updates) to the manager.
 */
public class WorldOutbound extends BaseOutbound {
    private static final Logger LOGGER = LoggerFactory.getLogger(WorldOutbound.class);

    private static WorldOutbound instance;
    private final Set<ChunkPos> sentChunks = Collections.synchronizedSet(new HashSet<>());
    private final Set<SectionKey> pendingLightSections = java.util.concurrent.ConcurrentHashMap.newKeySet();

    public WorldOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
        instance = this;
    }

    /**
     * Sends a query to the manager to check if it has the block registry for this data version.
     * Called on connection initialization.
     */
    public void sendRegistryQuery() {
        int dataVersion = SharedConstants.getCurrentVersion().dataVersion().version();
        int protocolVersion = SharedConstants.getProtocolVersion();

        World.QueryBlockRegistryMessage query = World.QueryBlockRegistryMessage.newBuilder()
            .setDataVersion(dataVersion)
            .setProtocolVersion(protocolVersion)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setQueryRegistry(query)
            .build();

        connection.sendMessage(message);
        LOGGER.info("Block registry query sent (data version: {})", dataVersion);
    }

    /**
     * Handles the registry response from the manager.
     * If the manager needs the registry, builds and sends the full mapping.
     */
    public void handleRegistryResponse(World.BlockRegistryResponse response) {
        if (response.getStatus() == World.RegistryStatus.NEED_IT) {
            LOGGER.info("Manager needs block registry, building and sending...");
            sendFullRegistry();
        } else {
            LOGGER.info("Manager already has block registry");
        }
    }

    /**
     * Sends a query to the manager to check if it has the item registry for this data version.
     */
    public void sendItemRegistryQuery() {
        int dataVersion = SharedConstants.getCurrentVersion().dataVersion().version();
        int protocolVersion = SharedConstants.getProtocolVersion();

        Registry.QueryItemRegistryMessage query = Registry.QueryItemRegistryMessage.newBuilder()
            .setDataVersion(dataVersion)
            .setProtocolVersion(protocolVersion)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setQueryItemRegistry(query)
            .build();

        connection.sendMessage(message);
        LOGGER.info("Item registry query sent (data version: {})", dataVersion);
    }

    /**
     * Handles the item registry response from the manager.
     * If the manager needs the registry, builds and sends the full item mapping.
     */
    public void handleItemRegistryResponse(Registry.ItemRegistryResponse response) {
        if (response.getStatus() == World.RegistryStatus.NEED_IT) {
            LOGGER.info("Manager needs item registry, building and sending...");
            sendFullItemRegistry();
        } else {
            LOGGER.info("Manager already has item registry");
        }
    }

    /**
     * Builds and sends the full item registry to the manager.
     */
    private void sendFullItemRegistry() {
        int dataVersion = SharedConstants.getCurrentVersion().dataVersion().version();

        Registry.ItemRegistryMessage.Builder builder = Registry.ItemRegistryMessage.newBuilder()
            .setDataVersion(dataVersion);

        for (Item item : BuiltInRegistries.ITEM) {
            String itemId = BuiltInRegistries.ITEM.getResourceKey(item)
                .map(k -> k.identifier().toString())
                .orElse(null);
            if (itemId == null) continue;

            net.minecraft.world.item.ItemStack stack = item.getDefaultInstance();
            int maxStackSize = stack.getMaxStackSize();
            int maxDamage = stack.getMaxDamage();
            String displayName = stack.getDisplayName().getString();

            builder.addEntries(Registry.ItemEntry.newBuilder()
                .setItemId(itemId)
                .setMaxStackSize(maxStackSize)
                .setMaxDamage(maxDamage)
                .setDisplayName(displayName)
                .build());
        }

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setItemRegistry(builder.build())
            .build();

        connection.sendMessage(message);
        LOGGER.info("Item registry sent: {} items", builder.getEntriesCount());
    }

    /**
     * Builds and sends the full block state registry to the manager.
     */
    private void sendFullRegistry() {
        int dataVersion = SharedConstants.getCurrentVersion().dataVersion().version();
        Map<Integer, String> stateMap = new HashMap<>();
        Map<Integer, Integer> faceMasks = new HashMap<>();

        for (Block block : BuiltInRegistries.BLOCK) {
            for (BlockState state : block.getStateDefinition().getPossibleStates()) {
                int id = Block.getId(state);
                String stateString = BlockStateParser.serialize(state);
                stateMap.put(id, stateString);

                try {
                    net.minecraft.world.phys.shapes.VoxelShape shape =
                        state.getCollisionShape(net.minecraft.world.level.EmptyBlockGetter.INSTANCE, BlockPos.ZERO);
                    int mask = 0;
                    for (net.minecraft.core.Direction dir : net.minecraft.core.Direction.values()) {
                        if (Block.isFaceFull(shape, dir)) {
                            mask |= (1 << dir.ordinal());
                        }
                    }
                    if (mask != 0) {
                        faceMasks.put(id, mask);
                    }
                } catch (Exception e) {
                    // Some blocks may throw when queried without a real level context
                }
            }
        }

        World.BlockRegistryMessage registry = World.BlockRegistryMessage.newBuilder()
            .setDataVersion(dataVersion)
            .putAllStateMap(stateMap)
            .putAllFaceMasks(faceMasks)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setBlockRegistry(registry)
            .build();

        connection.sendMessage(message);
        LOGGER.info("Block registry sent: {} states", stateMap.size());
    }

    public static WorldOutbound getInstance() {
        return instance;
    }

    @Override
    protected void onClientTick(Minecraft client) {
        if (pendingLightSections.isEmpty()) return;
        Set<SectionKey> toScan = new HashSet<>(pendingLightSections);
        pendingLightSections.clear();
        for (SectionKey sk : toScan) {
            sendLightForSection(sk.chunkX, sk.chunkZ, sk.sectionY);
        }
    }

    public void onChunkLoaded(int chunkX, int chunkZ, ClientboundLightUpdatePacketData lightData) {
        ClientLevel level = client.level;
        if (level == null) return;

        ChunkPos pos = new ChunkPos(chunkX, chunkZ);

        LevelChunk chunk = level.getChunk(chunkX, chunkZ);

        sendChunkData(chunk, lightData);
        sentChunks.add(pos);
    }

    public void onChunkUnloaded(int chunkX, int chunkZ) {
        ChunkPos pos = new ChunkPos(chunkX, chunkZ);
        sentChunks.remove(pos);

        if (client.level == null) return;

        World.ChunkUnloadMessage unload = World.ChunkUnloadMessage.newBuilder()
            .setChunkX(chunkX)
            .setChunkZ(chunkZ)
            .setDimension(client.level.dimension().identifier().toString())
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setChunkUnload(unload)
            .build();

        connection.sendMessage(message);
    }

    public void onBlockUpdate(BlockPos pos, BlockState state) {
        if (client.level == null) return;

        // Queue section for light rescan - light engine updates on next tick
        pendingLightSections.add(new SectionKey(pos.getX() >> 4, pos.getZ() >> 4, pos.getY() >> 4));

        int stateId = Block.getId(state);

        World.BlockUpdateMessage update = World.BlockUpdateMessage.newBuilder()
            .setPosition(toProtoBlockPos(pos))
            .setStateId(stateId)
            .setDimension(client.level.dimension().identifier().toString())
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setBlockUpdate(update)
            .build();

        connection.sendMessage(message);
    }

    public void onMultiBlockUpdate(List<BlockPos> positions, List<BlockState> states) {
        if (client.level == null || positions.isEmpty()) return;

        // Queue all affected sections for light rescan
        for (BlockPos pos : positions) {
            pendingLightSections.add(new SectionKey(pos.getX() >> 4, pos.getZ() >> 4, pos.getY() >> 4));
        }

        World.MultiBlockUpdateMessage.Builder builder = World.MultiBlockUpdateMessage.newBuilder()
            .setDimension(client.level.dimension().identifier().toString());

        for (int i = 0; i < positions.size(); i++) {
            builder.addPositions(toProtoBlockPos(positions.get(i)));
            builder.addStateIds(Block.getId(states.get(i)));
        }

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setMultiBlockUpdate(builder.build())
            .build();

        connection.sendMessage(message);
    }

    private void sendChunkData(LevelChunk chunk, ClientboundLightUpdatePacketData lightData) {
        if (client.level == null) return;

        // Parse light data from the packet directly (light engine may not have applied it yet)
        int minLightSection = client.level.getChunkSource().getLightEngine().getMinLightSection();
        java.util.Map<Integer, byte[]> blockLightMap = new java.util.HashMap<>();
        java.util.Map<Integer, byte[]> skyLightMap = new java.util.HashMap<>();

        java.util.BitSet blockYMask = lightData.getBlockYMask();
        java.util.List<byte[]> blockUpdates = lightData.getBlockUpdates();
        int blockIdx = 0;
        for (int i = blockYMask.nextSetBit(0); i >= 0; i = blockYMask.nextSetBit(i + 1)) {
            blockLightMap.put(minLightSection + i, blockUpdates.get(blockIdx++));
        }

        java.util.BitSet skyYMask = lightData.getSkyYMask();
        java.util.List<byte[]> skyUpdates = lightData.getSkyUpdates();
        int skyIdx = 0;
        for (int i = skyYMask.nextSetBit(0); i >= 0; i = skyYMask.nextSetBit(i + 1)) {
            skyLightMap.put(minLightSection + i, skyUpdates.get(skyIdx++));
        }

        World.ChunkDataMessage.Builder chunkBuilder = World.ChunkDataMessage.newBuilder()
            .setChunkX(chunk.getPos().x)
            .setChunkZ(chunk.getPos().z)
            .setDimension(client.level.dimension().identifier().toString())
            .setMinY(chunk.getMinY())
            .setMaxY(chunk.getMaxY());

        // Encode each section
        LevelChunkSection[] sections = chunk.getSections();
        for (int i = 0; i < sections.length; i++) {
            LevelChunkSection section = sections[i];
            if (section == null) {
                continue;
            }

            int sectionY = chunk.getMinSectionY() + i;
            World.ChunkSection sectionProto = encodeSection(section, sectionY, blockLightMap, skyLightMap);
            chunkBuilder.addSections(sectionProto);
        }

        // Encode block entities
        for (var entry : chunk.getBlockEntities().entrySet()) {
            BlockEntity be = entry.getValue();
            try {
                CompoundTag nbt = be.saveWithFullMetadata(client.level.registryAccess());
                ByteArrayOutputStream baos = new ByteArrayOutputStream();
                nbt.write(new DataOutputStream(baos));
                chunkBuilder.addBlockEntityNbt(ByteString.copyFrom(baos.toByteArray()));
            } catch (Exception e) {
                // skip on error
            }
        }

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setChunkData(chunkBuilder.build())
            .build();

        connection.sendMessage(message);
    }

    /**
     * Encodes a single chunk section (16x16x16 blocks) into protobuf format.
     * Uses palette-based compression with numeric state IDs, and includes light data.
     */
    private static World.ChunkSection encodeSection(LevelChunkSection section, int sectionY,
                                                    java.util.Map<Integer, byte[]> blockLightMap,
                                                    java.util.Map<Integer, byte[]> skyLightMap) {
        // Build palette and indices using numeric state IDs
        Map<Integer, Integer> stateIdToPaletteIndex = new LinkedHashMap<>();
        List<Integer> paletteStateIds = new ArrayList<>();
        List<Integer> indices = new ArrayList<>(4096);

        boolean uniform = true;
        Integer firstStateId = null;

        // Iterate blocks in YZX order (y*256 + z*16 + x)
        for (int y = 0; y < 16; y++) {
            for (int z = 0; z < 16; z++) {
                for (int x = 0; x < 16; x++) {
                    BlockState state = section.getBlockState(x, y, z);
                    int stateId = Block.getId(state);

                    if (firstStateId == null) {
                        firstStateId = stateId;
                    } else if (stateId != firstStateId) {
                        uniform = false;
                    }

                    // Add to palette if not present
                    int paletteIndex = stateIdToPaletteIndex.computeIfAbsent(stateId, id -> {
                        paletteStateIds.add(id);
                        return paletteStateIds.size() - 1;
                    });

                    indices.add(paletteIndex);
                }
            }
        }

        World.ChunkSection.Builder builder = World.ChunkSection.newBuilder()
            .setSectionY(sectionY)
            .addAllPalette(paletteStateIds)
            .setUniform(uniform);

        // Only include indices if not uniform
        if (!uniform) {
            builder.addAllBlockIndices(indices);
        }

        // Extract biome data (4x4x4 per section, 64 entries, index = y*16 + z*4 + x)
        PalettedContainerRO<Holder<Biome>> biomeContainer = section.getBiomes();
        List<String> biomePaletteList = new ArrayList<>();
        Map<String, Integer> biomeToIndex = new LinkedHashMap<>();
        List<Integer> biomeIndices = new ArrayList<>(64);
        boolean biomeUniform = true;
        String firstBiome = null;

        for (int by = 0; by < 4; by++) {
            for (int bz = 0; bz < 4; bz++) {
                for (int bx = 0; bx < 4; bx++) {
                    Holder<Biome> holder = biomeContainer.get(bx, by, bz);
                    String biomeId = holder.unwrapKey()
                        .map(k -> k.identifier().toString())
                        .orElse("minecraft:the_void");
                    if (firstBiome == null) {
                        firstBiome = biomeId;
                    } else if (!biomeId.equals(firstBiome)) {
                        biomeUniform = false;
                    }
                    int idx = biomeToIndex.computeIfAbsent(biomeId, id -> {
                        biomePaletteList.add(id);
                        return biomePaletteList.size() - 1;
                    });
                    biomeIndices.add(idx);
                }
            }
        }

        builder.addAllBiomePalette(biomePaletteList).setBiomeUniform(biomeUniform);
        if (!biomeUniform) {
            builder.addAllBiomeIndices(biomeIndices);
        }

        // Attach light data from packet
        byte[] blockLightData = blockLightMap.get(sectionY);
        if (blockLightData != null) {
            builder.setBlockLight(ByteString.copyFrom(blockLightData));
        }
        byte[] skyLightData = skyLightMap.get(sectionY);
        if (skyLightData != null) {
            builder.setSkyLight(ByteString.copyFrom(skyLightData));
        }

        return builder.build();
    }

    private static Common.BlockPos toProtoBlockPos(BlockPos pos) {
        return Common.BlockPos.newBuilder()
            .setX(pos.getX())
            .setY(pos.getY())
            .setZ(pos.getZ())
            .build();
    }

    /**
     * Sends incremental light update for a chunk to the manager.
     * Called after handleLightUpdatePacket has already applied the update to the light engine.
     */
    public void onLightUpdate(int chunkX, int chunkZ, ClientboundLightUpdatePacketData lightData) {
        if (client.level == null) return;

        LevelLightEngine lightEngine = client.level.getChunkSource().getLightEngine();
        int minLightSection = lightEngine.getMinLightSection();

        World.LightUpdateMessage.Builder builder = World.LightUpdateMessage.newBuilder()
            .setChunkX(chunkX)
            .setChunkZ(chunkZ)
            .setDimension(client.level.dimension().identifier().toString());

        // Sky light: non-empty sections
        java.util.BitSet skyYMask = lightData.getSkyYMask();
        java.util.List<byte[]> skyUpdates = lightData.getSkyUpdates();
        int skyIdx = 0;
        for (int i = skyYMask.nextSetBit(0); i >= 0; i = skyYMask.nextSetBit(i + 1)) {
            builder.addSkySections(World.LightSection.newBuilder()
                .setSectionY(minLightSection + i)
                .setData(ByteString.copyFrom(skyUpdates.get(skyIdx++))));
        }

        // Sky light: cleared sections
        java.util.BitSet emptySkyYMask = lightData.getEmptySkyYMask();
        for (int i = emptySkyYMask.nextSetBit(0); i >= 0; i = emptySkyYMask.nextSetBit(i + 1)) {
            builder.addSkySections(World.LightSection.newBuilder()
                .setSectionY(minLightSection + i));  // empty data = clear
        }

        // Block light: non-empty sections
        java.util.BitSet blockYMask = lightData.getBlockYMask();
        java.util.List<byte[]> blockUpdates = lightData.getBlockUpdates();
        int blockIdx = 0;
        for (int i = blockYMask.nextSetBit(0); i >= 0; i = blockYMask.nextSetBit(i + 1)) {
            builder.addBlockSections(World.LightSection.newBuilder()
                .setSectionY(minLightSection + i)
                .setData(ByteString.copyFrom(blockUpdates.get(blockIdx++))));
        }

        // Block light: cleared sections
        java.util.BitSet emptyBlockYMask = lightData.getEmptyBlockYMask();
        for (int i = emptyBlockYMask.nextSetBit(0); i >= 0; i = emptyBlockYMask.nextSetBit(i + 1)) {
            builder.addBlockSections(World.LightSection.newBuilder()
                .setSectionY(minLightSection + i));  // empty data = clear
        }

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(java.util.UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setLightUpdate(builder.build())
            .build();

        connection.sendMessage(message);
    }

    /**
     * Sends current weather state to the manager.
     */
    public void sendWeatherUpdate(boolean isRaining, boolean isThundering, float rainLevel, float thunderLevel) {
        World.WeatherUpdate weather = World.WeatherUpdate.newBuilder()
            .setIsRaining(isRaining)
            .setIsThundering(isThundering)
            .setRainLevel(rainLevel)
            .setThunderLevel(thunderLevel)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setWeatherUpdate(weather)
            .build();

        connection.sendMessage(message);
    }

    private void sendLightForSection(int chunkX, int chunkZ, int sectionY) {
        if (client.level == null) return;
        LevelLightEngine lightEngine = client.level.getChunkSource().getLightEngine();
        SectionPos sectionPos = SectionPos.of(chunkX, sectionY, chunkZ);

        DataLayer blockLayer = lightEngine.getLayerListener(LightLayer.BLOCK).getDataLayerData(sectionPos);
        DataLayer skyLayer = lightEngine.getLayerListener(LightLayer.SKY).getDataLayerData(sectionPos);

        World.LightUpdateMessage.Builder builder = World.LightUpdateMessage.newBuilder()
            .setChunkX(chunkX)
            .setChunkZ(chunkZ)
            .setDimension(client.level.dimension().identifier().toString());

        boolean hasData = false;
        if (blockLayer != null && !blockLayer.isEmpty()) {
            builder.addBlockSections(World.LightSection.newBuilder()
                .setSectionY(sectionY)
                .setData(ByteString.copyFrom(blockLayer.getData())));
            hasData = true;
        }
        if (skyLayer != null && !skyLayer.isEmpty()) {
            builder.addSkySections(World.LightSection.newBuilder()
                .setSectionY(sectionY)
                .setData(ByteString.copyFrom(skyLayer.getData())));
            hasData = true;
        }
        if (!hasData) return;

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setLightUpdate(builder.build())
            .build();

        connection.sendMessage(message);
    }

    /**
     * Simple chunk position holder for tracking sent chunks.
     */
    private record ChunkPos(int x, int z) {}

    private record SectionKey(int chunkX, int chunkZ, int sectionY) {}
}