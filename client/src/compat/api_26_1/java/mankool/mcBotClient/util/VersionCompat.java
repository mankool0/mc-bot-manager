package mankool.mcBotClient.util;

import com.mojang.authlib.GameProfile;
import java.util.UUID;
import it.unimi.dsi.fastutil.ints.Int2ObjectArrayMap;
import mankool.mcbot.protocol.Commands.ClickContainerSlotCommand;
import net.minecraft.SharedConstants;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.HashedStack;
import net.minecraft.network.protocol.game.ServerboundContainerClickPacket;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.worldselection.WorldSelectionList;
import net.minecraft.client.input.CharacterEvent;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.client.input.MouseButtonInfo;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.multiplayer.MultiPlayerGameMode;
import net.minecraft.network.chat.Component;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.Registry;
import net.minecraft.network.protocol.game.ClientboundContainerSetContentPacket;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.inventory.ContainerInput;
import net.minecraft.world.item.Item;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.world.level.storage.LevelSummary;

public class VersionCompat {

    public static String keyId(ResourceKey<?> key) {
        return key.identifier().toString();
    }

    public static <T> T registryGet(Registry<T> registry, String id) {
        Identifier parsed = Identifier.tryParse(id);
        if (parsed == null) return null;
        return registry.getValue(parsed);
    }

    public static <T> String registryGetKeyId(Registry<T> registry, T value) {
        var key = registry.getKey(value);
        return key != null ? key.toString() : null;
    }

    public static <T> ResourceKey<T> parseResourceKey(ResourceKey<? extends Registry<T>> registry, String id) {
        return ResourceKey.create(registry, Identifier.parse(id));
    }

    public static int getDataVersion() {
        return SharedConstants.getCurrentVersion().dataVersion().version();
    }

    public static String getVersionName() {
        return SharedConstants.getCurrentVersion().name();
    }

    public static String getVersionSeries() {
        return SharedConstants.getCurrentVersion().dataVersion().series();
    }

    public static boolean isVersionSnapshot() {
        return !SharedConstants.getCurrentVersion().stable();
    }

    public static void screenMouseClicked(Screen screen, double x, double y, int button) {
        screen.mouseClicked(new MouseButtonEvent(x, y, new MouseButtonInfo(button, 0)), false);
    }

    public static void screenCharTyped(Screen screen, int codePoint) {
        screen.charTyped(new CharacterEvent(codePoint));
    }

    public static void screenKeyPressed(Screen screen, int key, int modifiers) {
        screen.keyPressed(new KeyEvent(key, 0, modifiers));
    }

    public static void screenKeyReleased(Screen screen, int key, int modifiers) {
        screen.keyReleased(new KeyEvent(key, 0, modifiers));
    }

    public static void disconnectLevel(ClientLevel level) {
        level.disconnect(Component.empty());
    }

    public static UUID profileId(GameProfile profile) {
        return profile.id();
    }

    public static String profileName(GameProfile profile) {
        return profile.name();
    }

    public static int getSelectedSlot(Inventory inventory) {
        return inventory.getSelectedSlot();
    }

    public static void setSelectedSlot(Inventory inventory, int slot) {
        inventory.setSelectedSlot(slot);
    }

    public static int getContainerId(ClientboundContainerSetContentPacket packet) {
        return packet.containerId();
    }

    public static void addBreakingBlockEffect(ClientLevel level, BlockPos pos, Direction face) {
        level.addBreakingBlockEffect(pos, face);
    }

    public static LevelSummary getLevelSummary(WorldSelectionList.WorldListEntry entry) {
        return entry.getLevelSummary();
    }

    public static int chunkPosX(ChunkPos pos) {
        return pos.x();
    }

    public static int chunkPosZ(ChunkPos pos) {
        return pos.z();
    }

    public static boolean isItemComponentsBound(Item item) {
        return item.builtInRegistryHolder().areComponentsBound();
    }

    public static void clickContainerSlot(MultiPlayerGameMode gameMode, int containerId, int slotIndex, int button, ClickContainerSlotCommand.ClickType protoClickType, Player player) {
        ContainerInput containerInput;
        switch (protoClickType) {
            case PICKUP: containerInput = ContainerInput.PICKUP; break;
            case QUICK_MOVE: containerInput = ContainerInput.QUICK_MOVE; break;
            case SWAP: containerInput = ContainerInput.SWAP; break;
            case CLONE: containerInput = ContainerInput.CLONE; break;
            case THROW: containerInput = ContainerInput.THROW; break;
            case QUICK_CRAFT: containerInput = ContainerInput.QUICK_CRAFT; break;
            case PICKUP_ALL: containerInput = ContainerInput.PICKUP_ALL; break;
            default: throw new IllegalArgumentException("Unknown click type: " + protoClickType);
        }
        gameMode.handleContainerInput(containerId, slotIndex, button, containerInput, player);
    }

    public static void requestInventoryResync(LocalPlayer player) {
        player.connection.send(new ServerboundContainerClickPacket(
            0,
            player.containerMenu.getStateId() - 1,
            (short) -999,
            (byte) 0,
            ContainerInput.PICKUP,
            new Int2ObjectArrayMap<>(),
            HashedStack.EMPTY
        ));
    }
}
