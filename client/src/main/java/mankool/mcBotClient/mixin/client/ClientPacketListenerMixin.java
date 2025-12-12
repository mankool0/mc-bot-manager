package mankool.mcBotClient.mixin.client;

import mankool.mcBotClient.handler.outbound.WorldOutbound;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.core.BlockPos;
import net.minecraft.core.SectionPos;
import net.minecraft.network.protocol.game.*;
import net.minecraft.world.level.block.state.BlockState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.util.ArrayList;
import java.util.List;

/**
 * Mixin into ClientPacketListener to intercept chunk and block update packets.
 * This allows us to send world data to the manager as it's received from the server.
 */
@Mixin(ClientPacketListener.class)
public class ClientPacketListenerMixin {

    @Inject(method = "handleLevelChunkWithLight", at = @At("TAIL"))
    private void onChunkData(ClientboundLevelChunkWithLightPacket packet, CallbackInfo ci) {
        WorldOutbound handler = WorldOutbound.getInstance();
        if (handler != null) {
            handler.onChunkLoaded(packet.getX(), packet.getZ());
        }
    }

    @Inject(method = "handleBlockUpdate", at = @At("TAIL"))
    private void onBlockUpdate(ClientboundBlockUpdatePacket packet, CallbackInfo ci) {
        WorldOutbound handler = WorldOutbound.getInstance();
        if (handler != null) {
            handler.onBlockUpdate(packet.getPos(), packet.getBlockState());
        }
    }

    @Inject(method = "handleChunkBlocksUpdate", at = @At("TAIL"))
    private void onChunkBlocksUpdate(ClientboundSectionBlocksUpdatePacket packet, CallbackInfo ci) {
        WorldOutbound handler = WorldOutbound.getInstance();
        if (handler != null) {
            List<BlockPos> positions = new ArrayList<>();
            List<BlockState> states = new ArrayList<>();

            ClientboundSectionBlocksUpdatePacketAccessor accessor = (ClientboundSectionBlocksUpdatePacketAccessor) packet;
            SectionPos sectionPos = accessor.getSectionPos();
            short[] positions_array = accessor.getPositions();
            BlockState[] states_array = accessor.getStates();

            for (int i = 0; i < positions_array.length; i++) {
                short packedPos = positions_array[i];
                BlockState state = states_array[i];

                // Calculate block position from section position
                // relativeToBlockPos handles unpacking the position internally
                BlockPos pos = sectionPos.relativeToBlockPos(packedPos);
                positions.add(pos);
                states.add(state);
            }

            if (!positions.isEmpty()) {
                handler.onMultiBlockUpdate(positions, states);
            }
        }
    }

    @Inject(method = "handleForgetLevelChunk", at = @At("TAIL"))
    private void onChunkUnload(ClientboundForgetLevelChunkPacket packet, CallbackInfo ci) {
        WorldOutbound handler = WorldOutbound.getInstance();
        if (handler != null) {
            handler.onChunkUnloaded(packet.pos().x, packet.pos().z);
        }
    }
}