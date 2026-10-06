package mankool.mcBotClient.plugin;

import com.google.gson.JsonObject;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcbot.protocol.Plugin;
import mankool.mcbot.protocol.Protocol;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.protocol.Packet;
import net.minecraft.world.phys.Vec3;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/**
 * The plugins loaded into this client, for the life of the JVM: they outlive a manager
 * reconnect, and a load under a name already taken replaces that plugin. Compiles off the game
 * thread; everything else, the hooks included, runs on it.
 */
public final class PluginHost {

    private static final Logger LOGGER = LoggerFactory.getLogger("mc-bot-client/plugins");
    private static final PluginHost INSTANCE = new PluginHost();
    static final String FAILED_CHANNEL = "mcbot:failed";
    // The manager may have to download them first: ten megabytes from Mojang.
    private static final long MAPPINGS_TIMEOUT_SECONDS = 120;

    public static PluginHost get() {
        return INSTANCE;
    }

    private final Map<String, Loaded> plugins = new LinkedHashMap<>();
    // Running plugins in load order, rebuilt on every change so a hook can unload one mid-pass.
    private Loaded[] running = new Loaded[0];
    private final ExecutorService compiler = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "mcbot-plugin-compiler");
        t.setDaemon(true);
        return t;
    });
    private volatile PipeConnection connection;
    private final Map<String, CompletableFuture<Plugin.MojangMappingsResponse>> mappingRequests = new ConcurrentHashMap<>();
    private boolean installed;
    private boolean bypassSend;
    private boolean bypassReceive;

    private PluginHost() {}

    /** Ticks of its own, so plugins keep running while the manager is away. */
    public void install() {
        if (installed) {
            return;
        }
        installed = true;
        ClientTickEvents.START_CLIENT_TICK.register(client -> {
            for (Loaded p : running) {
                for (Runnable hook : p.tickStart) {
                    if (!p.call(hook::run)) break;
                }
            }
        });
        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            for (Loaded p : running) {
                for (Runnable hook : p.tickEnd) {
                    if (!p.call(hook::run)) break;
                }
            }
        });
    }

    public void attach(PipeConnection connection) {
        this.connection = connection;
    }

    /**
     * Mojang's mappings for {@code version}, from the manager, which downloads them once per
     * machine. Blocks, so never on the game thread: the answer is handled there.
     */
    byte[] mojangMappings(String version) throws IOException {
        String id = UUID.randomUUID().toString();
        CompletableFuture<Plugin.MojangMappingsResponse> answer = new CompletableFuture<>();
        mappingRequests.put(id, answer);
        try {
            PipeConnection c = connection;
            if (c == null || !c.isConnected()) {
                throw new IOException("no manager to ask for Mojang's mappings");
            }
            send(Protocol.ClientToManagerMessage.newBuilder().setMojangMappingsRequest(
                Plugin.MojangMappingsRequest.newBuilder().setRequestId(id).setVersion(version)));
            Plugin.MojangMappingsResponse response = answer.get(MAPPINGS_TIMEOUT_SECONDS, TimeUnit.SECONDS);
            if (!response.getError().isEmpty()) {
                throw new IOException(response.getError());
            }
            return inflate(response.getMappings().toByteArray());
        } catch (InterruptedException | ExecutionException | TimeoutException e) {
            throw new IOException("no answer from the manager about Mojang's mappings: " + e);
        } finally {
            mappingRequests.remove(id);
        }
    }

    public void mappings(Plugin.MojangMappingsResponse response) {
        CompletableFuture<Plugin.MojangMappingsResponse> answer = mappingRequests.get(response.getRequestId());
        if (answer != null) {
            answer.complete(response);
        }
    }

    /** Qt's qCompress: the length, four bytes big-endian, then a zlib stream. */
    private static byte[] inflate(byte[] packed) throws IOException {
        if (packed.length < 4) {
            throw new IOException("the manager sent no mappings");
        }
        int size = ((packed[0] & 0xff) << 24) | ((packed[1] & 0xff) << 16) | ((packed[2] & 0xff) << 8) | (packed[3] & 0xff);
        if (size < 0 || size > (256 << 20)) {
            throw new IOException("the manager's mappings claim " + size + " bytes");
        }
        Inflater inflater = new Inflater();
        try {
            inflater.setInput(packed, 4, packed.length - 4);
            byte[] out = new byte[size];
            int n = 0;
            while (n < size && !inflater.finished()) {
                int got = inflater.inflate(out, n, size - n);
                if (got == 0 && (inflater.needsInput() || inflater.needsDictionary())) {
                    throw new IOException("the manager's mappings end early");
                }
                n += got;
            }
            return out;
        } catch (DataFormatException e) {
            throw new IOException("the manager's mappings do not inflate: " + e.getMessage());
        } finally {
            inflater.end();
        }
    }

    // ---- from the manager (game thread)

    public void load(String requestId, Plugin.LoadPluginCommand command) {
        List<PluginCompiler.Source> sources = new ArrayList<>();
        for (Plugin.PluginSource s : command.getSourcesList()) {
            sources.add(new PluginCompiler.Source(s.getPath(), s.getCode()));
        }
        String name = command.getName();
        compiler.execute(() -> {
            PluginCompiler.Result result = PluginCompiler.compile(sources);
            Minecraft.getInstance().execute(() -> finishLoad(requestId, name, command.getMainClass(), result));
        });
    }

    private void finishLoad(String requestId, String name, String mainClass, PluginCompiler.Result result) {
        Plugin.PluginLoadResult.Builder reply = Plugin.PluginLoadResult.newBuilder()
            .setRequestId(requestId)
            .setName(name);
        for (PluginCompiler.Diag d : result.diagnostics()) {
            reply.addDiagnostics(Plugin.PluginDiagnostic.newBuilder()
                .setKind(d.kind()).setPath(d.path()).setLine(d.line()).setColumn(d.column()).setMessage(d.message()));
        }
        if (!result.ok()) {
            send(Protocol.ClientToManagerMessage.newBuilder().setPluginLoadResult(
                reply.setOk(false).setError(result.error() == null ? "" : result.error())));
            return;
        }
        ClientPlugin plugin;
        PluginClassLoader loader = new PluginClassLoader(name, result.classes(), PluginHost.class.getClassLoader());
        try {
            Object instance = loader.loadClass(mainClass).getDeclaredConstructor().newInstance();
            if (!(instance instanceof ClientPlugin p)) {
                throw new IllegalArgumentException(mainClass + " does not implement ClientPlugin");
            }
            plugin = p;
        } catch (Throwable t) {
            send(Protocol.ClientToManagerMessage.newBuilder().setPluginLoadResult(
                reply.setOk(false).setError("could not create " + mainClass + ": " + t)));
            return;
        }
        unload(name);
        Loaded loaded = new Loaded(name, plugin, loader);
        plugins.put(name, loaded);
        rebuild();
        try {
            plugin.onLoad(loaded);
        } catch (Throwable t) {
            fail(loaded, t);
            send(Protocol.ClientToManagerMessage.newBuilder().setPluginLoadResult(
                reply.setOk(false).setError("onLoad threw: " + t)));
            return;
        }
        LOGGER.info("Loaded plugin {} ({} classes)", name, result.classes().size());
        send(Protocol.ClientToManagerMessage.newBuilder().setPluginLoadResult(reply.setOk(true)));
    }

    public void unload(String name) {
        Loaded p = plugins.remove(name);
        if (p == null) {
            return;
        }
        boolean wasRunning = p.alive;
        p.alive = false;
        rebuild();
        if (wasRunning) {
            try {
                p.plugin.onUnload();
            } catch (Throwable t) {
                LOGGER.warn("Plugin {} threw while unloading", name, t);
            }
        }
        LOGGER.info("Unloaded plugin {}", name);
    }

    public void list(String requestId) {
        Plugin.PluginListResponse.Builder reply = Plugin.PluginListResponse.newBuilder().setRequestId(requestId);
        for (Loaded p : plugins.values()) {
            reply.addPlugins(Plugin.PluginInfo.newBuilder()
                .setName(p.name)
                .setState(p.alive ? "running" : "failed")
                .setError(p.error == null ? "" : p.error));
        }
        send(Protocol.ClientToManagerMessage.newBuilder().setPluginListResponse(reply));
    }

    public void message(Plugin.PluginMessage message) {
        Loaded p = plugins.get(message.getPlugin());
        PluginContext.MessageHandler h = p != null && p.alive ? p.handlers.get(message.getChannel()) : null;
        String[] reply = {null};
        if (h == null) {
            LOGGER.warn("No running plugin {} with a handler on {}", message.getPlugin(), message.getChannel());
        } else {
            p.call(() -> reply[0] = h.handle(message.getPayload()));
        }
        if (!message.getRequestId().isEmpty()) {
            sendMessage(message.getPlugin(), message.getChannel(), reply[0] == null ? "null" : reply[0],
                message.getRequestId());
        }
    }

    // ---- from the game's hooks

    public Vec3 move(LocalPlayer player, Vec3 movement) {
        for (Loaded p : running) {
            for (PluginContext.MoveHook hook : p.moves) {
                Vec3[] step = {movement};
                if (!p.call(() -> step[0] = hook.modify(player, step[0]))) break;
                if (step[0] != null) movement = step[0];
            }
        }
        return movement;
    }

    public void beforeMovePacket() {
        for (Loaded p : running) {
            for (Runnable hook : p.beforeMovePacket) {
                if (!p.call(hook::run)) break;
            }
        }
    }

    /** False drops the packet. Only the game thread's packets are shown, and never a plugin's own. */
    public boolean allowReceive(Packet<?> packet) {
        return bypassReceive || allow(packet, p -> p.received);
    }

    /** False drops the packet. Only the game thread's packets are shown, and never a plugin's own. */
    public boolean allowSend(Packet<?> packet) {
        return bypassSend || allow(packet, p -> p.sent);
    }

    private boolean allow(Packet<?> packet, Function<Loaded, List<Predicate<Packet<?>>>> hooks) {
        if (running.length == 0 || !Minecraft.getInstance().isSameThread()) {
            return true;
        }
        for (Loaded p : running) {
            for (Predicate<Packet<?>> hook : hooks.apply(p)) {
                boolean[] keep = {true};
                if (!p.call(() -> keep[0] = hook.test(packet))) break;
                if (!keep[0]) return false;
            }
        }
        return true;
    }

    // ----

    private void rebuild() {
        List<Loaded> alive = new ArrayList<>();
        for (Loaded p : plugins.values()) {
            if (p.alive) alive.add(p);
        }
        running = alive.toArray(new Loaded[0]);
    }

    private void fail(Loaded p, Throwable t) {
        if (!p.alive) {
            return;
        }
        p.alive = false;
        p.error = t.toString();
        rebuild();
        LOGGER.error("Plugin {} failed and is unloaded", p.name, t);
        try {
            p.plugin.onUnload();
        } catch (Throwable ignored) {
            // Already failing.
        }
        JsonObject payload = new JsonObject();
        payload.addProperty("error", p.error);
        sendMessage(p.name, FAILED_CHANNEL, payload.toString(), "");
    }

    private void sendMessage(String plugin, String channel, String json, String replyTo) {
        send(Protocol.ClientToManagerMessage.newBuilder().setPluginMessage(Plugin.PluginMessage.newBuilder()
            .setPlugin(plugin)
            .setChannel(channel)
            .setPayload(json)
            .setReplyTo(replyTo)));
    }

    private void send(Protocol.ClientToManagerMessage.Builder message) {
        PipeConnection c = connection;
        if (c == null || !c.isConnected()) {
            return;
        }
        c.sendMessage(message
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .build());
    }

    private interface Call {
        void run() throws Exception;
    }

    /** One plugin as loaded: its hooks, and the context it was handed. */
    private final class Loaded implements PluginContext {
        final String name;
        final ClientPlugin plugin;
        final PluginClassLoader loader;
        final Logger logger;
        boolean alive = true;
        String error;
        final List<Runnable> tickStart = new ArrayList<>();
        final List<Runnable> tickEnd = new ArrayList<>();
        final List<MoveHook> moves = new ArrayList<>();
        final List<Runnable> beforeMovePacket = new ArrayList<>();
        final List<Predicate<Packet<?>>> received = new ArrayList<>();
        final List<Predicate<Packet<?>>> sent = new ArrayList<>();
        final Map<String, MessageHandler> handlers = new HashMap<>();

        Loaded(String name, ClientPlugin plugin, PluginClassLoader loader) {
            this.name = name;
            this.plugin = plugin;
            this.loader = loader;
            this.logger = LoggerFactory.getLogger("mc-bot-client/plugin/" + name);
        }

        /** Runs a hook; a throw fails the plugin. False once it is not running. */
        boolean call(Call hook) {
            if (!alive) {
                return false;
            }
            try {
                hook.run();
                return alive;
            } catch (Throwable t) {
                fail(this, t);
                return false;
            }
        }

        @Override
        public String name() {
            return name;
        }

        @Override
        public Minecraft client() {
            return Minecraft.getInstance();
        }

        @Override
        public Logger logger() {
            return logger;
        }

        @Override
        public void onTickStart(Runnable hook) {
            tickStart.add(hook);
        }

        @Override
        public void onTickEnd(Runnable hook) {
            tickEnd.add(hook);
        }

        @Override
        public void onMove(MoveHook hook) {
            moves.add(hook);
        }

        @Override
        public void beforeMovePacket(Runnable hook) {
            beforeMovePacket.add(hook);
        }

        @Override
        public void onPacketReceived(Predicate<Packet<?>> hook) {
            received.add(hook);
        }

        @Override
        public void onPacketSent(Predicate<Packet<?>> hook) {
            sent.add(hook);
        }

        @Override
        public void sendPacket(Packet<?> packet) {
            ClientPacketListener listener = Minecraft.getInstance().getConnection();
            if (listener == null) {
                return;
            }
            bypassSend = true;
            try {
                listener.send(packet);
            } finally {
                bypassSend = false;
            }
        }

        @Override
        @SuppressWarnings("unchecked")
        public void receivePacket(Packet<?> packet) {
            Minecraft client = Minecraft.getInstance();
            if (!client.isSameThread()) {
                throw new IllegalStateException("receivePacket is for the game thread");
            }
            ClientPacketListener listener = client.getConnection();
            if (listener == null || !listener.shouldHandleMessage(packet)) {
                return;
            }
            // Saved, not cleared: handling one can send a packet, whose hook can receive another.
            boolean outer = bypassReceive;
            bypassReceive = true;
            try {
                ((Packet<ClientPacketListener>) packet).handle(listener);
            } finally {
                bypassReceive = outer;
            }
        }

        @Override
        public void onMessage(String channel, MessageHandler handler) {
            handlers.put(channel, handler);
        }

        @Override
        public void sendMessage(String channel, String json) {
            PluginHost.this.sendMessage(name, channel, json, "");
        }
    }
}
