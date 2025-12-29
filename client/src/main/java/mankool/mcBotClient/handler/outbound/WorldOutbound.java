package mankool.mcBotClient.handler.outbound;

import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcbot.protocol.Common;
import mankool.mcbot.protocol.Protocol;
import mankool.mcbot.protocol.World;
import net.minecraft.SharedConstants;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.commands.arguments.blocks.BlockStateParser;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.chunk.LevelChunkSection;

import java.util.*;

/**
 * Handles sending world data (chunks, block updates) to the manager.
 */
public class WorldOutbound extends BaseOutbound {

    private static WorldOutbound instance;
    private final Set<ChunkPos> sentChunks = Collections.synchronizedSet(new HashSet<>());

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
        System.out.println("Block registry query sent (data version: " + dataVersion + ")");
    }

    /**
     * Handles the registry response from the manager.
     * If the manager needs the registry, builds and sends the full mapping.
     */
    public void handleRegistryResponse(World.BlockRegistryResponse response) {
        if (response.getStatus() == World.RegistryStatus.NEED_IT) {
            System.out.println("Manager needs block registry, building and sending...");
            sendFullRegistry();
        } else {
            System.out.println("Manager already has block registry");
        }
    }

    /**
     * Builds and sends the full block state registry to the manager.
     */
    private void sendFullRegistry() {
        int dataVersion = SharedConstants.getCurrentVersion().dataVersion().version();
        Map<Integer, String> stateMap = new HashMap<>();

        for (Block block : BuiltInRegistries.BLOCK) {
            for (BlockState state : block.getStateDefinition().getPossibleStates()) {
                int id = Block.getId(state);
                String stateString = BlockStateParser.serialize(state);
                stateMap.put(id, stateString);
            }
        }

        World.BlockRegistryMessage registry = World.BlockRegistryMessage.newBuilder()
            .setDataVersion(dataVersion)
            .putAllStateMap(stateMap)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setBlockRegistry(registry)
            .build();

        connection.sendMessage(message);
        System.out.println("Block registry sent: " + stateMap.size() + " states");
    }

    public static WorldOutbound getInstance() {
        return instance;
    }

    @Override
    protected void onClientTick(Minecraft client) {
        // No periodic tasks needed
    }

    public void onChunkLoaded(int chunkX, int chunkZ) {
        ClientLevel level = client.level;
        if (level == null) return;

        ChunkPos pos = new ChunkPos(chunkX, chunkZ);

        LevelChunk chunk = level.getChunk(chunkX, chunkZ);

        sendChunkData(chunk);
        sentChunks.add(pos);
    }

    public void onChunkUnloaded(int chunkX, int chunkZ) {
        ChunkPos pos = new ChunkPos(chunkX, chunkZ);
        sentChunks.remove(pos);

        if (client.level == null) return;

        World.ChunkUnloadMessage unload = World.ChunkUnloadMessage.newBuilder()
            .setChunkX(chunkX)
            .setChunkZ(chunkZ)
            .setDimension(client.level.dimension().location().toString())
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

        int stateId = Block.getId(state);

        World.BlockUpdateMessage update = World.BlockUpdateMessage.newBuilder()
            .setPosition(toProtoBlockPos(pos))
            .setStateId(stateId)
            .setDimension(client.level.dimension().location().toString())
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

        World.MultiBlockUpdateMessage.Builder builder = World.MultiBlockUpdateMessage.newBuilder()
            .setDimension(client.level.dimension().location().toString());

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

    private void sendChunkData(LevelChunk chunk) {
        if (client.level == null) return;

        World.ChunkDataMessage.Builder chunkBuilder = World.ChunkDataMessage.newBuilder()
            .setChunkX(chunk.getPos().x)
            .setChunkZ(chunk.getPos().z)
            .setDimension(client.level.dimension().location().toString())
            .setMinY(chunk.getMinY())
            .setMaxY(chunk.getMaxY());

        // Encode each section
        LevelChunkSection[] sections = chunk.getSections();
        for (int i = 0; i < sections.length; i++) {
            LevelChunkSection section = sections[i];
            if (section == null || section.hasOnlyAir()) {
                continue; // Skip empty sections
            }

            int sectionY = chunk.getMinSectionY() + i;
            World.ChunkSection sectionProto = encodeSection(section, sectionY);
            chunkBuilder.addSections(sectionProto);
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
     * Uses palette-based compression with numeric state IDs.
     */
    private static World.ChunkSection encodeSection(LevelChunkSection section, int sectionY) {
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
     * Simple chunk position holder for tracking sent chunks.
     */
    private record ChunkPos(int x, int z) {}
}