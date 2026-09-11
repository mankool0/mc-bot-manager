package mankool.mcBotClient.integration.meteor;

import mankool.mcbot.protocol.Meteor.FriendsUpdate;
import mankool.mcbot.protocol.Meteor.ModifyFriendsCommand;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.handler.inbound.BaseInboundHandler;
import mankool.mcBotClient.util.VersionCompat;
import meteordevelopment.meteorclient.systems.friends.Friend;
import meteordevelopment.meteorclient.systems.friends.Friends;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.client.multiplayer.PlayerInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/**
 * Meteor's friends list, mirrored to the manager and editable from it. Meteor has no event for a
 * friend being added, so the list is read back once a second and sent whenever it differs from what
 * the manager was last told - which covers edits made in Meteor's own GUI as well as ours.
 */
public class MeteorFriendsHandler extends BaseInboundHandler {

    private static final Logger LOGGER = LoggerFactory.getLogger(MeteorFriendsHandler.class);

    private static final int READ_EVERY_TICKS = 20;

    private int tick;
    // What the manager has: null until the first send, so a fresh connection always gets one.
    private List<String> sent;

    public MeteorFriendsHandler(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    /** Called every client tick while this connection is up. */
    public void tick() {
        if (++tick % READ_EVERY_TICKS != 0) {
            return;
        }
        try {
            sendIfChanged();
        } catch (Exception e) {
            LOGGER.error("Failed to read Meteor friends", e);
        }
    }

    public void handleGetFriends(String messageId) {
        List<String> names = read();
        if (names == null) {
            sendFailure(messageId, "Meteor friends are not loaded yet");
            return;
        }
        send(names);
    }

    public void handleModifyFriends(ModifyFriendsCommand command) {
        Friends friends = Friends.get();
        if (friends == null) {
            LOGGER.warn("Cannot modify friends: Meteor friends are not loaded yet");
            return;
        }

        for (String name : command.getAddList()) {
            // add() takes the name it is given, so an existing friend under different casing would
            // become a second entry; Meteor's own lookups are case-insensitive, so ask it first.
            if (name.isEmpty() || friends.get(name) != null) {
                continue;
            }
            friends.add(friendFor(name));
        }
        for (String name : command.getRemoveList()) {
            Friend friend = friends.get(name);
            if (friend != null) {
                friends.remove(friend);
            }
        }

        sendIfChanged();
    }

    /** The friends Meteor holds, or null while its systems are still loading. */
    private List<String> read() {
        Friends friends = Friends.get();
        if (friends == null) {
            return null;
        }
        List<String> names = new ArrayList<>();
        for (Friend friend : friends) {
            names.add(friend.getName());
        }
        return names;
    }

    // Meteor's own /friends add takes the UUID off the tab list entry; a name that is not on it
    // still makes a usable friend, since every lookup Meteor does goes by name.
    private Friend friendFor(String name) {
        ClientPacketListener connection = client.getConnection();
        if (connection != null) {
            PlayerInfo info = connection.getPlayerInfo(name);
            if (info != null) {
                UUID id = VersionCompat.profileId(info.getProfile());
                return new Friend(VersionCompat.profileName(info.getProfile()), id);
            }
        }
        return new Friend(name);
    }

    private void sendIfChanged() {
        List<String> names = read();
        if (names != null && !names.equals(sent)) {
            send(names);
        }
    }

    private void send(List<String> names) {
        FriendsUpdate update = FriendsUpdate.newBuilder().addAllFriends(names).build();
        connection.sendMessage(Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setFriendsUpdate(update)
            .build());
        sent = names;
    }
}
