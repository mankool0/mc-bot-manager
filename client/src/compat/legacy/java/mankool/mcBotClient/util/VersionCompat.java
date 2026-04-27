package mankool.mcBotClient.util;

import com.mojang.authlib.GameProfile;
import java.util.UUID;
import net.minecraft.SharedConstants;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.worldselection.WorldSelectionList;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.Registry;
import net.minecraft.network.protocol.game.ClientboundContainerSetContentPacket;
import net.minecraft.resources.ResourceKey;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.level.storage.LevelSummary;

public class VersionCompat {

    public static String keyId(ResourceKey<?> key) {
        return key.location().toString();
    }

    public static <T> T registryGet(Registry<T> registry, String id) {
        ResourceLocation parsed = ResourceLocation.tryParse(id);
        if (parsed == null) return null;
        return registry.getValue(parsed);
    }

    public static <T> String registryGetKeyId(Registry<T> registry, T value) {
        var key = registry.getKey(value);
        return key != null ? key.toString() : null;
    }

    public static <T> ResourceKey<T> parseResourceKey(ResourceKey<? extends Registry<T>> registry, String id) {
        return ResourceKey.create(registry, ResourceLocation.parse(id));
    }

    public static int getDataVersion() {
        return SharedConstants.getCurrentVersion().getDataVersion().getVersion();
    }

    public static String getVersionName() {
        return SharedConstants.getCurrentVersion().getName();
    }

    public static String getVersionSeries() {
        // 1.21.4 is in the main release series
        return "main";
    }

    public static boolean isVersionSnapshot() {
        return !SharedConstants.getCurrentVersion().isStable();
    }

    public static void screenMouseClicked(Screen screen, double x, double y, int button) {
        screen.mouseClicked(x, y, button);
    }

    public static void screenCharTyped(Screen screen, int codePoint) {
        screen.charTyped((char) codePoint, 0);
    }

    public static void screenKeyPressed(Screen screen, int key, int modifiers) {
        screen.keyPressed(key, 0, modifiers);
    }

    public static void screenKeyReleased(Screen screen, int key, int modifiers) {
        screen.keyReleased(key, 0, modifiers);
    }

    public static void disconnectLevel(ClientLevel level) {
        level.disconnect();
    }

    public static UUID profileId(GameProfile profile) {
        return profile.getId();
    }

    public static String profileName(GameProfile profile) {
        return profile.getName();
    }

    public static int getSelectedSlot(Inventory inventory) {
        return inventory.selected;
    }

    public static void setSelectedSlot(Inventory inventory, int slot) {
        inventory.selected = slot;
    }

    public static int getContainerId(ClientboundContainerSetContentPacket packet) {
        return packet.getContainerId();
    }

    public static void addBreakingBlockEffect(ClientLevel level, BlockPos pos, Direction face) {
        level.addDestroyBlockEffect(pos, level.getBlockState(pos));
    }

    public static LevelSummary getLevelSummary(WorldSelectionList.WorldListEntry entry) {
        try {
            var field = entry.getClass().getDeclaredField("summary");
            field.setAccessible(true);
            return (LevelSummary) field.get(entry);
        } catch (Exception e) {
            return null;
        }
    }
}
