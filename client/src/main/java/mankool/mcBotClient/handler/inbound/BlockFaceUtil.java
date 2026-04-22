package mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Common;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

class BlockFaceUtil {

    static Direction protoFaceToDirection(Common.BlockFace face) {
        return switch (face) {
            case FACE_DOWN  -> Direction.DOWN;
            case FACE_UP    -> Direction.UP;
            case FACE_NORTH -> Direction.NORTH;
            case FACE_SOUTH -> Direction.SOUTH;
            case FACE_WEST  -> Direction.WEST;
            case FACE_EAST  -> Direction.EAST;
            default         -> Direction.UP;
        };
    }

    // faceOrdinal: 1=DOWN,2=UP,3=NORTH,4=SOUTH,5=WEST,6=EAST (BlockFace proto enum)
    static Direction faceFromOrdinal(int faceOrdinal) {
        return switch (faceOrdinal) {
            case 1 -> Direction.DOWN;
            case 2 -> Direction.UP;
            case 3 -> Direction.NORTH;
            case 4 -> Direction.SOUTH;
            case 5 -> Direction.WEST;
            case 6 -> Direction.EAST;
            default -> null;
        };
    }

    static Vec3[] faceCandidates(BlockPos pos, Direction face, AABB aabb) {
        double bx = pos.getX(), by = pos.getY(), bz = pos.getZ();
        return switch (face) {
            case UP -> new Vec3[]{
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + aabb.maxY, bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.1), by + aabb.maxY, bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.9), by + aabb.maxY, bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + aabb.maxY, bz + lerp(aabb.minZ, aabb.maxZ, 0.1)),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + aabb.maxY, bz + lerp(aabb.minZ, aabb.maxZ, 0.9)),
            };
            case DOWN -> new Vec3[]{
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + aabb.minY, bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.1), by + aabb.minY, bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.9), by + aabb.minY, bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + aabb.minY, bz + lerp(aabb.minZ, aabb.maxZ, 0.1)),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + aabb.minY, bz + lerp(aabb.minZ, aabb.maxZ, 0.9)),
            };
            case NORTH -> new Vec3[]{
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + lerp(aabb.minY, aabb.maxY, 0.75), bz + aabb.minZ),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + lerp(aabb.minY, aabb.maxY, 0.25), bz + aabb.minZ),
            };
            case SOUTH -> new Vec3[]{
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + lerp(aabb.minY, aabb.maxY, 0.75), bz + aabb.maxZ),
                new Vec3(bx + lerp(aabb.minX, aabb.maxX, 0.5), by + lerp(aabb.minY, aabb.maxY, 0.25), bz + aabb.maxZ),
            };
            case WEST -> new Vec3[]{
                new Vec3(bx + aabb.minX, by + lerp(aabb.minY, aabb.maxY, 0.75), bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
                new Vec3(bx + aabb.minX, by + lerp(aabb.minY, aabb.maxY, 0.25), bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
            };
            case EAST -> new Vec3[]{
                new Vec3(bx + aabb.maxX, by + lerp(aabb.minY, aabb.maxY, 0.75), bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
                new Vec3(bx + aabb.maxX, by + lerp(aabb.minY, aabb.maxY, 0.25), bz + lerp(aabb.minZ, aabb.maxZ, 0.5)),
            };
        };
    }

    static Vec3 extendRay(Vec3 eye, Vec3 target) {
        Vec3 dir = target.subtract(eye).normalize();
        return eye.add(dir.scale(eye.distanceTo(target) + 0.5));
    }

    static void applyRotationToward(LocalPlayer player, Vec3 eyePos, Vec3 target) {
        double dx = target.x - eyePos.x, dy = target.y - eyePos.y, dz = target.z - eyePos.z;
        player.setYRot((float) Math.toDegrees(Math.atan2(-dx, dz)));
        player.setXRot((float) Math.toDegrees(-Math.atan2(dy, Math.sqrt(dx * dx + dz * dz))));
    }

    private static double lerp(double min, double max, double t) {
        return min + (max - min) * t;
    }
}
