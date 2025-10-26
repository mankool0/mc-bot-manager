package me.mankool.mcBotClient.handler;

import mankool.mcbot.protocol.*;
import me.mankool.mcBotClient.connection.PipeConnection;
import me.mankool.mcBotClient.handler.inbound.*;
import me.mankool.mcBotClient.handler.outbound.*;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.MinecraftClient;

import java.util.EnumMap;
import java.util.Map;
import java.util.function.Consumer;

public class MessageHandler {
    private final PipeConnection connection;
    private final MinecraftClient client;
    private volatile boolean running = false;
    private int tick = 0;

    private final Map<Protocol.ManagerToClientMessage.PayloadCase, Consumer<Protocol.ManagerToClientMessage>> handlers;

    // Inbound handlers (handle incoming commands)
    private final ConnectionHandler connectionHandler;
    private final PlayerActionHandler playerActionHandler;
    private final InventoryHandler inventoryHandler;
    private final ChatHandler chatHandler;
    private final MeteorModuleHandler meteorModuleHandler;
    private final BaritoneHandler baritoneHandler;

    // Outbound handlers (send data updates)
    private final ServerOutbound serverOutbound;
    private final PlayerOutbound playerOutbound;
    private final InventoryOutbound inventoryOutbound;

    public MessageHandler(PipeConnection connection, MinecraftClient client) {
        this.connection = connection;
        this.client = client;

        // Initialize inbound handlers
        this.connectionHandler = new ConnectionHandler(client, connection);
        this.playerActionHandler = new PlayerActionHandler(client, connection);
        this.inventoryHandler = new InventoryHandler(client, connection);
        this.chatHandler = new ChatHandler(client, connection);
        this.meteorModuleHandler = new MeteorModuleHandler(client, connection);
        this.baritoneHandler = new BaritoneHandler(client, connection);

        // Initialize outbound handlers
        this.serverOutbound = new ServerOutbound(client, connection);
        this.playerOutbound = new PlayerOutbound(client, connection);
        this.inventoryOutbound = new InventoryOutbound(client, connection);

        // Register message handlers
        this.handlers = new EnumMap<>(Protocol.ManagerToClientMessage.PayloadCase.class);
        registerHandlers();

        // Register tick handler for protocol-level updates
        ClientTickEvents.END_CLIENT_TICK.register(this::onClientTick);
    }

    private void registerHandlers() {
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.CONNECT_SERVER,
            msg -> connectionHandler.handleConnectToServer(msg.getMessageId(), msg.getConnectServer()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.DISCONNECT,
            msg -> connectionHandler.handleDisconnect(msg.getMessageId(), msg.getDisconnect()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.SEND_CHAT,
            msg -> chatHandler.handleSendChat(msg.getMessageId(), msg.getSendChat()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.MOVE_TO,
            msg -> playerActionHandler.handleMoveTo(msg.getMessageId(), msg.getMoveTo()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.LOOK_AT,
            msg -> playerActionHandler.handleLookAt(msg.getMessageId(), msg.getLookAt()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.SET_ROTATION,
            msg -> playerActionHandler.handleSetRotation(msg.getMessageId(), msg.getSetRotation()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.SWITCH_HOTBAR,
            msg -> inventoryHandler.handleSwitchHotbar(msg.getMessageId(), msg.getSwitchHotbar()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.USE_ITEM,
            msg -> inventoryHandler.handleUseItem(msg.getMessageId(), msg.getUseItem()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.DROP_ITEM,
            msg -> inventoryHandler.handleDropItem(msg.getMessageId(), msg.getDropItem()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.SHUTDOWN,
            msg -> connectionHandler.handleShutdown(msg.getMessageId(), msg.getShutdown()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.GET_MODULES,
            msg -> meteorModuleHandler.handleGetModules(msg.getMessageId(), msg.getGetModules()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.SET_MODULE_CONFIG,
            msg -> meteorModuleHandler.handleSetModuleConfig(msg.getMessageId(), msg.getSetModuleConfig()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.GET_BARITONE_SETTINGS,
            msg -> baritoneHandler.handleGetBaritoneSettings(msg.getMessageId(), msg.getGetBaritoneSettings()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.GET_BARITONE_COMMANDS,
            msg -> baritoneHandler.handleGetBaritoneCommands(msg.getMessageId(), msg.getGetBaritoneCommands()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.SET_BARITONE_SETTINGS,
            msg -> baritoneHandler.handleSetBaritoneSettings(msg.getMessageId(), msg.getSetBaritoneSettings()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.EXECUTE_BARITONE_COMMAND,
            msg -> baritoneHandler.handleExecuteBaritoneCommand(msg.getMessageId(), msg.getExecuteBaritoneCommand()));
    }

    public void start() {
        if (running) {
            return;
        }

        running = true;

        // Send initial connection info
        serverOutbound.sendConnectionInfo();

        System.out.println("MessageHandler started, processing messages on game tick");
    }

    public void stop() {
        running = false;
    }

    private void handleMessage(Protocol.ManagerToClientMessage message) {
        Consumer<Protocol.ManagerToClientMessage> handler = handlers.get(message.getPayloadCase());
        if (handler != null) {
            handler.accept(message);
        } else {
            System.err.println("Unknown message type: " + message.getPayloadCase());
        }
    }

    private void onClientTick(MinecraftClient client) {
        if (!running) {
            return;
        }
        tick++;

        // Process all pending messages from manager
        Protocol.ManagerToClientMessage message;
        while ((message = connection.receiveMessage()) != null) {
            handleMessage(message);
        }

        // Send heartbeat every second (20 ticks)
        if (tick % 20 == 0) {
            connection.sendHeartbeat();
        }

        // Tick Baritone handler for setting change polling
        baritoneHandler.tick();
    }
}