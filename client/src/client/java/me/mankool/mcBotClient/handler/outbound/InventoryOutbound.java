package me.mankool.mcBotClient.handler.outbound;

import mankool.mcbot.protocol.Common;
import mankool.mcbot.protocol.Inventory;
import mankool.mcbot.protocol.Protocol;
import me.mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;

import java.util.UUID;

public class InventoryOutbound extends BaseOutbound {
    private int tickCounter = 0;

    public InventoryOutbound(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(MinecraftClient client) {
        if (client.player == null || client.world == null) {
            return;
        }

        tickCounter++;

        // Send inventory update every 10 ticks
        if (tickCounter % 10 == 0) {
            sendUpdate();
        }
    }

    private void sendUpdate() {
        ClientPlayerEntity player = client.player;
        if (player == null) return;

        Inventory.InventoryUpdate.Builder inventoryBuilder = Inventory.InventoryUpdate.newBuilder()
            .setSelectedSlot(player.getInventory().getSelectedSlot());

        // Add all inventory items
        for (int i = 0; i < player.getInventory().size(); i++) {
            net.minecraft.item.ItemStack itemStack = player.getInventory().getStack(i);
            if (!itemStack.isEmpty()) {
                Common.ItemStack protoItem = Common.ItemStack.newBuilder()
                    .setSlot(i)
                    .setItemId(itemStack.getItem().toString())
                    .setCount(itemStack.getCount())
                    .setDamage(itemStack.getDamage())
                    .setMaxDamage(itemStack.getMaxDamage())
                    .setDisplayName(itemStack.getName().getString())
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