package mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Common;
import mankool.mcbot.protocol.Inventory;
import mankool.mcbot.protocol.Protocol;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import mankool.mcBotClient.connection.PipeConnection;
import java.util.UUID;

public class InventoryOutbound extends BaseOutbound {
    private int tickCounter = 0;

    public InventoryOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(Minecraft client) {
        if (client.player == null || client.level == null) {
            return;
        }

        tickCounter++;

        // Send inventory update every 10 ticks
        if (tickCounter % 10 == 0) {
            sendUpdate();
        }
    }

    private void sendUpdate() {
        LocalPlayer player = client.player;
        if (player == null) return;

        Inventory.InventoryUpdate.Builder inventoryBuilder = Inventory.InventoryUpdate.newBuilder()
            .setSelectedSlot(player.getInventory().getSelectedSlot());

        // Add all inventory items
        for (int i = 0; i < player.getInventory().getContainerSize(); i++) {
            net.minecraft.world.item.ItemStack itemStack = player.getInventory().getItem(i);
            if (!itemStack.isEmpty()) {
                Common.ItemStack protoItem = Common.ItemStack.newBuilder()
                    .setSlot(i)
                    .setItemId(itemStack.getItem().toString())
                    .setCount(itemStack.getCount())
                    .setDamage(itemStack.getDamageValue())
                    .setMaxDamage(itemStack.getMaxDamage())
                    .setDisplayName(itemStack.getHoverName().getString())
                    .build();
                inventoryBuilder.addItems(protoItem);
            }
        }

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setInventory(inventoryBuilder.build())
            .build();

        connection.sendMessage(message);
    }
}