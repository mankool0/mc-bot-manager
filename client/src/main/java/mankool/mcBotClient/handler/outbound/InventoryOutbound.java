package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Inventory;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.util.ProtoUtil;
import mankool.mcBotClient.util.VersionCompat;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.item.ItemStack;
import mankool.mcBotClient.connection.PipeConnection;

import java.lang.ref.WeakReference;
import java.util.UUID;

public class InventoryOutbound extends BaseOutbound {
    private static final int CURSOR_SLOT = -1;

    private static InventoryOutbound instance;
    private boolean pendingUpdate = false;

    private ItemStack[] sentItems = null;
    private ItemStack sentCursor = ItemStack.EMPTY;
    private int sentSelectedSlot = -1;
    private boolean forceFull = false;

    private WeakReference<LocalPlayer> snapshotOwner = new WeakReference<>(null);

    public InventoryOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
        instance = this;
    }

    public static InventoryOutbound getInstance() {
        return instance;
    }

    @Override
    protected void onClientTick(Minecraft client) {
        LocalPlayer player = client.player;
        if (player == null) return;

        if (VersionCompat.getSelectedSlot(player.getInventory()) != sentSelectedSlot) {
            pendingUpdate = true;
        }

        // Send batched inventory update once per tick
        if (pendingUpdate) {
            pendingUpdate = false;
            sendUpdate(player);
        }
    }

    public void queueUpdate() {
        pendingUpdate = true;
    }

    /**
     * Queues an update that is guaranteed to be a full snapshot rather than a delta.
     * Used when the manager's view may have drifted and a diff cannot repair it.
     */
    public void queueFullUpdate() {
        forceFull = true;
        pendingUpdate = true;
    }

    private void sendUpdate(LocalPlayer player) {
        var inventory = player.getInventory();
        int size = inventory.getContainerSize();
        int selectedSlot = VersionCompat.getSelectedSlot(inventory);
        ItemStack cursor = player.containerMenu.getCarried();

        boolean needsFullSync = forceFull
            || sentItems == null
            || sentItems.length != size
            || snapshotOwner.get() != player;

        if (needsFullSync) {
            sendFull(player, size, selectedSlot, cursor);
        } else {
            sendDelta(player, size, selectedSlot, cursor);
        }
    }

    private void sendFull(LocalPlayer player, int size, int selectedSlot, ItemStack cursor) {
        var inventory = player.getInventory();
        var update = Inventory.InventoryUpdate.newBuilder()
            .setSelectedSlot(selectedSlot);

        sentItems = new ItemStack[size];
        for (int slot = 0; slot < size; slot++) {
            ItemStack item = inventory.getItem(slot);
            update.addItems(ProtoUtil.buildItemStack(item, slot));
            sentItems[slot] = item.copy();
        }
        update.setCursorItem(ProtoUtil.buildItemStack(cursor, CURSOR_SLOT));

        connection.sendMessage(Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setInventory(update.build())
            .build());

        sentCursor = cursor.copy();
        sentSelectedSlot = selectedSlot;
        snapshotOwner = new WeakReference<>(player);
        forceFull = false;
    }

    private void sendDelta(LocalPlayer player, int size, int selectedSlot, ItemStack cursor) {
        var inventory = player.getInventory();
        var delta = Inventory.InventoryDeltaUpdate.newBuilder();

        for (int slot = 0; slot < size; slot++) {
            ItemStack item = inventory.getItem(slot);
            if (ItemStack.matches(sentItems[slot], item)) continue;
            delta.addChangedItems(ProtoUtil.buildItemStack(item, slot));
            sentItems[slot] = item.copy();
        }

        if (selectedSlot != sentSelectedSlot) {
            delta.setSelectedSlot(selectedSlot);
            sentSelectedSlot = selectedSlot;
        }

        if (!ItemStack.matches(sentCursor, cursor)) {
            delta.setCursorItem(ProtoUtil.buildItemStack(cursor, CURSOR_SLOT));
            sentCursor = cursor.copy();
        }

        // The change hooks fire on plenty of writes that change nothing (the server resending
        // identical container contents, for one), so empty deltas are common.
        if (delta.getChangedItemsCount() == 0 && !delta.hasSelectedSlot() && !delta.hasCursorItem()) {
            return;
        }

        connection.sendMessage(Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setInventoryDelta(delta.build())
            .build());
    }
}
