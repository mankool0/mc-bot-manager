package mankool.mcBotClient.integration.meteor;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import meteordevelopment.meteorclient.settings.PacketListSetting;
import meteordevelopment.meteorclient.utils.network.PacketUtils;
import net.minecraft.network.protocol.Packet;

// Version-split like the core VersionCompat: these use Meteor/Minecraft APIs that differ per MC version.
//
// Packet names are each generation's own vocabulary and must be round-tripped unchanged:
// old Meteor uses its hardcoded (Yarn) class names like "HandSwingC2SPacket", new Meteor uses
// PacketType.toString() like "serverbound/minecraft:swing". There is no cross-version translation.
public class MeteorVersionCompat {

    private static final boolean OLD_PACKET_API = detectOldPacketApi();

    private static boolean detectOldPacketApi() {
        try {
            PacketUtils.class.getMethod("getName", Class.class);
            return true;
        } catch (NoSuchMethodException e) {
            return false;
        }
    }

    @SuppressWarnings("unchecked")
    private static String packetName(Object entry) {
        if (entry instanceof Class<?> cls) {
            return PacketUtils.getName((Class<? extends Packet<?>>) cls);
        }
        return entry.toString();
    }

    public static List<String> getPacketListPossibleValues(PacketListSetting setting) {
        if (!OLD_PACKET_API) {
            // New Meteor's override already applies the setting's flow filter.
            return setting.getSuggestions();
        }
        // Old Meteor's getSuggestions() ignores the filter, so enumerate and filter manually.
        List<String> names = new ArrayList<>();
        for (Class<? extends Packet<?>> packet : PacketUtils.getC2SPackets()) {
            if (setting.filter == null || setting.filter.test(packet)) {
                names.add(PacketUtils.getName(packet));
            }
        }
        for (Class<? extends Packet<?>> packet : PacketUtils.getS2CPackets()) {
            if (setting.filter == null || setting.filter.test(packet)) {
                names.add(PacketUtils.getName(packet));
            }
        }
        return names;
    }

    public static void setPacketListValue(PacketListSetting setting, List<String> names) {
        // Setting.parse is identical on both generations; parseImpl silently drops names the
        // running Meteor doesn't recognize and applies the flow filter.
        setting.parse(String.join(",", names));
    }

    public static List<String> getPacketListValues(PacketListSetting setting) {
        // Raw view: the element type is Class on old Meteor, PacketType on new Meteor.
        Set<?> value = setting.get();
        List<String> names = new ArrayList<>(value.size());
        for (Object entry : value) {
            String name = packetName(entry);
            if (name != null) {
                names.add(name);
            }
        }
        return names;
    }
}
