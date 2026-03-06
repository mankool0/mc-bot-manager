package mankool.mcBotClient.connection;

import com.google.protobuf.InvalidProtocolBufferException;
import mankool.mcbot.protocol.Connection;
import mankool.mcbot.protocol.Protocol;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.Closeable;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.net.UnixDomainSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.Channels;
import java.nio.channels.SocketChannel;
import java.nio.file.Path;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicBoolean;

public class PipeConnection {
    private static final Logger LOGGER = LoggerFactory.getLogger(PipeConnection.class);
    private static final String PIPE_NAME = "minecraft_manager";
    private static final boolean IS_WINDOWS = System.getProperty("os.name").toLowerCase().contains("win");
    private static final String WINDOWS_PIPE_PATH = "\\\\.\\pipe\\" + PIPE_NAME;
    private static final String UNIX_SOCKET_PATH = getUnixSocketPath();

    private static String getUnixSocketPath() {
        String xdgRuntime = System.getenv("XDG_RUNTIME_DIR");
        if (xdgRuntime != null && !xdgRuntime.isEmpty()) {
            return xdgRuntime + "/" + PIPE_NAME;
        }
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
    private Closeable connection;

    private final String clientId;

    public PipeConnection(String clientId) {
        this.clientId = clientId;
    }

    public boolean connect() {
        if (connected.get()) {
            return true;
        }

        String path = IS_WINDOWS ? WINDOWS_PIPE_PATH : UNIX_SOCKET_PATH;
        LOGGER.info("Attempting to connect to manager at: {}", path);

        try {
            if (IS_WINDOWS) {
                RandomAccessFile pipe = new RandomAccessFile(WINDOWS_PIPE_PATH, "rw");
                this.connection = pipe;
                this.inputStream = new FileInputStream(pipe.getFD());
                this.outputStream = new FileOutputStream(pipe.getFD());
            } else {
                SocketChannel channel = SocketChannel.open(UnixDomainSocketAddress.of(Path.of(UNIX_SOCKET_PATH)));
                this.connection = channel;
                this.inputStream = Channels.newInputStream(channel);
                this.outputStream = Channels.newOutputStream(channel);
            }
            connected.set(true);
            startThreads();
            return true;
        } catch (Exception e) {
            LOGGER.error("Failed to connect to {}: {}. Make sure the manager is running.", path, e.getMessage());
            return false;
        }
    }

    private void startThreads() {
        running.set(true);

        sendThread = new Thread(this::sendLoop, "PipeConnection-Send");
        sendThread.setDaemon(true);
        sendThread.start();

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
                LOGGER.error("Error sending message: {}", e.getMessage(), e);
            }
        }
    }

    private void sendMessageInternal(Protocol.ClientToManagerMessage message) throws IOException {
        byte[] data = message.toByteArray();
        ByteBuffer buffer = ByteBuffer.allocate(4 + data.length);
        buffer.order(ByteOrder.LITTLE_ENDIAN);
        buffer.putInt(data.length);
        buffer.put(data);
        outputStream.write(buffer.array());
        outputStream.flush();
    }

    private void receiveLoop() {
        byte[] buffer = new byte[65536];
        while (running.get()) {
            try {
                Protocol.ManagerToClientMessage message = receiveMessage(buffer);
                if (message != null) {
                    receiveQueue.offer(message);
                }
            } catch (Exception e) {
                LOGGER.error("Connection lost: {}", e.getMessage());
                disconnect();
                break;
            }
        }
    }

    private Protocol.ManagerToClientMessage receiveMessage(byte[] buffer) throws IOException {
        byte[] lengthBytes = new byte[4];
        int bytesRead = inputStream.read(lengthBytes);
        if (bytesRead != 4) {
            return null;
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
                return null;
            }
            totalRead += read;
        }

        try {
            return Protocol.ManagerToClientMessage.parseFrom(ByteBuffer.wrap(buffer, 0, totalRead));
        } catch (InvalidProtocolBufferException e) {
            LOGGER.error("Failed to parse protobuf message: {}", e.getMessage());
            return null;
        }
    }

    public void disconnect() {
        if (!connected.getAndSet(false)) {
            return;
        }

        running.set(false);

        if (sendThread != null) sendThread.interrupt();
        if (receiveThread != null) receiveThread.interrupt();

        try {
            if (connection != null) connection.close();
        } catch (Exception e) {
            LOGGER.error("Error closing connection: {}", e.getMessage());
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
