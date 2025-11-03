package mankool.mcBotClient.util;

import mankool.mcbot.protocol.Common;
import net.minecraft.world.phys.Vec3;

public class ProtoUtil {

    public static Common.Vec3d toProtoVec3d(Vec3 vec) {
        return Common.Vec3d.newBuilder()
            .setX(vec.x)
            .setY(vec.y)
            .setZ(vec.z)
            .build();
    }

    public static Common.Vec3d toProtoVec3d(double x, double y, double z) {
        return Common.Vec3d.newBuilder()
            .setX(x)
            .setY(y)
            .setZ(z)
            .build();
    }
}