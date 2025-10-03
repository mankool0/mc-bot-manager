package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Commands;
import me.mankool.mcBotClient.connection.PipeConnection;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;

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

        // TODO: Implement hotbar switching when we have proper access
        System.out.println("Switch to hotbar slot: " + slot);
        sendFailure(messageId, "Hotbar switching not implemented yet");
    }

    public void handleUseItem(String messageId, Commands.UseItemCommand command) {
        ClientPlayerEntity player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        // TODO: Use item in specified hand
        System.out.println("Use item in hand: " + command.getHand());
        sendFailure(messageId, "Item use not implemented yet");
    }

    public void handleDropItem(String messageId, Commands.DropItemCommand command) {
        ClientPlayerEntity player = client.player;
        if (player == null) {
            sendFailure(messageId, "Not in game");
            return;
        }

        // TODO: Drop item(s)
        System.out.println("Drop item, all: " + command.getDropAll());
        sendFailure(messageId, "Item drop not implemented yet");
    }
}