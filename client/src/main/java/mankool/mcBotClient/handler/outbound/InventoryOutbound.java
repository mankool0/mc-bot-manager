package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Common;
import mankool.mcbot.protocol.Inventory;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.util.ProtoUtil;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import mankool.mcBotClient.connection.PipeConnection;
import java.util.UUID;

public class InventoryOutbound extends BaseOutbound {
    private static InventoryOutbound instance;
    private boolean pendingUpdate = false;

    public InventoryOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
        instance = this;
    }

    public static InventoryOutbound getInstance() {
        return instance;
    }

    @Override
    protected void onClientTick(Minecraft client) {
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
            .setSelectedSlot(player.getInventory().getSelectedSlot());

        // Add all inventory slots
        for (int i = 0; i < player.getInventory().getContainerSize(); i++) {
            net.minecraft.world.item.ItemStack itemStack = player.getInventory().getItem(i);
            inventoryBuilder.addItems(ProtoUtil.buildItemStack(itemStack, i));
        }

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setInventory(inventoryBuilder.build())
            .build();

        connection.sendMessage(message);
    }
}