package mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Common;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.inventory.ClickType;
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

    public void handleClickContainerSlot(String messageId, Commands.ClickContainerSlotCommand command) {
        LocalPlayer player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        if (player.containerMenu == null) {
            sendFailure(messageId, "No container open");
            return;
        }

        try {
            int slotIndex = command.getSlotIndex();
            int button = command.getButton();

            // Map protobuf ClickType to Minecraft ClickType
            ClickType clickType;
            switch (command.getClickType()) {
                case PICKUP:
                    clickType = ClickType.PICKUP;
                    break;
                case QUICK_MOVE:
                    clickType = ClickType.QUICK_MOVE;
                    break;
                case SWAP:
                    clickType = ClickType.SWAP;
                    break;
                case CLONE:
                    clickType = ClickType.CLONE;
                    break;
                case THROW:
                    clickType = ClickType.THROW;
                    break;
                case QUICK_CRAFT:
                    clickType = ClickType.QUICK_CRAFT;
                    break;
                case PICKUP_ALL:
                    clickType = ClickType.PICKUP_ALL;
                    break;
                default:
                    sendFailure(messageId, "Unknown click type: " + command.getClickType());
                    return;
            }

            if (client.gameMode != null) {
                int containerId = player.containerMenu.containerId;
                client.gameMode.handleInventoryMouseClick(
                    containerId,
                    slotIndex,
                    button,
                    clickType,
                    player
                );
                sendSuccess(messageId, "Clicked slot " + slotIndex + " with button " + button + " (type: " + clickType + ")");
            } else {
                sendFailure(messageId, "Game mode not available");
            }
        } catch (Exception e) {
            System.err.println("Failed to click container slot: " + e.getMessage());
            e.printStackTrace();
            sendFailure(messageId, "Failed to click slot: " + e.getMessage());
        }
    }

    public void handleCloseContainer(String messageId, Commands.CloseContainerCommand command) {
        LocalPlayer player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        try {
            System.err.println("Trying to close container");
            player.closeContainer();
            System.err.println("Closed container");
            sendSuccess(messageId, "Closed container");
        } catch (Exception e) {
            System.err.println("Failed to close container: " + e.getMessage());
            sendFailure(messageId, "Failed to close container: " + e.getMessage());
        }
    }
}