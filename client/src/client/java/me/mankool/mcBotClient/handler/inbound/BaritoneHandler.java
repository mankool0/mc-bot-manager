package me.mankool.mcBotClient.handler.inbound;

import baritone.api.BaritoneAPI;
import baritone.api.IBaritone;
import baritone.api.Settings;
import baritone.api.command.ICommand;
import baritone.api.command.manager.ICommandManager;
import mankool.mcbot.protocol.Baritone.*;
import mankool.mcbot.protocol.Protocol;
import me.mankool.mcBotClient.connection.PipeConnection;
import me.mankool.mcBotClient.api.baritone.IBaritoneSettingChangeListener;
import net.minecraft.client.MinecraftClient;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;

public class BaritoneHandler extends BaseInboundHandler {

    private static final Logger LOGGER = LoggerFactory.getLogger(BaritoneHandler.class);
    private static final int CHECK_INTERVAL_TICKS = 20;

    private boolean listenersInstalled = false;
    private int tickCounter = 0;

    public BaritoneHandler(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
        LOGGER.info("Initialized with Mixin-based setting change notifications");
    }

    public void tick() {
        if (!listenersInstalled) {
            return;
        }

        tickCounter++;
        if (tickCounter >= CHECK_INTERVAL_TICKS) {
            tickCounter = 0;
            pollForChanges();
        }
    }

    private void ensureListenersInstalled() {
        if (listenersInstalled) return;

        try {
            installListeners();
            listenersInstalled = true;
        } catch (Exception e) {
            LOGGER.error("Failed to install setting listeners", e);
        }
    }

    @SuppressWarnings("unchecked")
    private void installListeners() {
        Settings settings = BaritoneAPI.getSettings();
        int installedCount = 0;

        for (Settings.Setting<?> setting : settings.allSettings) {
            try {
                IBaritoneSettingChangeListener listenerSetting = (IBaritoneSettingChangeListener) (Object) setting;
                listenerSetting.mcBotClient$setChangeListener((oldValue, newValue) ->
                    notifySettingChanged(setting, oldValue, newValue)
                );
                installedCount++;
            } catch (Exception e) {
                LOGGER.warn("Failed to install listener for setting: {}", setting.getName(), e);
            }
        }

        LOGGER.info("Installed listeners on {} Baritone settings", installedCount);
    }

    @SuppressWarnings("unchecked")
    private void pollForChanges() {
        try {
            Settings settings = BaritoneAPI.getSettings();

            for (Settings.Setting<?> setting : settings.allSettings) {
                try {
                    IBaritoneSettingChangeListener listenerSetting = (IBaritoneSettingChangeListener) (Object) setting;
                    listenerSetting.mcBotClient$checkAndNotifyIfChanged();
                } catch (Exception e) {
                    LOGGER.trace("Error polling setting: {}", e.getMessage());
                }
            }
        } catch (Exception e) {
            LOGGER.error("Error polling for setting changes", e);
        }
    }

    private void notifySettingChanged(Settings.Setting<?> setting, Object oldValue, Object newValue) {
        try {
            LOGGER.debug("Baritone setting changed: {} = {} (was: {})",
                setting.getName(), newValue, oldValue);

            BaritoneSettingUpdate notification = BaritoneSettingUpdate.newBuilder()
                .setSettingName(setting.getName())
                .setNewValue(newValue != null ? newValue.toString() : "null")
                .build();

            sendBaritoneSettingUpdate(notification);
        } catch (Exception e) {
            LOGGER.error("Failed to notify setting change for {}", setting.getName(), e);
        }
    }

    public void handleGetBaritoneSettings(String messageId, GetBaritoneSettingsRequest request) {
        ensureListenersInstalled();

        try {
            GetBaritoneSettingsResponse.Builder response = GetBaritoneSettingsResponse.newBuilder();
            Settings settings = BaritoneAPI.getSettings();

            if (request.getSettingNamesCount() > 0) {
                for (String settingName : request.getSettingNamesList()) {
                    Settings.Setting<?> setting = settings.byLowerName.get(settingName.toLowerCase());
                    if (setting != null) {
                        BaritoneSettingInfo info = buildSettingInfo(setting);
                        response.addSettings(info);
                    } else {
                        LOGGER.warn("Requested setting not found: {}", settingName);
                    }
                }
            } else {
                for (Settings.Setting<?> setting : settings.allSettings) {
                    BaritoneSettingInfo info = buildSettingInfo(setting);
                    response.addSettings(info);
                }
            }

            sendBaritoneSettingsResponse(messageId, response.build());
        } catch (Exception e) {
            LOGGER.error("Failed to get Baritone settings", e);
            sendFailure(messageId, "Failed to get Baritone settings: " + e.getMessage());
        }
    }

    public void handleGetBaritoneCommands(String messageId, GetBaritoneCommandsRequest request) {
        try {
            GetBaritoneCommandsResponse.Builder response = GetBaritoneCommandsResponse.newBuilder();
            IBaritone baritone = BaritoneAPI.getProvider().getPrimaryBaritone();
            ICommandManager commandManager = baritone.getCommandManager();

            commandManager.getRegistry().stream()
                .map(this::buildCommandInfo)
                .forEach(response::addCommands);

            sendBaritoneCommandsResponse(messageId, response.build());
        } catch (Exception e) {
            LOGGER.error("Failed to get Baritone commands", e);
            sendFailure(messageId, "Failed to get Baritone commands: " + e.getMessage());
        }
    }

    public void handleSetBaritoneSettings(String messageId, SetBaritoneSettingsCommand command) {
        ensureListenersInstalled();

        try {
            Settings settings = BaritoneAPI.getSettings();
            List<BaritoneSettingInfo> updatedSettings = new ArrayList<>();
            StringBuilder errors = new StringBuilder();

            for (var entry : command.getSettingsMap().entrySet()) {
                String settingName = entry.getKey();
                String value = entry.getValue();

                Settings.Setting<?> setting = settings.byLowerName.get(settingName.toLowerCase());
                if (setting == null) {
                    errors.append("Setting not found: ").append(settingName).append("; ");
                    continue;
                }

                Object oldValue = setting.value;
                if (!applySetting(setting, value)) {
                    errors.append("Failed to apply setting: ").append(settingName).append("; ");
                    continue;
                }

                LOGGER.info("Updated Baritone setting {} from {} to {}", settingName, oldValue, value);
                updatedSettings.add(buildSettingInfo(setting));
            }

            boolean success = errors.isEmpty();
            String message = success ? "Settings updated successfully" : errors.toString();
            sendBaritoneSettingsSetResponse(messageId, success, message, updatedSettings);

        } catch (Exception e) {
            LOGGER.error("Failed to set Baritone settings", e);
            sendBaritoneSettingsSetResponse(messageId, false, "Error: " + e.getMessage(), new ArrayList<>());
        }
    }

    public void handleExecuteBaritoneCommand(String messageId, ExecuteBaritoneCommand command) {
        try {
            String commandStr = command.getCommand();
            if (commandStr == null || commandStr.trim().isEmpty()) {
                sendBaritoneCommandResponse(messageId, false, "Command string is empty");
                return;
            }

            IBaritone baritone = BaritoneAPI.getProvider().getPrimaryBaritone();
            ICommandManager commandManager = baritone.getCommandManager();

            client.execute(() -> {
                try {
                    LOGGER.info("Executing Baritone command: {}", commandStr);
                    boolean success = commandManager.execute(commandStr);
                    sendBaritoneCommandResponse(messageId, success,
                        success ? "Command executed successfully" : "Command execution failed");
                } catch (Exception e) {
                    LOGGER.error("Failed to execute Baritone command", e);
                    sendBaritoneCommandResponse(messageId, false, "Error: " + e.getMessage());
                }
            });

        } catch (Exception e) {
            LOGGER.error("Failed to execute Baritone command", e);
            sendBaritoneCommandResponse(messageId, false, "Error: " + e.getMessage());
        }
    }

    private BaritoneSettingInfo buildSettingInfo(Settings.Setting<?> setting) {
        return BaritoneSettingInfo.newBuilder()
            .setName(setting.getName())
            .setType(setting.getValueClass().getSimpleName())
            .setCurrentValue(setting.value != null ? setting.value.toString() : "null")
            .setDefaultValue(setting.defaultValue != null ? setting.defaultValue.toString() : "null")
            .build();
    }

    private BaritoneCommandInfo buildCommandInfo(ICommand command) {
        List<String> names = command.getNames();
        if (names == null || names.isEmpty()) {
            LOGGER.warn("Command has no names, using 'unknown'");
            names = List.of("unknown");
        }

        BaritoneCommandInfo.Builder builder = BaritoneCommandInfo.newBuilder()
            .setName(names.get(0))
            .addAllAliases(names)
            .setShortDesc(command.getShortDesc());

        List<String> longDesc = command.getLongDesc();
        if (longDesc != null && !longDesc.isEmpty()) {
            builder.addAllLongDesc(longDesc);
        }

        return builder.build();
    }

    private boolean applySetting(Settings.Setting<?> setting, String value) {
        try {
            Class<?> type = setting.getValueClass();
            Object parsedValue;

            if (type == Boolean.class) {
                parsedValue = Boolean.parseBoolean(value);
            } else if (type == Integer.class) {
                parsedValue = Integer.parseInt(value);
            } else if (type == Long.class) {
                parsedValue = Long.parseLong(value);
            } else if (type == Float.class) {
                parsedValue = Float.parseFloat(value);
            } else if (type == Double.class) {
                parsedValue = Double.parseDouble(value);
            } else if (type == String.class) {
                parsedValue = value;
            } else {
                LOGGER.warn("Unsupported setting type {} for setting {}, skipping", type, setting.getName());
                return false;
            }

            @SuppressWarnings("unchecked")
            IBaritoneSettingChangeListener listenerSetting = (IBaritoneSettingChangeListener) (Object) setting;
            listenerSetting.mcBotClient$setValue(parsedValue);

            return true;
        } catch (Exception e) {
            LOGGER.error("Failed to parse value '{}' for setting {}", value, setting.getName(), e);
            return false;
        }
    }

    private void sendBaritoneSettingsResponse(String messageId, GetBaritoneSettingsResponse response) {
        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(messageId)
            .setTimestamp(System.currentTimeMillis())
            .setBaritoneSettingsResponse(response)
            .build();

        connection.sendMessage(message);
    }

    private void sendBaritoneCommandsResponse(String messageId, GetBaritoneCommandsResponse response) {
        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(messageId)
            .setTimestamp(System.currentTimeMillis())
            .setBaritoneCommandsResponse(response)
            .build();

        connection.sendMessage(message);
    }

    private void sendBaritoneSettingsSetResponse(String messageId, boolean success, String result, List<BaritoneSettingInfo> updatedSettings) {
        SetBaritoneSettingsResponse response = SetBaritoneSettingsResponse.newBuilder()
            .setSuccess(success)
            .setResult(result)
            .addAllUpdatedSettings(updatedSettings)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(messageId)
            .setTimestamp(System.currentTimeMillis())
            .setBaritoneSettingsSetResponse(response)
            .build();

        connection.sendMessage(message);
    }

    private void sendBaritoneCommandResponse(String messageId, boolean success, String result) {
        ExecuteBaritoneCommandResponse response = ExecuteBaritoneCommandResponse.newBuilder()
            .setSuccess(success)
            .setResult(result)
            .build();

        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(messageId)
            .setTimestamp(System.currentTimeMillis())
            .setBaritoneCommandResponse(response)
            .build();

        connection.sendMessage(message);
    }

    private void sendBaritoneSettingUpdate(BaritoneSettingUpdate notification) {
        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(java.util.UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setBaritoneSettingUpdate(notification)
            .build();

        connection.sendMessage(message);
    }
}