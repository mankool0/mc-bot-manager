package mankool.mcBotClient.util;

import com.mojang.authlib.GameProfile;
import net.minecraft.SharedConstants;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.worldselection.WorldSelectionList;
import net.minecraft.client.input.CharacterEvent;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.client.input.MouseButtonInfo;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.network.chat.Component;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.Registry;
import net.minecraft.network.protocol.game.ClientboundContainerSetContentPacket;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.entity.player.Inventory;
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
        screen.charTyped(new CharacterEvent(codePoint, 0));
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
}
