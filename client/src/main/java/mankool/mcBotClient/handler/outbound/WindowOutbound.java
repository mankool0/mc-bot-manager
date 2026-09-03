package mankool.mcBotClient.handler.outbound;

import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.util.BotWindow;
import mankool.mcBotClient.util.VersionCompat;
import mankool.mcbot.protocol.Protocol;
import mankool.mcbot.protocol.WindowProto;
import net.minecraft.client.Minecraft;
import org.lwjgl.PointerBuffer;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWNativeWin32;
import org.lwjgl.glfw.GLFWVidMode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/**
 * Reports and changes the game window's placement on behalf of the manager.
 *
 * <p>Rectangles on the wire are the window's outer frame relative to a monitor's work area (see
 * {@code window.proto}); GLFW reports the client area in screen coordinates, so this converts with
 * {@code glfwGetWindowFrameSize}. Moving is platform-specific: on Win32 {@code glfwSetWindowPos}
 * places the client area, but on X11 it is a bare {@code XMoveWindow}, and the window manager puts
 * the frame's corner at the requested point (ICCCM north-west gravity), so no frame offset is added
 * there. Everything here runs on the render thread, which owns GLFW: inbound commands are
 * dispatched from the client tick and reports are scheduled onto it.
 *
 * <p>A report that follows a change is delayed a few ticks. On X11 a move is a ConfigureRequest
 * the window manager handles on its own schedule (and it owns {@code _NET_FRAME_EXTENTS} too), so
 * geometry read right after {@code glfwSetWindowPos} is still the old rect. Win32 applies moves
 * synchronously and Wayland refuses them, but the delay is unconditional, and it also covers
 * (un)minimize and the post-handshake report. It is a fixed wait, not a confirmation: a slow window
 * manager can still be reported mid-move. A bare {@code GetWindowStateCommand} replies immediately.
 *
 * <p>The window starts out hidden (see {@link BotWindow}) and is mapped by the first
 * {@code SetWindowCommand} carrying {@code visible}, which the manager sends in reply to the
 * post-handshake report together with the placement. Should that never arrive, the window is shown
 * where it is after {@link #SHOW_FALLBACK_TICKS}. X11 reports frame extents only for a mapped and
 * decorated window, so a placement applied while hidden sizes the client area as if the frame had
 * none and is applied once more after the show, which trims it to the cell; the position was
 * already right.
 */
public class WindowOutbound extends BaseOutbound {
    private static final Logger LOGGER = LoggerFactory.getLogger(WindowOutbound.class);

    /** Ticks between applying a change (or connecting) and reporting the resulting state. */
    private static final int REPORT_DELAY_TICKS = 5;
    /** Ticks after connecting before a still-hidden window is shown without a placement. */
    private static final int SHOW_FALLBACK_TICKS = 10 * 20;

    private final List<String> pendingRequestIds = new ArrayList<>();
    private int reportCountdown = -1;
    private int showFallbackCountdown = -1;
    /** Placement to redo once the window manager has decorated a freshly shown window. */
    private WindowProto.SetWindowCommand pendingGeometry;

    public WindowOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    @Override
    protected void onClientTick(Minecraft client) {
        if (showFallbackCountdown >= 0 && showFallbackCountdown-- == 0) {
            long handle = handle();
            if (handle != 0L && !BotWindow.isVisible(handle)) {
                LOGGER.warn("No window placement from the manager within {}s, showing the window where it is",
                    SHOW_FALLBACK_TICKS / 20);
                BotWindow.setVisible(handle, true);
            }
        }

        if (reportCountdown < 0) return;
        if (reportCountdown-- > 0) return;
        if (pendingGeometry != null) {
            WindowProto.SetWindowCommand cmd = pendingGeometry;
            pendingGeometry = null;
            long handle = handle();
            if (handle != 0L) {
                applyGeometry(handle, cmd, canMove());
            }
            reportCountdown = REPORT_DELAY_TICKS;
            return;
        }
        List<String> ids = new ArrayList<>(pendingRequestIds);
        pendingRequestIds.clear();
        for (String id : ids) {
            sendState(id);
        }
    }

    /** Queues a state report answering requestId (empty for an unsolicited report). */
    public void scheduleReport(String requestId) {
        // One reply per outstanding request, all taken once the window has settled.
        if (!pendingRequestIds.contains(requestId)) {
            pendingRequestIds.add(requestId);
        }
        reportCountdown = REPORT_DELAY_TICKS;
    }

    /** Starts the clock on showing a window the manager never places. Called once connected. */
    public void armShowFallback() {
        long handle = handle();
        if (handle != 0L && !BotWindow.isVisible(handle)) {
            showFallbackCountdown = SHOW_FALLBACK_TICKS;
        }
    }

    /** Manager -> Client: report without changing anything. */
    public void handleGetWindowState(String messageId) {
        sendState(messageId);
    }

    /** Manager -> Client: hide / move / resize / show / (un)minimize, in that order, then report. */
    public void handleSetWindow(String messageId, WindowProto.SetWindowCommand cmd) {
        long handle = handle();
        if (handle == 0L) {
            sendState(messageId);
            return;
        }
        boolean canMove = canMove();
        boolean fullscreen = GLFW.glfwGetWindowMonitor(handle) != 0L;

        if (cmd.hasVisible()) {
            // The manager has decided; the fallback must not overrule a deliberate hide later on.
            showFallbackCountdown = -1;
            if (!cmd.getVisible() && BotWindow.isVisible(handle)) {
                BotWindow.setVisible(handle, false);
            }
        }

        if (cmd.hasMinimized() && !cmd.getMinimized()
                && GLFW.glfwGetWindowAttrib(handle, GLFW.GLFW_ICONIFIED) == GLFW.GLFW_TRUE) {
            GLFW.glfwRestoreWindow(handle);
        }

        boolean wantsGeometry = cmd.hasX() || cmd.hasY() || cmd.hasWidth() || cmd.hasHeight() || !cmd.getMonitor().isEmpty();
        boolean extentsKnown = true;
        if (wantsGeometry && fullscreen) {
            LOGGER.warn("Ignoring window placement while fullscreen");
        } else if (wantsGeometry) {
            extentsKnown = applyGeometry(handle, cmd, canMove);
        }

        if (cmd.hasVisible() && cmd.getVisible() && !BotWindow.isVisible(handle)) {
            BotWindow.setVisible(handle, true);
            if (wantsGeometry && !fullscreen && !extentsKnown) {
                pendingGeometry = cmd;
            }
        }

        if (cmd.hasMinimized() && cmd.getMinimized()) {
            GLFW.glfwIconifyWindow(handle);
        }
        scheduleReport(messageId);
    }

    /** Applies the placement; returns whether frame extents were available (false while unmapped on X11). */
    private boolean applyGeometry(long handle, WindowProto.SetWindowCommand cmd, boolean canMove) {
        List<Monitor> monitors = monitors();
        Frame current = currentFrame(handle, canMove);
        Monitor target = null;
        if (!cmd.getMonitor().isEmpty()) {
            target = findMonitor(monitors, cmd.getMonitor());
            if (target == null) {
                LOGGER.warn("Unknown monitor '{}', keeping the current one", cmd.getMonitor());
            }
        }
        if (target == null) {
            target = monitorOf(monitors, current);
        }

        int[] left = new int[1], top = new int[1], right = new int[1], bottom = new int[1];
        GLFW.glfwGetWindowFrameSize(handle, left, top, right, bottom);
        boolean extentsKnown = left[0] + top[0] + right[0] + bottom[0] > 0;

        // Frame rect relative to the target work area. Unset position keeps the window's offset
        // within its current monitor's work area, so a bare monitor change carries it across.
        Monitor currentMonitor = monitorOf(monitors, current);
        int relX = cmd.hasX() ? cmd.getX() : current.x - (currentMonitor != null ? currentMonitor.workX : 0);
        int relY = cmd.hasY() ? cmd.getY() : current.y - (currentMonitor != null ? currentMonitor.workY : 0);
        int frameW = cmd.hasWidth() ? cmd.getWidth() : current.width;
        int frameH = cmd.hasHeight() ? cmd.getHeight() : current.height;

        if (cmd.hasWidth() || cmd.hasHeight()) {
            int clientW = Math.max(1, frameW - left[0] - right[0]);
            int clientH = Math.max(1, frameH - top[0] - bottom[0]);
            GLFW.glfwSetWindowSize(handle, clientW, clientH);
        }
        boolean moves = cmd.hasX() || cmd.hasY() || !cmd.getMonitor().isEmpty();
        if (moves) {
            if (!canMove) {
                LOGGER.warn("Window positioning is not available on this platform ({})", platformName());
                return extentsKnown;
            }
            boolean framePositioned = GLFW.glfwGetPlatform() == GLFW.GLFW_PLATFORM_X11;
            int absX = (target != null ? target.workX : 0) + relX + (framePositioned ? 0 : left[0]);
            int absY = (target != null ? target.workY : 0) + relY + (framePositioned ? 0 : top[0]);
            GLFW.glfwSetWindowPos(handle, absX, absY);
        }
        return extentsKnown;
    }

    /** Sends the current state. Runs on the render thread. */
    public void sendState(String requestId) {
        WindowProto.WindowStateResponse.Builder state = WindowProto.WindowStateResponse.newBuilder()
            .setRequestId(requestId)
            .setPlatform(platformName())
            .setCanMove(canMove());

        long handle = handle();
        List<Monitor> monitors = monitors();
        for (Monitor m : monitors) {
            state.addMonitors(WindowProto.MonitorInfo.newBuilder()
                .setName(m.name)
                .setPrimary(m.primary)
                .setX(m.x).setY(m.y).setWidth(m.width).setHeight(m.height)
                .setWorkX(m.workX).setWorkY(m.workY).setWorkWidth(m.workWidth).setWorkHeight(m.workHeight));
        }

        if (handle != 0L) {
            Frame frame = currentFrame(handle, canMove());
            Monitor on = monitorOf(monitors, frame);
            state.setMonitor(on != null ? on.name : "")
                .setX(frame.x - (on != null ? on.workX : 0))
                .setY(frame.y - (on != null ? on.workY : 0))
                .setWidth(frame.width)
                .setHeight(frame.height)
                .setMinimized(GLFW.glfwGetWindowAttrib(handle, GLFW.GLFW_ICONIFIED) == GLFW.GLFW_TRUE)
                .setFocused(GLFW.glfwGetWindowAttrib(handle, GLFW.GLFW_FOCUSED) == GLFW.GLFW_TRUE)
                .setVisible(BotWindow.isVisible(handle));
        }

        Protocol.ClientToManagerMessage msg = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setWindowState(state)
            .build();
        connection.sendMessage(msg);
    }

    private long handle() {
        var window = client.getWindow();
        return window != null ? VersionCompat.windowHandle(window) : 0L;
    }

    private static boolean canMove() {
        return GLFW.glfwGetPlatform() != GLFW.GLFW_PLATFORM_WAYLAND;
    }

    private static String platformName() {
        return switch (GLFW.glfwGetPlatform()) {
            case GLFW.GLFW_PLATFORM_WIN32 -> "win32";
            case GLFW.GLFW_PLATFORM_COCOA -> "cocoa";
            case GLFW.GLFW_PLATFORM_WAYLAND -> "wayland";
            case GLFW.GLFW_PLATFORM_X11 -> "x11";
            default -> "null";
        };
    }

    private record Monitor(String name, boolean primary, int x, int y, int width, int height,
                           int workX, int workY, int workWidth, int workHeight) {
        boolean contains(int px, int py) {
            return px >= x && py >= y && px < x + width && py < y + height;
        }
    }

    private record Frame(int x, int y, int width, int height) {}

    private static List<Monitor> monitors() {
        List<Monitor> result = new ArrayList<>();
        PointerBuffer handles = GLFW.glfwGetMonitors();
        if (handles == null) return result;
        long primary = GLFW.glfwGetPrimaryMonitor();
        int platform = GLFW.glfwGetPlatform();
        int[] x = new int[1], y = new int[1], w = new int[1], h = new int[1];
        for (int i = 0; i < handles.limit(); i++) {
            long m = handles.get(i);
            GLFW.glfwGetMonitorPos(m, x, y);
            int px = x[0], py = y[0];
            GLFWVidMode mode = GLFW.glfwGetVideoMode(m);
            int width = mode != null ? mode.width() : 0;
            int height = mode != null ? mode.height() : 0;
            GLFW.glfwGetMonitorWorkarea(m, x, y, w, h);
            result.add(new Monitor(monitorName(m, platform), m == primary, px, py, width, height,
                x[0], y[0], w[0], h[0]));
        }
        return result;
    }

    /**
     * GLFW's monitor name is the output name on Linux ("DP-1") but only the model string on
     * Windows, which repeats for identical monitors; the adapter name is the unique one there.
     */
    private static String monitorName(long monitor, int platform) {
        if (platform == GLFW.GLFW_PLATFORM_WIN32) {
            String adapter = GLFWNativeWin32.glfwGetWin32Adapter(monitor);
            if (adapter != null && !adapter.isEmpty()) return adapter;
        }
        String name = GLFW.glfwGetMonitorName(monitor);
        return name != null ? name : "";
    }

    private static Monitor findMonitor(List<Monitor> monitors, String name) {
        for (Monitor m : monitors) {
            if (m.name.equals(name)) return m;
        }
        return null;
    }

    /** The monitor containing the frame's centre, else the one containing its origin, else primary. */
    private static Monitor monitorOf(List<Monitor> monitors, Frame frame) {
        int cx = frame.x + frame.width / 2, cy = frame.y + frame.height / 2;
        for (Monitor m : monitors) {
            if (m.contains(cx, cy)) return m;
        }
        for (Monitor m : monitors) {
            if (m.contains(frame.x, frame.y)) return m;
        }
        for (Monitor m : monitors) {
            if (m.primary) return m;
        }
        return monitors.isEmpty() ? null : monitors.get(0);
    }

    /** Outer frame rect in screen coordinates. Position is unavailable on Wayland and read as 0,0. */
    private static Frame currentFrame(long handle, boolean canMove) {
        int[] a = new int[1], b = new int[1], c = new int[1], d = new int[1];
        int x = 0, y = 0;
        if (canMove) {
            GLFW.glfwGetWindowPos(handle, a, b);
            x = a[0];
            y = b[0];
        }
        GLFW.glfwGetWindowSize(handle, a, b);
        int w = a[0], h = b[0];
        GLFW.glfwGetWindowFrameSize(handle, a, b, c, d);
        return new Frame(x - a[0], y - b[0], w + a[0] + c[0], h + b[0] + d[0]);
    }
}
