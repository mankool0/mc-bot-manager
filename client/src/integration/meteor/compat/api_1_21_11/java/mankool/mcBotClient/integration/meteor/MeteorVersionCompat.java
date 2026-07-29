package mankool.mcBotClient.integration.meteor;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;
import meteordevelopment.meteorclient.settings.PacketListSetting;
import meteordevelopment.meteorclient.utils.network.PacketUtils;
import net.minecraft.network.protocol.Packet;

// Version-split like the core VersionCompat: these use Meteor/Minecraft APIs that differ per MC version.
public class MeteorVersionCompat {

    public static List<String> getPacketListPossibleValues(PacketListSetting setting) {
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
        Set<Class<? extends Packet<?>>> packets = names.stream()
                .map(PacketUtils::getPacket)
                .filter(Objects::nonNull)
                .collect(Collectors.toSet());
        setting.set(packets);
    }

    public static List<String> getPacketListValues(PacketListSetting setting) {
        return setting.get().stream()
                .map(PacketUtils::getName)
                .collect(Collectors.toList());
    }
}
