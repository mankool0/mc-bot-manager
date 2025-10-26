package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Meteor.*;
import me.mankool.mcBotClient.connection.PipeConnection;
import meteordevelopment.meteorclient.settings.*;
import meteordevelopment.meteorclient.systems.modules.Module;
import meteordevelopment.meteorclient.systems.modules.Modules;
import net.minecraft.client.MinecraftClient;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.lang.reflect.Field;
import java.util.Collection;
import java.util.function.Consumer;

public class MeteorModuleHandler extends BaseInboundHandler {

    private static final Logger LOGGER = LoggerFactory.getLogger(MeteorModuleHandler.class);
    private boolean hooksInstalled = false;

    public MeteorModuleHandler(MinecraftClient client, PipeConnection connection) {
        super(client, connection);
        LOGGER.info("Initialized (hooks will be installed on first use)");
    }

    private void ensureHooksInstalled() {
        if (hooksInstalled) return;

        try {
            if (Modules.get() == null) {
                LOGGER.warn("Modules system not ready yet");
                return;
            }

            Collection<Module> modules = Modules.get().getAll();
            if (modules == null || modules.isEmpty()) {
                LOGGER.warn("No modules available yet");
                return;
            }

            hookIntoSettings();
            subscribeToModuleEvents();
            hooksInstalled = true;
        } catch (Exception e) {
            LOGGER.error("Failed to hook into settings", e);
        }
    }

    @SuppressWarnings("unchecked")
    private void hookIntoSettings() throws Exception {
        Field onChangedField = Setting.class.getDeclaredField("onChanged");
        onChangedField.setAccessible(true);

        int hookedCount = 0;

        for (Module module : Modules.get().getAll()) {
            for (SettingGroup group : module.settings.groups) {
                String groupName = group.name;

                for (Setting<?> setting : group) {
                    try {
                        Consumer<Object> originalConsumer = (Consumer<Object>) onChangedField.get(setting);

                        Consumer<Object> wrappedConsumer = (value) -> {
                            try {
                                if (originalConsumer != null) {
                                    originalConsumer.accept(value);
                                }
                                notifySettingChanged(module, groupName, setting);
                            } catch (Exception e) {
                                LOGGER.error("Error in setting change hook for {}.{}", module.name, setting.name, e);
                            }
                        };

                        onChangedField.set(setting, wrappedConsumer);
                        hookedCount++;
                    } catch (Exception e) {
                        LOGGER.warn("Failed to hook setting {}.{}", module.name, setting.name, e);
                    }
                }
            }
        }

        LOGGER.info("Hooked into {} settings from {} modules", hookedCount, Modules.get().getAll().size());
    }

    private void subscribeToModuleEvents() {
        ModuleToggleListener listener = new ModuleToggleListener(this);
        meteordevelopment.meteorclient.MeteorClient.EVENT_BUS.subscribe(listener);
        LOGGER.info("Subscribed to module toggle events");
    }

    void notifyModuleToggled(Module module) {
        try {
            LOGGER.debug("Module toggled: {} = {}", module.name, module.isActive());

            ModuleStateChanged notification = ModuleStateChanged.newBuilder()
                .setModuleName(module.name)
                .setEnabled(module.isActive())
                .build();

            sendModuleStateChanged(notification);
        } catch (Exception e) {
            LOGGER.error("Failed to notify module toggle for {}", module.name, e);
        }
    }

    private void notifySettingChanged(Module module, String groupName, Setting<?> setting) {
        try {
            String settingPath = setting.name;
            if (groupName != null && !groupName.equals("General")) {
                settingPath = groupName + "." + setting.name;
            }

            LOGGER.debug("Setting changed: {}.{} = {}", module.name, settingPath, setting.get());

            ModuleStateChanged notification = ModuleStateChanged.newBuilder()
                .setModuleName(module.name)
                .putChangedSettings(settingPath, setting.get().toString())
                .build();

            sendModuleStateChanged(notification);
        } catch (Exception e) {
            LOGGER.error("Failed to notify setting change for {}.{}", module.name, setting.name, e);
        }
    }

    public void handleGetModules(String messageId, GetModulesRequest request) {
        ensureHooksInstalled();

        try {
            GetModulesResponse.Builder response = GetModulesResponse.newBuilder();

            Collection<Module> modules = Modules.get().getAll();
            for (Module module : modules) {
                // Filter by category if requested
                if (request.hasCategoryFilter() &&
                    !module.category.toString().equalsIgnoreCase(request.getCategoryFilter())) {
                    continue;
                }

                ModuleInfo moduleInfo = buildModuleInfo(module);
                response.addModules(moduleInfo);
            }

            sendModulesResponse(response.build());
        } catch (Exception e) {
            sendFailure(messageId, "Failed to get modules: " + e.getMessage());
        }
    }

    public void handleSetModuleConfig(String messageId, SetModuleConfigCommand command) {
        ensureHooksInstalled();

        try {
            Module module = Modules.get().get(command.getModuleName());
            if (module == null) {
                sendModuleConfigResponse(false, "Module not found: " + command.getModuleName(), null);
                return;
            }

            if (command.hasEnabled()) {
                if (command.getEnabled() && !module.isActive()) {
                    module.toggle();
                } else if (!command.getEnabled() && module.isActive()) {
                    module.toggle();
                }
            }

            for (var entry : command.getSettingsMap().entrySet()) {
                String settingName = entry.getKey();
                String value = entry.getValue();

                if (!applySetting(module, settingName, value)) {
                    sendModuleConfigResponse(false, "Failed to apply setting: " + settingName, null);
                    return;
                }
            }

            ModuleInfo updatedInfo = buildModuleInfo(module);
            sendModuleConfigResponse(true, "Module configured successfully", updatedInfo);

        } catch (Exception e) {
            sendModuleConfigResponse(false, "Error: " + e.getMessage(), null);
        }
    }

    private ModuleInfo buildModuleInfo(Module module) {
        ModuleInfo.Builder info = ModuleInfo.newBuilder()
            .setName(module.name)
            .setCategory(module.category.toString())
            .setEnabled(module.isActive())
            .setDescription(module.description);

        // Extract settings from all setting groups
        for (SettingGroup group : module.settings.groups) {
            for (Setting<?> setting : group) {
                SettingInfo settingInfo = extractSettingInfo(setting, group.name);
                if (settingInfo != null) {
                    info.addSettings(settingInfo);
                }
            }
        }

        return info.build();
    }

    private SettingInfo extractSettingInfo(Setting<?> setting, String groupName) {
        try {
            SettingInfo.Builder builder = SettingInfo.newBuilder()
                .setName(setting.name)
                .setCurrentValue(setting.get().toString());

            // Set group name if not "General" (default group)
            if (groupName != null && !groupName.equals("General")) {
                builder.setGroupName(groupName);
            }

            // Set description if available
            if (setting.description != null && !setting.description.isEmpty()) {
                builder.setDescription(setting.description);
            }

            // Determine setting type and add type-specific info
            switch (setting) {
                case BoolSetting ignored -> builder.setType(SettingInfo.SettingType.BOOLEAN);
                case IntSetting intSetting -> {
                    builder.setType(SettingInfo.SettingType.INTEGER);
                    builder.setMinValue(intSetting.min);
                    builder.setMaxValue(intSetting.max);
                }
                case DoubleSetting doubleSetting -> {
                    builder.setType(SettingInfo.SettingType.DOUBLE);
                    builder.setMinValue(doubleSetting.min);
                    builder.setMaxValue(doubleSetting.max);
                }
                case StringSetting ignored -> builder.setType(SettingInfo.SettingType.STRING);
                case EnumSetting<?> enumSetting -> {
                    builder.setType(SettingInfo.SettingType.ENUM);
                    for (Enum<?> value : enumSetting.get().getClass().getEnumConstants()) {
                        builder.addPossibleValues(value.name());
                    }
                }
                case ColorSetting ignored -> builder.setType(SettingInfo.SettingType.COLOR);
                case KeybindSetting ignored -> builder.setType(SettingInfo.SettingType.KEYBIND);
                case BlockSetting ignored -> builder.setType(SettingInfo.SettingType.BLOCK);
                case ItemSetting ignored -> builder.setType(SettingInfo.SettingType.ITEM);
                default ->
                    // Unknown setting type, use STRING as fallback
                        builder.setType(SettingInfo.SettingType.STRING);
            }

            return builder.build();
        } catch (Exception e) {
            LOGGER.warn("Failed to extract setting info for: {}", setting.name, e);
            return null;
        }
    }

    private boolean applySetting(Module module, String settingPath, String value) {
        try {
            String groupName = null;
            String settingName = settingPath;

            if (settingPath.contains(".")) {
                String[] parts = settingPath.split("\\.", 2);
                groupName = parts[0];
                settingName = parts[1];
            }

            for (SettingGroup group : module.settings.groups) {
                if (groupName != null && !group.name.equals(groupName)) {
                    continue;
                }

                Setting<?> setting = group.get(settingName);
                if (setting != null) {
                    return applySettingValue(setting, value);
                }
            }
            return false;
        } catch (Exception e) {
            LOGGER.error("Failed to apply setting {}", settingPath, e);
            return false;
        }
    }

    @SuppressWarnings("unchecked")
    private boolean applySettingValue(Setting<?> setting, String value) {
        try {
            switch (setting) {
                case BoolSetting boolSetting -> boolSetting.set(Boolean.parseBoolean(value));
                case IntSetting intSetting -> intSetting.set(Integer.parseInt(value));
                case DoubleSetting doubleSetting -> doubleSetting.set(Double.parseDouble(value));
                case StringSetting stringSetting -> stringSetting.set(value);
                case EnumSetting<?> enumSetting -> {
                    Class<?> enumClass = enumSetting.get().getClass();
                    for (Enum<?> enumValue : (Enum<?>[]) enumClass.getEnumConstants()) {
                        if (enumValue.name().equalsIgnoreCase(value)) {
                            ((EnumSetting<Enum<?>>) enumSetting).set(enumValue);
                            return true;
                        }
                    }
                    return false;
                }
                default -> setting.parse(value);
            }
            return true;
        } catch (Exception e) {
            LOGGER.error("Failed to parse value '{}' for setting {}", value, setting.name, e);
            return false;
        }
    }

    private void sendModulesResponse(GetModulesResponse response) {
        mankool.mcbot.protocol.Protocol.ClientToManagerMessage message =
            mankool.mcbot.protocol.Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(java.util.UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setModulesResponse(response)
                .build();

        connection.sendMessage(message);
    }

    private void sendModuleConfigResponse(boolean success, String message, ModuleInfo updatedModule) {
        SetModuleConfigResponse.Builder response = SetModuleConfigResponse.newBuilder()
            .setSuccess(success)
            .setErrorMessage(message);

        if (updatedModule != null) {
            response.setUpdatedModule(updatedModule);
        }

        mankool.mcbot.protocol.Protocol.ClientToManagerMessage responseMessage =
            mankool.mcbot.protocol.Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(java.util.UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setModuleConfigResponse(response.build())
                .build();

        connection.sendMessage(responseMessage);
    }

    private void sendModuleStateChanged(ModuleStateChanged notification) {
        mankool.mcbot.protocol.Protocol.ClientToManagerMessage message =
            mankool.mcbot.protocol.Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(java.util.UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setModuleStateChanged(notification)
                .build();

        connection.sendMessage(message);
    }
}