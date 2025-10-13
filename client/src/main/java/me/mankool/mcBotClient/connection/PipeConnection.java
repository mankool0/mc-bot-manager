package me.mankool.mcBotClient.connection;

import com.google.protobuf.InvalidProtocolBufferException;
import com.sun.jna.platform.win32.Kernel32;
import com.sun.jna.platform.win32.WinBase;
import com.sun.jna.platform.win32.WinNT;
import com.sun.jna.ptr.IntByReference;
import mankool.mcbot.protocol.Connection;
import mankool.mcbot.protocol.Protocol;
import org.newsclub.net.unix.AFUNIXSocket;
import org.newsclub.net.unix.AFUNIXSocketAddress;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicBoolean;

public class PipeConnection {
    private static final String PIPE_NAME = "minecraft_manager";
    private static final String WINDOWS_PIPE_PATH = "\\\\.\\pipe\\" + PIPE_NAME;
    private static final String UNIX_SOCKET_PATH = getUnixSocketPath();

    private static String getUnixSocketPath() {
        // Use XDG_RUNTIME_DIR if available (better for sockets, works with flatpak)
        String xdgRuntime = System.getenv("XDG_RUNTIME_DIR");
        if (xdgRuntime != null && !xdgRuntime.isEmpty()) {
            return xdgRuntime + "/" + PIPE_NAME;
        }
        // Fallback to /tmp if XDG_RUNTIME_DIR is not set
        return "/tmp/" + PIPE_NAME;
    }

    private final BlockingQueue<Protocol.ClientToManagerMessage> sendQueue = new LinkedBlockingQueue<>();
    private final BlockingQueue<Protocol.ManagerToClientMessage> receiveQueue = new LinkedBlockingQueue<>();
    private final AtomicBoolean connected = new AtomicBoolean(false);
    private final AtomicBoolean running = new AtomicBoolean(false);

    private Thread sendThread;
    private Thread receiveThread;
    private OutputStream outputStream;
    private InputStream inputStream;
    private Object connection; // WinNT.HANDLE for Windows, AFUNIXSocket for Unix

    private final String clientId;

    public PipeConnection(String clientId) {
        this.clientId = clientId;
    }

    public boolean connect() {
        if (connected.get()) {
            return true;
        }

        String socketPath = System.getProperty("os.name").toLowerCase().contains("win") ? WINDOWS_PIPE_PATH : UNIX_SOCKET_PATH;
        System.out.println("Attempting to connect to manager pipe at: " + socketPath);

        try {
            if (System.getProperty("os.name").toLowerCase().contains("win")) {
                return connectWindows();
            } else {
                return connectUnix();
            }
        } catch (Exception e) {
            System.err.println("Failed to connect to pipe at " + socketPath);
            System.err.println("Error: " + e.getMessage());
            System.err.println("Make sure the manager is running and the socket file exists");
            return false;
        }
    }

    private boolean connectWindows() throws IOException {
        // Open the named pipe
        WinNT.HANDLE pipeHandle = Kernel32.INSTANCE.CreateFile(
            WINDOWS_PIPE_PATH,
            WinNT.GENERIC_READ | WinNT.GENERIC_WRITE,
            0, // No sharing
            null, // Default security
            WinNT.OPEN_EXISTING,
            0, // Default attributes
            null // No template
        );

        if (pipeHandle == WinNT.INVALID_HANDLE_VALUE) {
            int error = Kernel32.INSTANCE.GetLastError();
            throw new IOException("Failed to open pipe: " + WINDOWS_PIPE_PATH + ", error: " + error);
        }

        // Set pipe mode to message mode
        IntByReference lpMode = new IntByReference(WinBase.PIPE_READMODE_MESSAGE);
        boolean success = Kernel32.INSTANCE.SetNamedPipeHandleState(
            pipeHandle,
            lpMode,
            null,
            null
        );

        if (!success) {
            Kernel32.INSTANCE.CloseHandle(pipeHandle);
            throw new IOException("Failed to set pipe mode");
        }

        this.connection = pipeHandle;
        connected.set(true);
        startThreads();
        return true;
    }

    private boolean connectUnix() throws IOException {
        AFUNIXSocket socket = AFUNIXSocket.newInstance();
        socket.connect(AFUNIXSocketAddress.of(new File(UNIX_SOCKET_PATH)));

        this.connection = socket;
        this.outputStream = socket.getOutputStream();
        this.inputStream = socket.getInputStream();

        connected.set(true);
        startThreads();
        return true;
    }

    private void startThreads() {
        running.set(true);

        // Start send thread
        sendThread = new Thread(this::sendLoop, "PipeConnection-Send");
        sendThread.setDaemon(true);
        sendThread.start();

        // Start receive thread
        receiveThread = new Thread(this::receiveLoop, "PipeConnection-Receive");
        receiveThread.setDaemon(true);
        receiveThread.start();
    }

    private void sendLoop() {
        while (running.get()) {
            try {
                Protocol.ClientToManagerMessage message = sendQueue.take();
                sendMessageInternal(message);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            } catch (Exception e) {
                System.err.println("Error sending message: " + e.getMessage());
                e.printStackTrace();
            }
        }
    }

    private void sendMessageInternal(Protocol.ClientToManagerMessage message) throws IOException {
        byte[] data = message.toByteArray();

        if (System.getProperty("os.name").toLowerCase().contains("win")) {
            sendMessageWindows(data);
        } else {
            sendMessageUnix(data);
        }
    }

    private void sendMessageWindows(byte[] data) throws IOException {
        WinNT.HANDLE pipeHandle = (WinNT.HANDLE) connection;
        IntByReference bytesWritten = new IntByReference();

        boolean success = Kernel32.INSTANCE.WriteFile(
            pipeHandle,
            data,
            data.length,
            bytesWritten,
            null
        );

        if (!success) {
            throw new IOException("Failed to write to pipe");
        }
    }

    private void sendMessageUnix(byte[] data) throws IOException {
        ByteBuffer messageBuffer = ByteBuffer.allocate(4 + data.length);
        messageBuffer.order(ByteOrder.LITTLE_ENDIAN);
        messageBuffer.putInt(data.length);
        messageBuffer.put(data);

        outputStream.write(messageBuffer.array());
        outputStream.flush();
    }

    private void receiveLoop() {
        byte[] buffer = new byte[65536]; // 64KB buffer

        while (running.get()) {
            try {
                Protocol.ManagerToClientMessage message = receiveMessage(buffer);
                if (message != null) {
                    receiveQueue.offer(message);
                }
            } catch (Exception e) {
                System.err.println("Connection lost: " + e.getMessage());
                disconnect();
                break;
            }
        }
    }

    private Protocol.ManagerToClientMessage receiveMessage(byte[] buffer) throws IOException {
        int bytesRead;

        if (System.getProperty("os.name").toLowerCase().contains("win")) {
            bytesRead = receiveMessageWindows(buffer);
        } else {
            bytesRead = receiveMessageUnix(buffer);
        }

        if (bytesRead < 0) {
            throw new IOException("Connection closed");
        }

        if (bytesRead == 0) {
            return null;
        }

        try {
            return Protocol.ManagerToClientMessage.parseFrom(ByteBuffer.wrap(buffer, 0, bytesRead));
        } catch (InvalidProtocolBufferException e) {
            System.err.println("Failed to parse protobuf message: " + e.getMessage());
            return null;
        }
    }

    private int receiveMessageWindows(byte[] buffer) throws IOException {
        WinNT.HANDLE pipeHandle = (WinNT.HANDLE) connection;
        IntByReference bytesRead = new IntByReference();

        boolean success = Kernel32.INSTANCE.ReadFile(
            pipeHandle,
            buffer,
            buffer.length,
            bytesRead,
            null
        );

        if (!success) {
            int error = Kernel32.INSTANCE.GetLastError();
            if (error == 109) { // ERROR_BROKEN_PIPE
                disconnect();
                return -1;
            }
            throw new IOException("Failed to read from pipe, error: " + error);
        }

        return bytesRead.getValue();
    }

    private int receiveMessageUnix(byte[] buffer) throws IOException {
        // Read length prefix (4 bytes, little-endian)
        byte[] lengthBytes = new byte[4];
        int bytesRead = inputStream.read(lengthBytes);
        if (bytesRead != 4) {
            return -1;
        }

        ByteBuffer lengthBuffer = ByteBuffer.wrap(lengthBytes);
        lengthBuffer.order(ByteOrder.LITTLE_ENDIAN);
        int messageLength = lengthBuffer.getInt();

        if (messageLength <= 0 || messageLength > buffer.length) {
            throw new IOException("Invalid message length: " + messageLength);
        }

        int totalRead = 0;
        while (totalRead < messageLength) {
            int read = inputStream.read(buffer, totalRead, messageLength - totalRead);
            if (read < 0) {
                return -1;
            }
            totalRead += read;
        }

        return totalRead;
    }

    public void disconnect() {
        if (!connected.getAndSet(false)) {
            return;
        }

        running.set(false);

        if (sendThread != null) {
            sendThread.interrupt();
        }
        if (receiveThread != null) {
            receiveThread.interrupt();
        }

        try {
            if (System.getProperty("os.name").toLowerCase().contains("win")) {
                if (connection != null) {
                    Kernel32.INSTANCE.CloseHandle((WinNT.HANDLE) connection);
                }
            } else {
                if (connection != null) {
                    ((AFUNIXSocket) connection).close();
                }
            }
        } catch (Exception e) {
            System.err.println("Error closing connection: " + e.getMessage());
        }

        connection = null;
        outputStream = null;
        inputStream = null;
    }

    public boolean isConnected() {
        return connected.get();
    }

    public void sendMessage(Protocol.ClientToManagerMessage message) {
        if (!connected.get()) {
            throw new IllegalStateException("Not connected to pipe");
        }
        sendQueue.offer(message);
    }

    public Protocol.ManagerToClientMessage receiveMessage() {
        return receiveQueue.poll();
    }

    public Protocol.ManagerToClientMessage receiveManagerMessageBlocking() throws InterruptedException {
        return receiveQueue.take();
    }

    public boolean hasMessages() {
        return !receiveQueue.isEmpty();
    }

    public void sendHeartbeat() {
        if (!connected.get()) {
            throw new IllegalStateException("Not connected to pipe");
        }

        Runtime runtime = Runtime.getRuntime();
        long currentMemory = runtime.totalMemory() - runtime.freeMemory();

        Connection.HeartbeatMessage heartbeat = Connection.HeartbeatMessage.newBuilder()
            .setCurrentMemory(currentMemory)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(java.util.UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setHeartbeat(heartbeat)
            .build();

        sendMessage(message);
    }
}