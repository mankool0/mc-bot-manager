package mankool.mcBotClient.integration.meteor;

import mankool.mcbot.protocol.Commands;
import mankool.mcbot.protocol.Protocol;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.integration.ClientIntegration;
import mankool.mcBotClient.integration.ConnectionContext;
import mankool.mcBotClient.integration.InitContext;
import net.fabricmc.loader.api.FabricLoader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.lang.invoke.MethodHandles;
import java.util.List;
import java.util.UUID;

public class MeteorIntegration implements ClientIntegration {
    private static final Logger LOGGER = LoggerFactory.getLogger(MeteorIntegration.class);

    @Override
    public List<String> capabilities() {
        return List.of("meteor", "proxy");
    }

    @Override
    public boolean isAvailable() {
        return FabricLoader.getInstance().isModLoaded("meteor-client");
    }

    @Override
    public void onInitialize(InitContext ctx) {
        // The Orbit event bus needs this to dispatch our @EventHandler methods (ModuleToggleListener).
        try {
            meteordevelopment.meteorclient.MeteorClient.EVENT_BUS.registerLambdaFactory(
                "mankool.mcBotClient",
                (lookupInMethod, klass) -> (MethodHandles.Lookup) lookupInMethod.invoke(null, klass, MethodHandles.lookup())
            );
            LOGGER.info("Registered Orbit lambda factory for event handlers");
        } catch (Exception e) {
            LOGGER.error("Failed to register Orbit lambda factory", e);
        }
    }

    @Override
    public void onConnectionSetup(ConnectionContext ctx) {
        MeteorModuleHandler moduleHandler = new MeteorModuleHandler(ctx.client(), ctx.connection());
        ctx.registerHandler(Protocol.ManagerToClientMessage.PayloadCase.GET_MODULES,
            msg -> moduleHandler.handleGetModules(msg.getMessageId(), msg.getGetModules()));
        ctx.registerHandler(Protocol.ManagerToClientMessage.PayloadCase.SET_MODULE_CONFIG,
            msg -> moduleHandler.handleSetModuleConfig(msg.getMessageId(), msg.getSetModuleConfig()));

        PipeConnection connection = ctx.connection();
        ctx.registerHandler(Protocol.ManagerToClientMessage.PayloadCase.SET_PROXY_CONFIG,
            msg -> handleSetProxyConfig(connection, msg.getMessageId(), msg.getSetProxyConfig()));
    }

    private void handleSetProxyConfig(PipeConnection connection, String messageId, Commands.SetProxyConfigCommand command) {
        MeteorProxyManager.apply(command.getConfig());

        Commands.CommandResponse response = Commands.CommandResponse.newBuilder()
            .setRequestId(messageId)
            .setStatus(Commands.CommandResponse.Status.SUCCESS)
            .setMessage("Proxy config applied")
            .build();
        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setCommandResponse(response)
            .build();
        connection.sendMessage(message);
    }
}
