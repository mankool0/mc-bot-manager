package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Common;
import me.mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;
import net.minecraft.util.Hand;

public class InventoryHandler extends BaseInboundHandler {

    public InventoryHandler(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleSwitchHotbar(String messageId, Commands.SwitchHotbarSlotCommand command) {
        ClientPlayerEntity player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        int slot = command.getSlot();
        if (slot < 0 || slot > 8) {
            sendFailure(messageId, "Invalid slot: " + slot + " (must be 0-8)");
            return;
        }

        try {
            player.getInventory().setSelectedSlot(slot);
            sendSuccess(messageId, "Switched to hotbar slot " + slot);
        } catch (Exception e) {
            System.err.println("Failed to switch hotbar slot: " + e.getMessage());
            sendFailure(messageId, "Failed to switch slot: " + e.getMessage());
        }
    }

    public void handleUseItem(String messageId, Commands.UseItemCommand command) {
        ClientPlayerEntity player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            Hand hand = command.getHand() == Common.Hand.OFF_HAND ? Hand.OFF_HAND : Hand.MAIN_HAND;

            if (client.interactionManager != null) {
                client.interactionManager.interactItem(player, hand);
                sendSuccess(messageId, "Used item in " + hand + " hand");
            } else {
                sendFailure(messageId, "Interaction manager not available");
            }
        } catch (Exception e) {
            System.err.println("Failed to use item: " + e.getMessage());
            sendFailure(messageId, "Failed to use item: " + e.getMessage());
        }
    }

    public void handleDropItem(String messageId, Commands.DropItemCommand command) {
        ClientPlayerEntity player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            boolean dropAll = command.getDropAll();
            boolean dropped = player.dropSelectedItem(dropAll);

            if (dropped) {
                sendSuccess(messageId, "Dropped item" + (dropAll ? " stack" : ""));
            } else {
                sendFailure(messageId, "No item to drop");
            }
        } catch (Exception e) {
            System.err.println("Failed to drop item: " + e.getMessage());
            sendFailure(messageId, "Failed to drop item: " + e.getMessage());
        }
    }
}