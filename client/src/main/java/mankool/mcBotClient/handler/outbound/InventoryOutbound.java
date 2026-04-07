package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Common;
import mankool.mcbot.protocol.Inventory;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.util.ProtoUtil;
import mankool.mcBotClient.util.VersionCompat;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import mankool.mcBotClient.connection.PipeConnection;
import java.util.UUID;

public class InventoryOutbound extends BaseOutbound {
    private static InventoryOutbound instance;
    private boolean pendingUpdate = false;
    private int lastSelectedSlot = -1;

    public InventoryOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
        instance = this;
    }

    public static InventoryOutbound getInstance() {
        return instance;
    }

    @Override
    protected void onClientTick(Minecraft client) {
        if (client.player != null) {
            int currentSlot = VersionCompat.getSelectedSlot(client.player.getInventory());
            if (currentSlot != lastSelectedSlot) {
                lastSelectedSlot = currentSlot;
                pendingUpdate = true;
            }
        }
        // Send batched inventory update once per tick
        if (pendingUpdate) {
            pendingUpdate = false;
            sendUpdate();
        }
    }

    public void queueUpdate() {
        pendingUpdate = true;
    }

    private void sendUpdate() {
        LocalPlayer player = client.player;
        if (player == null) return;

        Inventory.InventoryUpdate.Builder inventoryBuilder = Inventory.InventoryUpdate.newBuilder()
            .setSelectedSlot(VersionCompat.getSelectedSlot(player.getInventory()));

        // Add all inventory slots
        for (int i = 0; i < player.getInventory().getContainerSize(); i++) {
            net.minecraft.world.item.ItemStack itemStack = player.getInventory().getItem(i);
            inventoryBuilder.addItems(ProtoUtil.buildItemStack(itemStack, i));
        }

        inventoryBuilder.setCursorItem(ProtoUtil.buildItemStack(player.containerMenu.getCarried(), -1));

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setInventory(inventoryBuilder.build())
            .build();

        connection.sendMessage(message);
    }
}