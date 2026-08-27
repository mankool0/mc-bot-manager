package mankool.mcBotClient.handler;

import mankool.mcbot.protocol.*;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.handler.inbound.*;
import mankool.mcBotClient.handler.outbound.*;
import mankool.mcBotClient.integration.ClientIntegration;
import mankool.mcBotClient.integration.ConnectionContext;
import mankool.mcBotClient.integration.IntegrationRegistry;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.Minecraft;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.util.ArrayList;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;

public class MessageHandler {
    private static final Logger LOGGER = LoggerFactory.getLogger(MessageHandler.class);

    private static final AtomicBoolean tickHookInstalled = new AtomicBoolean(false);
    private static volatile MessageHandler activeHandler;

    private final PipeConnection connection;
    private final Minecraft client;
    private volatile boolean running = false;
    private int tick = 0;

    private final Map<Protocol.ManagerToClientMessage.PayloadCase, Consumer<Protocol.ManagerToClientMessage>> handlers;

    private final List<Runnable> integrationTicks = new ArrayList<>();
    private final List<BaseOutbound> outbounds;

    // Inbound handlers (handle incoming commands)
    private final ConnectionHandler connectionHandler;
    private final PlayerActionHandler playerActionHandler;
    private final InventoryHandler inventoryHandler;
    private final ChatHandler chatHandler;
    private final WorldInteractionHandler worldInteractionHandler;
    private final ScreenInteractionHandler screenInteractionHandler;

    // Outbound handlers (send data updates)
    private final ServerOutbound serverOutbound;
    private final PlayerOutbound playerOutbound;
    private final InventoryOutbound inventoryOutbound;
    private final ChatOutbound chatOutbound;
    private final WorldOutbound worldOutbound;
    private final ContainerOutbound containerOutbound;
    private final ScreenOutbound screenOutbound;
    private final EntityOutbound entityOutbound;
    private final TabListOutbound tabListOutbound;
    private final StatsOutbound statsOutbound;
    private final WindowOutbound windowOutbound;

    public MessageHandler(PipeConnection connection, Minecraft client) {
        this.connection = connection;
        this.client = client;

        // Initialize inbound handlers
        this.connectionHandler = new ConnectionHandler(this.client, connection);
        this.playerActionHandler = new PlayerActionHandler(this.client, connection);
        this.inventoryHandler = new InventoryHandler(this.client, connection);
        this.chatHandler = new ChatHandler(this.client, connection);
        this.worldInteractionHandler = new WorldInteractionHandler(this.client, connection);

        // Initialize outbound handlers
        this.serverOutbound = new ServerOutbound(this.client, connection);
        this.playerOutbound = new PlayerOutbound(this.client, connection);
        this.inventoryOutbound = new InventoryOutbound(this.client, connection);
        this.chatOutbound = new ChatOutbound(this.client, connection);
        this.worldOutbound = new WorldOutbound(this.client, connection);
        this.containerOutbound = new ContainerOutbound(this.client, connection);
        this.screenOutbound = new ScreenOutbound(this.client, connection);
        this.entityOutbound = new EntityOutbound(this.client, connection);
        this.tabListOutbound = new TabListOutbound(this.client, connection);
        this.statsOutbound = new StatsOutbound(this.client, connection);
        this.windowOutbound = new WindowOutbound(this.client, connection);
        this.screenInteractionHandler = new ScreenInteractionHandler(this.client, connection, this.screenOutbound);

        // Ticked from onClientTick below, in construction order
        this.outbounds = List.of(serverOutbound, playerOutbound, inventoryOutbound,
                                 worldOutbound, containerOutbound, screenOutbound, entityOutbound,
                                 tabListOutbound, statsOutbound, windowOutbound);

        // Register message handlers
        this.handlers = new EnumMap<>(Protocol.ManagerToClientMessage.PayloadCase.class);
        registerHandlers();

        setupIntegrations(connection);

        installTickHook();
    }

    // Installs the single shared tick hook the first time a handler is built. The lambda holds no
    // reference to any instance, so a handler becomes collectable as soon as it stops being the
    // active one.
    private static void installTickHook() {
        if (!tickHookInstalled.compareAndSet(false, true)) {
            return;
        }
        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            MessageHandler handler = activeHandler;
            if (handler != null) {
                handler.onClientTick(client);
            }
        });
    }

    private void setupIntegrations(PipeConnection connection) {
        ConnectionContext ctx = new ConnectionContext() {
            @Override
            public Minecraft client() {
                return client;
            }

            @Override
            public PipeConnection connection() {
                return connection;
            }

            @Override
            public void registerHandler(Protocol.ManagerToClientMessage.PayloadCase payloadCase,
                                        Consumer<Protocol.ManagerToClientMessage> handler) {
                handlers.put(payloadCase, handler);
            }

            @Override
            public void registerTick(Runnable tickHook) {
                integrationTicks.add(tickHook);
            }
        };

        for (ClientIntegration integration : IntegrationRegistry.integrations()) {
            try {
                integration.onConnectionSetup(ctx);
            } catch (Throwable t) {
                LOGGER.error("Integration {} failed onConnectionSetup", integration.getClass().getName(), t);
            }
        }
    }

    private void registerHandlers() {
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.HANDSHAKE_REJECT,
            msg -> connectionHandler.handleHandshakeReject(msg.getHandshakeReject()));
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
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.INTERACT_WITH_BLOCK,
            msg -> worldInteractionHandler.handleInteractWithBlock(msg.getMessageId(), msg.getInteractWithBlock()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.CAN_REACH_BLOCKS,
            msg -> worldInteractionHandler.handleCanReachBlocks(msg.getMessageId(), msg.getCanReachBlocks()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.REGISTRY_RESPONSE,
            msg -> worldOutbound.handleRegistryResponse(msg.getRegistryResponse()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.ITEM_REGISTRY_RESPONSE,
            msg -> worldOutbound.handleItemRegistryResponse(msg.getItemRegistryResponse()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.CLICK_CONTAINER_SLOT,
            msg -> inventoryHandler.handleClickContainerSlot(msg.getMessageId(), msg.getClickContainerSlot()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.CLOSE_CONTAINER,
            msg -> inventoryHandler.handleCloseContainer(msg.getMessageId(), msg.getCloseContainer()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.OPEN_INVENTORY,
            msg -> inventoryHandler.handleOpenInventory(msg.getMessageId(), msg.getOpenInventory()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.CLICK_SCREEN_WIDGET,
            msg -> screenInteractionHandler.handleClickScreenWidget(msg.getMessageId(), msg.getClickScreenWidget()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.CLICK_SCREEN_POSITION,
            msg -> screenInteractionHandler.handleClickScreenPosition(msg.getMessageId(), msg.getClickScreenPosition()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.TYPE_TEXT,
            msg -> screenInteractionHandler.handleTypeText(msg.getMessageId(), msg.getTypeText()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.PRESS_KEY,
            msg -> screenInteractionHandler.handlePressKey(msg.getMessageId(), msg.getPressKey()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.OPEN_GAME_MENU,
            msg -> screenInteractionHandler.handleOpenGameMenu(msg.getMessageId()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.HOLD_ATTACK,
            msg -> worldInteractionHandler.handleHoldAttack(msg.getHoldAttack()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.GET_HOLD_ATTACK_STATUS,
            msg -> worldInteractionHandler.handleGetHoldAttackStatus(msg.getMessageId()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.REQUEST_INVENTORY_RESYNC,
            msg -> inventoryHandler.handleRequestInventoryResync(msg.getMessageId()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.REQUEST_STATISTICS,
            msg -> statsOutbound.handleRequestStatistics(msg.getMessageId()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.SET_WINDOW,
            msg -> windowOutbound.handleSetWindow(msg.getMessageId(), msg.getSetWindow()));
        handlers.put(Protocol.ManagerToClientMessage.PayloadCase.GET_WINDOW_STATE,
            msg -> windowOutbound.handleGetWindowState(msg.getMessageId()));
    }

    public void start() {
        if (running) {
            return;
        }

        running = true;
        // Takes over the shared tick hook, which drops the previous handler's last reference.
        activeHandler = this;

        // Send initial connection info
        serverOutbound.sendConnectionInfo();

        // Window geometry and monitors, so the manager can place the window right away
        windowOutbound.scheduleReport("");

        // Send block registry query
        worldOutbound.sendRegistryQuery();

        // Send item registry query
        worldOutbound.sendItemRegistryQuery();

        // Seed the manager with a full inventory snapshot. Later updates are deltas against
        // it, so a fresh connection must not have to wait for the next inventory change.
        inventoryOutbound.queueFullUpdate();

        LOGGER.info("MessageHandler started, processing messages on game tick");
    }

    public void stop() {
        running = false;
        // Only clear the hook if a newer handler has not already taken it over.
        if (activeHandler == this) {
            activeHandler = null;
        }
    }

    public ServerOutbound getServerOutbound() {
        return serverOutbound;
    }

    private void handleMessage(Protocol.ManagerToClientMessage message) {
        Consumer<Protocol.ManagerToClientMessage> handler = handlers.get(message.getPayloadCase());
        if (handler != null) {
            handler.accept(message);
        } else {
            LOGGER.warn("Unknown message type: {}", message.getPayloadCase());
        }
    }

    private void onClientTick(Minecraft client) {
        if (!running) {
            return;
        }
        tick++;

        for (BaseOutbound outbound : outbounds) {
            outbound.tick(client);
        }

        // Process all pending messages from manager
        Protocol.ManagerToClientMessage message;
        while ((message = connection.receiveMessage()) != null) {
            handleMessage(message);
        }

        // Send heartbeat every second (20 ticks)
        if (tick % 20 == 0) {
            connection.sendHeartbeat();
        }

        for (Runnable tickHook : integrationTicks) {
            tickHook.run();
        }

        // Tick world interaction handler for continuous actions
        worldInteractionHandler.tick();
    }
}