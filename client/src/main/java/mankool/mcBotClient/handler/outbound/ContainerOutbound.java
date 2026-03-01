package mankool.mcBotClient.handler.outbound;

import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.mixin.client.AbstractContainerMenuAccessor;
import mankool.mcBotClient.util.ProtoUtil;
import mankool.mcbot.protocol.Common;
import mankool.mcbot.protocol.Inventory;
import mankool.mcbot.protocol.Protocol;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.world.inventory.AbstractContainerMenu;
import net.minecraft.world.item.ItemStack;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

public class ContainerOutbound extends BaseOutbound {

    private static ContainerOutbound instance;

    private ContainerState currentContainer = null;
    private boolean pendingUpdate = false;

    private BlockPos lastInteractedBlock = null;
    private long lastInteractionTime = 0;

    public ContainerOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
        instance = this;
    }

    public static ContainerOutbound getInstance() {
        return instance;
    }

    @Override
    protected void onClientTick(Minecraft client) {
        // Send batched container update once per tick
        if (currentContainer != null && client.player != null) {
            AbstractContainerMenu menu = client.player.containerMenu;
            if (menu.containerId == currentContainer.containerId) {
                // Check for changes or pending update flag
                if (pendingUpdate || hasContainerChanged(menu)) {
                    pendingUpdate = false;
                    sendContainerUpdate();
                }
            }
        }
    }

    public void onBlockInteraction(BlockPos pos) {
        lastInteractedBlock = pos;
        lastInteractionTime = System.currentTimeMillis();
    }

    public void onContainerOpened(int containerId, String containerType, BlockPos position) {
        if (position == null && lastInteractedBlock != null) {
            long timeSinceInteraction = System.currentTimeMillis() - lastInteractionTime;

            int timeoutMs = getPlayerPing() * 2 + 50; // 2x ping + 50ms buffer

            if (timeSinceInteraction < timeoutMs) {
                position = lastInteractedBlock;
                System.out.println("Using cached block position (interaction was " + timeSinceInteraction + "ms ago, timeout: " + timeoutMs + "ms)");
            } else {
                System.out.println("Cached block position too old (" + timeSinceInteraction + "ms ago, timeout: " + timeoutMs + "ms)");
            }
        }

        System.out.println("Container opened: " + containerType + " at " + position + " (id=" + containerId + ")");

        currentContainer = new ContainerState(containerId, containerType, position);
    }

    public void onContainerClosed(int containerId) {
        if (currentContainer != null && currentContainer.containerId == containerId) {
            System.out.println("Container closed: id=" + containerId);

            // Send container closed message (is_open = false, no items)
            Inventory.ContainerUpdate.Builder containerBuilder = Inventory.ContainerUpdate.newBuilder()
                .setContainerId(containerId)
                .setIsOpen(false);

            Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setContainer(containerBuilder.build())
                .build();

            connection.sendMessage(message);

            currentContainer = null;
        }
    }


    public void onContainerContentSet(int containerId) {
        if (currentContainer != null && currentContainer.containerId == containerId) {
            pendingUpdate = true;
        } else if (currentContainer == null && client.player != null && client.player.containerMenu.containerId == containerId) {
            System.out.println("Container content received before open screen packet (id=" + containerId + ")");
        }
    }


    public void onContainerSlotChanged(int containerId, int slot) {
        if (currentContainer != null && currentContainer.containerId == containerId) {
            pendingUpdate = true;
        }
    }

    private void sendContainerUpdate() {
        if (currentContainer == null || client.player == null) return;

        AbstractContainerMenu menu = client.player.containerMenu;
        if (menu.containerId != currentContainer.containerId) return;

        Inventory.ContainerUpdate.Builder containerBuilder = Inventory.ContainerUpdate.newBuilder()
            .setType(mapContainerType(currentContainer.containerType))
            .setContainerId(currentContainer.containerId)
            .setIsOpen(true);

        if (currentContainer.position != null) {
            containerBuilder.setPosition(Common.BlockPos.newBuilder()
                .setX(currentContainer.position.getX())
                .setY(currentContainer.position.getY())
                .setZ(currentContainer.position.getZ())
                .build());
        }

        for (int i = 0; i < menu.slots.size(); i++) {
            ItemStack itemStack = menu.getSlot(i).getItem();
            if (!itemStack.isEmpty()) {
                containerBuilder.addItems(ProtoUtil.buildItemStack(itemStack, i));
            }
        }

        addContainerProperties(menu, containerBuilder);

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setContainer(containerBuilder.build())
            .build();

        connection.sendMessage(message);

        currentContainer.updateCache(menu);
    }

    private boolean hasContainerChanged(AbstractContainerMenu menu) {
        if (currentContainer.cachedItems.size() != menu.slots.size()) {
            return true;
        }

        for (int i = 0; i < menu.slots.size(); i++) {
            ItemStack current = menu.getSlot(i).getItem();
            ItemStack cached = currentContainer.cachedItems.get(i);

            if (!ItemStack.matches(current, cached)) {
                return true;
            }
        }

        return false;
    }

    /**
     * Maps Minecraft container type string to proto enum.
     */
    private Inventory.ContainerUpdate.ContainerType mapContainerType(String typeString) {
        if (typeString == null) {
            System.err.println("ERROR: Container type is null! Defaulting to OTHER");
            return Inventory.ContainerUpdate.ContainerType.OTHER;
        }

        return switch (typeString.toLowerCase()) {
            case "minecraft:generic_9x1", "minecraft:generic_9x2", "minecraft:generic_9x3",
                 "minecraft:generic_9x4", "minecraft:generic_9x5", "minecraft:generic_9x6" ->
                Inventory.ContainerUpdate.ContainerType.CHEST;
            case "minecraft:ender_chest" -> Inventory.ContainerUpdate.ContainerType.ENDER_CHEST;
            case "minecraft:shulker_box" -> Inventory.ContainerUpdate.ContainerType.SHULKER_BOX;
            case "minecraft:furnace" -> Inventory.ContainerUpdate.ContainerType.FURNACE;
            case "minecraft:blast_furnace" -> Inventory.ContainerUpdate.ContainerType.BLAST_FURNACE;
            case "minecraft:smoker" -> Inventory.ContainerUpdate.ContainerType.SMOKER;
            case "minecraft:crafting" -> Inventory.ContainerUpdate.ContainerType.CRAFTING_TABLE;
            case "minecraft:enchantment" -> Inventory.ContainerUpdate.ContainerType.ENCHANTING_TABLE;
            case "minecraft:anvil" -> Inventory.ContainerUpdate.ContainerType.ANVIL;
            case "minecraft:brewing_stand" -> Inventory.ContainerUpdate.ContainerType.BREWING_STAND;
            case "minecraft:hopper" -> Inventory.ContainerUpdate.ContainerType.HOPPER;
            case "minecraft:dispenser" -> Inventory.ContainerUpdate.ContainerType.DISPENSER;
            case "minecraft:dropper" -> Inventory.ContainerUpdate.ContainerType.DROPPER;
            case "minecraft:beacon" -> Inventory.ContainerUpdate.ContainerType.BEACON;
            default -> {
                System.err.println("Unknown container type: " + typeString + " - defaulting to OTHER");
                yield Inventory.ContainerUpdate.ContainerType.OTHER;
            }
        };
    }

    private void addContainerProperties(AbstractContainerMenu menu, Inventory.ContainerUpdate.Builder builder) {
        var dataSlots = ((AbstractContainerMenuAccessor) menu).getDataSlots();

        // Map each data slot to its NBT field name based on container type
        Inventory.ContainerUpdate.ContainerType containerType = mapContainerType(currentContainer.containerType);

        for (int i = 0; i < dataSlots.size(); i++) {
            int value = dataSlots.get(i).get();
            String fieldName = getDataSlotNBTFieldName(containerType, i);
            builder.putProperties(fieldName, value);
        }
    }

    private int getPlayerPing() {
        try {
            if (client.player != null && client.getConnection() != null) {
                var playerInfo = client.getConnection().getPlayerInfo(client.player.getUUID());
                if (playerInfo != null) {
                    int ping = playerInfo.getLatency();
                    if (ping > 0) {
                        return ping;
                    }
                }
            }
        } catch (Exception e) {
            System.err.println("Failed to get player ping: " + e.getMessage());
        }

        // Fallback to conservative 150ms with warning
        System.err.println("WARNING: Could not retrieve player ping, using fallback of 150ms");
        return 150;
    }

    private String getDataSlotNBTFieldName(Inventory.ContainerUpdate.ContainerType type, int slotIndex) {
        return switch (type) {
            case FURNACE, BLAST_FURNACE, SMOKER -> switch (slotIndex) {
                case 0 -> "lit_time_remaining";    // litTimeRemaining field
                case 1 -> "lit_total_time";        // litTotalTime field
                case 2 -> "cooking_time_spent";    // cookingTimer field
                case 3 -> "cooking_total_time";    // cookingTotalTime field
                default -> "data_" + slotIndex;
            };

            case BREWING_STAND -> switch (slotIndex) {
                case 0 -> "BrewTime";              // brewTime field
                case 1 -> "Fuel";                  // fuel field
                default -> "data_" + slotIndex;
            };

            // Note: primary_effect and secondary_effect are stored as strings in NBT,
            // but synchronized as integer effect IDs in data slots
            case BEACON -> switch (slotIndex) {
                case 0 -> "Levels";                // levels field
                case 1 -> "primary_effect";        // primaryPower field (effect ID as int)
                case 2 -> "secondary_effect";      // secondaryPower field (effect ID as int)
                default -> "data_" + slotIndex;
            };

            case ENCHANTING_TABLE -> switch (slotIndex) {
                case 0 -> "costs_0";               // costs[0]
                case 1 -> "costs_1";               // costs[1]
                case 2 -> "costs_2";               // costs[2]
                case 3 -> "enchantmentSeed";       // enchantmentSeed field
                case 4 -> "enchantClue_0";         // enchantClue[0]
                case 5 -> "enchantClue_1";         // enchantClue[1]
                case 6 -> "enchantClue_2";         // enchantClue[2]
                case 7 -> "levelClue_0";           // levelClue[0]
                case 8 -> "levelClue_1";           // levelClue[1]
                case 9 -> "levelClue_2";           // levelClue[2]
                default -> "data_" + slotIndex;
            };

            case ANVIL -> switch (slotIndex) {
                case 0 -> "cost";
                default -> "data_" + slotIndex;
            };

            // Containers with no data slots (only items)
            case CHEST, ENDER_CHEST, SHULKER_BOX, DISPENSER, DROPPER,
                 CRAFTING_TABLE, VILLAGER_TRADE, HORSE_INVENTORY, HOPPER, OTHER,
                 PLAYER_INVENTORY, UNRECOGNIZED ->
                "data_" + slotIndex;  // Should never be called for these types
        };
    }

    private static class ContainerState {
        final int containerId;
        final String containerType;
        final BlockPos position;
        final Map<Integer, ItemStack> cachedItems = new HashMap<>();

        ContainerState(int containerId, String containerType, BlockPos position) {
            this.containerId = containerId;
            this.containerType = containerType;
            this.position = position;
        }

        void updateCache(AbstractContainerMenu menu) {
            cachedItems.clear();
            for (int i = 0; i < menu.slots.size(); i++) {
                cachedItems.put(i, menu.getSlot(i).getItem().copy());
            }
        }
    }
}
