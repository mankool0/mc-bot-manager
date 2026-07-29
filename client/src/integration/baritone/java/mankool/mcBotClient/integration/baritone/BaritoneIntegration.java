package mankool.mcBotClient.integration.baritone;

import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.integration.ClientIntegration;
import mankool.mcBotClient.integration.ConnectionContext;
import net.fabricmc.loader.api.FabricLoader;

import java.util.List;

/** Baritone integration: pathfinding commands, settings, and log/status forwarding. */
public class BaritoneIntegration implements ClientIntegration {

    @Override
    public List<String> capabilities() {
        return List.of("baritone");
    }

    @Override
    public boolean isAvailable() {
        // Meteor's Baritone fork uses the mod id "baritone-meteor"; upstream Baritone uses "baritone".
        FabricLoader loader = FabricLoader.getInstance();
        return loader.isModLoaded("baritone-meteor") || loader.isModLoaded("baritone");
    }

    @Override
    public void onConnectionSetup(ConnectionContext ctx) {
        BaritoneHandler baritoneHandler = new BaritoneHandler(ctx.client(), ctx.connection());
        // Installs JVM-wide log hooks once; constructing it re-points them at this connection.
        new BaritoneLogOutbound(ctx.client(), ctx.connection());

        ctx.registerHandler(Protocol.ManagerToClientMessage.PayloadCase.GET_BARITONE_SETTINGS,
            msg -> baritoneHandler.handleGetBaritoneSettings(msg.getMessageId(), msg.getGetBaritoneSettings()));
        ctx.registerHandler(Protocol.ManagerToClientMessage.PayloadCase.GET_BARITONE_COMMANDS,
            msg -> baritoneHandler.handleGetBaritoneCommands(msg.getMessageId(), msg.getGetBaritoneCommands()));
        ctx.registerHandler(Protocol.ManagerToClientMessage.PayloadCase.SET_BARITONE_SETTINGS,
            msg -> baritoneHandler.handleSetBaritoneSettings(msg.getMessageId(), msg.getSetBaritoneSettings()));
        ctx.registerHandler(Protocol.ManagerToClientMessage.PayloadCase.EXECUTE_BARITONE_COMMAND,
            msg -> baritoneHandler.handleExecuteBaritoneCommand(msg.getMessageId(), msg.getExecuteBaritoneCommand()));

        ctx.registerTick(baritoneHandler::tick);
    }
}
