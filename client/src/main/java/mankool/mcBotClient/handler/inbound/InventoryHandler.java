package mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Common;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.InteractionHand;
import mankool.mcBotClient.connection.PipeConnection;

public class InventoryHandler extends BaseInboundHandler {

    public InventoryHandler(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    public void handleSwitchHotbar(String messageId, Commands.SwitchHotbarSlotCommand command) {
        LocalPlayer player = client.player;
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
        LocalPlayer player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            InteractionHand hand = command.getHand() == Common.Hand.OFF_HAND ? InteractionHand.OFF_HAND : InteractionHand.MAIN_HAND;

            if (client.gameMode != null) {
                client.gameMode.useItem(player, hand);
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
        LocalPlayer player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            boolean dropAll = command.getDropAll();
            boolean dropped = player.drop(dropAll);

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