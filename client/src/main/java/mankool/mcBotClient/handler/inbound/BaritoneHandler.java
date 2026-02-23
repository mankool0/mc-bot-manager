package mankool.mcBotClient.handler.inbound;

import baritone.api.BaritoneAPI;
import baritone.api.IBaritone;
import baritone.api.Settings;
import baritone.api.behavior.IPathingBehavior;
import baritone.api.command.ICommand;
import baritone.api.command.manager.ICommandManager;
import baritone.api.event.events.*;
import baritone.api.event.listener.IGameEventListener;
import baritone.api.pathing.calc.IPathingControlManager;
import baritone.api.pathing.goals.Goal;
import baritone.api.process.IBaritoneProcess;
import baritone.api.utils.SettingsUtil;
import mankool.mcbot.protocol.Baritone;
import mankool.mcbot.protocol.Baritone.*;
import mankool.mcbot.protocol.Protocol;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Vec3i;
import net.minecraft.world.level.block.Mirror;
import net.minecraft.world.level.block.Rotation;
import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.api.baritone.IBaritoneSettingChangeListener;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.awt.Color;
import java.util.*;

public class BaritoneHandler extends BaseInboundHandler {

    private static final Logger LOGGER = LoggerFactory.getLogger(BaritoneHandler.class);
    private static final int CHECK_INTERVAL_TICKS = 20;

    private boolean listenersInstalled = false;
    private boolean pathEventListenerInstalled = false;
    private int tickCounter = 0;

    // Track last sent state to avoid spam
    private PathEvent lastPathEvent = null;
    private boolean lastIsPathing = false;
    private String lastGoalDescription = null;
    private Double lastEstimatedTicksToGoal = null;
    private Double lastTicksRemainingInSegment = null;

    public BaritoneHandler(Minecraft client, PipeConnection connection) {
        super(client, connection);
        LOGGER.info("Initialized with Mixin-based setting change notifications");
        installPathEventListener();
    }

    public void tick() {
        if (!listenersInstalled) {
            return;
        }

        tickCounter++;
        if (tickCounter >= CHECK_INTERVAL_TICKS) {
            tickCounter = 0;
            pollForChanges();
            pollBaritoneStatus();
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

            BaritoneSettingValue value = toProtoValue(setting.value, setting);
            if (value == null) {
                LOGGER.warn("Unable to serialize Baritone setting value for {}", setting.getName());
                return;
            }

            BaritoneSettingUpdate notification = BaritoneSettingUpdate.newBuilder()
                    .setSettingName(setting.getName())
                    .setNewValue(value)
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
                        BaritoneSettingInfo info = toProtoSettingInfo(setting);
                        if (info != null) {
                            response.addSettings(info);
                        }
                    } else {
                        LOGGER.warn("Requested setting not found: {}", settingName);
                    }
                }
            } else {
                for (Settings.Setting<?> setting : settings.allSettings) {
                    BaritoneSettingInfo info = toProtoSettingInfo(setting);
                    if (info != null) {
                        response.addSettings(info);
                    }
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
                    .map(this::toProtoCommandInfo)
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
                BaritoneSettingValue value = entry.getValue();

                Settings.Setting<?> setting = settings.byLowerName.get(settingName.toLowerCase());
                if (setting == null) {
                    errors.append("Setting not found: ").append(settingName).append("; ");
                    continue;
                }

                Object oldValue = setting.value;
                if (!applyProtoSetting(setting, value)) {
                    errors.append("Failed to apply setting: ").append(settingName).append("; ");
                    continue;
                }

                LOGGER.info("Updated Baritone setting {} from {} to {}", settingName, oldValue, setting.value);
                BaritoneSettingInfo info = toProtoSettingInfo(setting);
                if (info != null) {
                    updatedSettings.add(info);
                }
            }

            boolean success = errors.isEmpty();

            if (success && !updatedSettings.isEmpty()) {
                try {
                    SettingsUtil.save(settings);
                    LOGGER.info("Saved {} Baritone settings to file", updatedSettings.size());
                } catch (Exception e) {
                    LOGGER.error("Failed to save Baritone settings to file", e);
                    success = false;
                    errors.append("Settings applied but failed to save to file: ").append(e.getMessage());
                }
            }

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
            if (commandStr.trim().isEmpty()) {
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

    private BaritoneSettingInfo toProtoSettingInfo(Settings.Setting<?> setting) {
        BaritoneSettingInfo.SettingType type = toProtoSettingType(setting);
        if (type == BaritoneSettingInfo.SettingType.UNRECOGNIZED) {
            return null;
        }

        BaritoneSettingInfo.Builder builder = BaritoneSettingInfo.newBuilder()
                .setName(setting.getName())
                .setType(type);

        BaritoneSettingValue currentValue = toProtoValue(setting.value, setting);
        if (currentValue != null) {
            builder.setCurrentValue(currentValue);
        }

        BaritoneSettingValue defaultValue = toProtoValue(setting.defaultValue, setting);
        if (defaultValue != null) {
            builder.setDefaultValue(defaultValue);
        }

        if (type == BaritoneSettingInfo.SettingType.LIST) {
            addPossibleValues(setting, builder);
        } else if (type == BaritoneSettingInfo.SettingType.MAP_BLOCK_TO_BLOCK_LIST) {
            addMapBlockToBlockListMetadata(setting, builder);
        }

        return builder.build();
    }

    private void addPossibleValues(Settings.Setting<?> setting, BaritoneSettingInfo.Builder builder) {
        try {
            java.lang.reflect.Type elementType = getSettingTypeArgument(setting, 0);

            if (elementType == net.minecraft.world.level.block.Block.class) {
                for (net.minecraft.world.level.block.Block block : net.minecraft.core.registries.BuiltInRegistries.BLOCK) {
                    builder.addPossibleValues(net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(block).toString());
                }
            } else if (elementType == net.minecraft.world.item.Item.class) {
                for (net.minecraft.world.item.Item item : net.minecraft.core.registries.BuiltInRegistries.ITEM) {
                    builder.addPossibleValues(net.minecraft.core.registries.BuiltInRegistries.ITEM.getKey(item).toString());
                }
            } else if (elementType == String.class) {
                // String lists (like buildIgnoreProperties) - no predefined values
                LOGGER.debug("Skipping possible values for String list setting '{}'", setting.getName());
            } else if (elementType != null) {
                LOGGER.warn("Unsupported List element type for setting '{}': List<{}> - add proto support and manual handling",
                        setting.getName(), elementType);
            }
        } catch (Exception e) {
            LOGGER.debug("Failed to add possible values for setting {}", setting.getName(), e);
        }
    }

    private void addMapBlockToBlockListMetadata(Settings.Setting<?> setting, BaritoneSettingInfo.Builder builder) {
        try {
            MapMetadata.Builder metadataBuilder = MapMetadata.newBuilder();

            for (net.minecraft.world.level.block.Block block : net.minecraft.core.registries.BuiltInRegistries.BLOCK) {
                metadataBuilder.addPossibleKeys(net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(block).toString());
                metadataBuilder.addPossibleListValues(net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(block).toString());
            }

            builder.setMapMetadata(metadataBuilder.build());
        } catch (Exception e) {
            LOGGER.debug("Failed to add map metadata for setting {}", setting.getName(), e);
        }
    }

    private java.lang.reflect.Type getSettingTypeArgument(Settings.Setting<?> setting, int index) {
        try {
            Settings settings = BaritoneAPI.getSettings();
            java.lang.reflect.Type type = settings.settingTypes.get(setting);

            if (type instanceof java.lang.reflect.ParameterizedType paramType) {
                java.lang.reflect.Type[] args = paramType.getActualTypeArguments();
                if (args.length > index) {
                    return args[index];
                }
            }
        } catch (Exception e) {
            LOGGER.info("Failed to get type argument {} for {}", index, setting.getName(), e);
        }
        return null;
    }

    private boolean isMapBlockToBlockList(Settings.Setting<?> setting) {
        try {
            java.lang.reflect.Type keyType = getSettingTypeArgument(setting, 0);
            java.lang.reflect.Type valueType = getSettingTypeArgument(setting, 1);

            boolean result = keyType == net.minecraft.world.level.block.Block.class &&
                   valueType instanceof java.lang.reflect.ParameterizedType valueParamType &&
                   valueParamType.getRawType() == List.class &&
                   valueParamType.getActualTypeArguments().length > 0 &&
                   valueParamType.getActualTypeArguments()[0] == net.minecraft.world.level.block.Block.class;

            return result;
        } catch (Exception e) {
            LOGGER.info("Failed to check map type for {}", setting.getName(), e);
            return false;
        }
    }

    private BaritoneCommandInfo toProtoCommandInfo(ICommand command) {
        List<String> names = command.getNames();
        if (names == null || names.isEmpty()) {
            LOGGER.warn("Command has no names, using 'unknown'");
            names = List.of("unknown");
        }

        BaritoneCommandInfo.Builder builder = BaritoneCommandInfo.newBuilder()
                .setName(names.getFirst())
                .addAllAliases(names)
                .setShortDesc(command.getShortDesc());

        List<String> longDesc = command.getLongDesc();
        if (longDesc != null && !longDesc.isEmpty()) {
            builder.addAllLongDesc(longDesc);
        }

        return builder.build();
    }

    private boolean applyProtoSetting(Settings.Setting<?> setting, BaritoneSettingValue protoValue) {
        try {
            Class<?> type = setting.getValueClass();
            Object parsedValue;

            if (type == Boolean.class) {
                if (!protoValue.hasBoolValue()) return false;
                parsedValue = protoValue.getBoolValue();
            } else if (type == Integer.class) {
                if (!protoValue.hasIntValue()) return false;
                parsedValue = protoValue.getIntValue();
            } else if (type == Long.class) {
                if (!protoValue.hasLongValue()) return false;
                parsedValue = protoValue.getLongValue();
            } else if (type == Float.class) {
                if (!protoValue.hasFloatValue()) return false;
                parsedValue = protoValue.getFloatValue();
            } else if (type == Double.class) {
                if (!protoValue.hasDoubleValue()) return false;
                parsedValue = protoValue.getDoubleValue();
            } else if (type == String.class) {
                if (!protoValue.hasStringValue()) return false;
                parsedValue = protoValue.getStringValue();
            } else if (type == Color.class) {
                if (!protoValue.hasColorValue()) return false;
                RGBColor color = protoValue.getColorValue();
                parsedValue = new Color(color.getRed(), color.getGreen(), color.getBlue());
            } else if (type == List.class) {
                if (!protoValue.hasListValue()) return false;
                java.lang.reflect.Type elementType = getSettingTypeArgument(setting, 0);
                List<Object> list = new ArrayList<>();

                for (String item : protoValue.getListValue().getItemsList()) {
                    if (elementType == net.minecraft.world.level.block.Block.class) {
                        net.minecraft.resources.Identifier id = net.minecraft.resources.Identifier.tryParse(item);
                        if (id != null) {
                            list.add(net.minecraft.core.registries.BuiltInRegistries.BLOCK.getValue(id));
                        }
                    } else if (elementType == net.minecraft.world.item.Item.class) {
                        net.minecraft.resources.Identifier id = net.minecraft.resources.Identifier.tryParse(item);
                        if (id != null) {
                            list.add(net.minecraft.core.registries.BuiltInRegistries.ITEM.getValue(id));
                        }
                    } else {
                        list.add(item);
                    }
                }
                parsedValue = list;
            } else if (type == Map.class) {
                // Check if it's Map<Block, List<Block>>
                if (isMapBlockToBlockList(setting)) {
                    if (!protoValue.hasBlockToBlockListMapValue()) return false;
                    Map<net.minecraft.world.level.block.Block, List<net.minecraft.world.level.block.Block>> blockMap = new HashMap<>();
                    for (var entry : protoValue.getBlockToBlockListMapValue().getEntriesMap().entrySet()) {
                        String keyId = entry.getKey();
                        StringList valueList = entry.getValue();

                        net.minecraft.resources.Identifier keyIdentifier = net.minecraft.resources.Identifier.tryParse(keyId);
                        if (keyIdentifier == null) {
                            LOGGER.warn("Invalid block ID for map key: {}", keyId);
                            continue;
                        }
                        net.minecraft.world.level.block.Block keyBlock = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getValue(keyIdentifier);

                        List<net.minecraft.world.level.block.Block> valueBlocks = new ArrayList<>();
                        for (String blockId : valueList.getItemsList()) {
                            net.minecraft.resources.Identifier blockIdentifier = net.minecraft.resources.Identifier.tryParse(blockId);
                            if (blockIdentifier != null) {
                                net.minecraft.world.level.block.Block block = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getValue(blockIdentifier);
                                valueBlocks.add(block);
                            } else {
                                LOGGER.warn("Invalid block ID in map value: {}", blockId);
                            }
                        }

                        blockMap.put(keyBlock, valueBlocks);
                    }
                    parsedValue = blockMap;
                } else {
                    // Generic string map
                    if (!protoValue.hasMapValue()) return false;
                    parsedValue = new HashMap<>(protoValue.getMapValue().getEntriesMap());
                }
            } else if (type == BlockPos.class) {
                if (!protoValue.hasVec3IValue()) return false;
                Baritone.Vec3i vec = protoValue.getVec3IValue();
                parsedValue = new BlockPos(vec.getX(), vec.getY(), vec.getZ());
            } else if (type == Vec3i.class) {
                if (!protoValue.hasVec3IValue()) return false;
                Baritone.Vec3i vec = protoValue.getVec3IValue();
                parsedValue = new Vec3i(vec.getX(), vec.getY(), vec.getZ());
            } else if (type == Rotation.class) {
                if (!protoValue.hasRotationValue()) return false;
                parsedValue = fromProtoRotation(protoValue.getRotationValue());
            } else if (type == Mirror.class) {
                if (!protoValue.hasMirrorValue()) return false;
                parsedValue = fromProtoMirror(protoValue.getMirrorValue());
            } else {
                LOGGER.warn("Unsupported setting type {} for setting {}", type, setting.getName());
                return false;
            }

            @SuppressWarnings("unchecked")
            IBaritoneSettingChangeListener listenerSetting = (IBaritoneSettingChangeListener) (Object) setting;
            listenerSetting.mcBotClient$setValue(parsedValue);

            return true;
        } catch (Exception e) {
            LOGGER.error("Failed to apply value for setting {}", setting.getName(), e);
            return false;
        }
    }

    private BaritoneSettingInfo.SettingType toProtoSettingType(Settings.Setting<?> setting) {
        Class<?> type = setting.getValueClass();

        if (type == Boolean.class) return BaritoneSettingInfo.SettingType.BOOLEAN;
        if (type == Integer.class) return BaritoneSettingInfo.SettingType.INTEGER;
        if (type == Double.class) return BaritoneSettingInfo.SettingType.DOUBLE;
        if (type == Float.class) return BaritoneSettingInfo.SettingType.FLOAT;
        if (type == Long.class) return BaritoneSettingInfo.SettingType.LONG;
        if (type == String.class) return BaritoneSettingInfo.SettingType.STRING;
        if (type == Color.class) return BaritoneSettingInfo.SettingType.COLOR;
        if (type == List.class) return BaritoneSettingInfo.SettingType.LIST;
        if (type == Map.class) {
            if (isMapBlockToBlockList(setting)) {
                return BaritoneSettingInfo.SettingType.MAP_BLOCK_TO_BLOCK_LIST;
            } else {
                LOGGER.warn("Unrecognized Map type for setting '{}' - add proto support and handling",
                        setting.getName());
                return BaritoneSettingInfo.SettingType.UNRECOGNIZED;
            }
        }
        if (type == BlockPos.class) return BaritoneSettingInfo.SettingType.VEC3I;
        if (type == Vec3i.class) return BaritoneSettingInfo.SettingType.VEC3I;
        if (type == Rotation.class) return BaritoneSettingInfo.SettingType.BLOCK_ROTATION;
        if (type == Mirror.class) return BaritoneSettingInfo.SettingType.BLOCK_MIRROR;

        LOGGER.warn("Unrecognized Baritone setting type for '{}': {} - add support or explicitly ignore this type",
                setting.getName(), type.getSimpleName());
        return BaritoneSettingInfo.SettingType.UNRECOGNIZED;
    }

    @SuppressWarnings("unchecked")
    private BaritoneSettingValue toProtoValue(Object value, Settings.Setting<?> setting) {
        try {
            if (value == null) return null;

            BaritoneSettingValue.Builder builder = BaritoneSettingValue.newBuilder();

            switch (value) {
                case Boolean b -> builder.setBoolValue(b);
                case Integer i -> builder.setIntValue(i);
                case Double d -> builder.setDoubleValue(d);
                case Float f -> builder.setFloatValue(f);
                case Long l -> builder.setLongValue(l);
                case String s -> builder.setStringValue(s);
                case Color color -> builder.setColorValue(RGBColor.newBuilder()
                        .setRed(color.getRed())
                        .setGreen(color.getGreen())
                        .setBlue(color.getBlue())
                        .build());
                case List<?> list -> {
                    List<String> strings = new ArrayList<>();
                    for (Object item : list) {
                        if (item instanceof net.minecraft.world.level.block.Block block) {
                            strings.add(net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(block).toString());
                        } else if (item instanceof net.minecraft.world.item.Item itemObj) {
                            strings.add(net.minecraft.core.registries.BuiltInRegistries.ITEM.getKey(itemObj).toString());
                        } else {
                            strings.add(item.toString());
                        }
                    }
                    builder.setListValue(StringList.newBuilder().addAllItems(strings).build());
                }
                case Map<?, ?> map -> {
                    if (setting != null && isMapBlockToBlockList(setting)) {
                        BlockToBlockListMap.Builder mapBuilder = BlockToBlockListMap.newBuilder();
                        for (Map.Entry<?, ?> entry : map.entrySet()) {
                            net.minecraft.world.level.block.Block keyBlock = (net.minecraft.world.level.block.Block) entry.getKey();
                            @SuppressWarnings("unchecked")
                            List<net.minecraft.world.level.block.Block> valueBlocks = (List<net.minecraft.world.level.block.Block>) entry.getValue();

                            String keyId = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(keyBlock).toString();
                            StringList.Builder listBuilder = StringList.newBuilder();
                            for (net.minecraft.world.level.block.Block block : valueBlocks) {
                                listBuilder.addItems(net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(block).toString());
                            }
                            mapBuilder.putEntries(keyId, listBuilder.build());
                        }
                        builder.setBlockToBlockListMapValue(mapBuilder.build());
                    } else {
                        Map<String, String> stringMap = new HashMap<>();
                        for (Map.Entry<?, ?> entry : map.entrySet()) {
                            stringMap.put(entry.getKey().toString(), entry.getValue().toString());
                        }
                        builder.setMapValue(StringMap.newBuilder().putAllEntries(stringMap).build());
                    }
                }
                case BlockPos pos -> builder.setVec3IValue(Baritone.Vec3i.newBuilder()
                        .setX(pos.getX())
                        .setY(pos.getY())
                        .setZ(pos.getZ())
                        .build());
                case Vec3i vec -> builder.setVec3IValue(Baritone.Vec3i.newBuilder()
                        .setX(vec.getX())
                        .setY(vec.getY())
                        .setZ(vec.getZ())
                        .build());
                case Rotation rotation -> builder.setRotationValue(toProtoRotation(rotation));
                case Mirror mirror -> builder.setMirrorValue(toProtoMirror(mirror));
                default -> {
                    LOGGER.warn("Unsupported Baritone setting type for serialization: {}", value.getClass());
                    return null;
                }
            }

            return builder.build();
        } catch (Exception e) {
            LOGGER.error("Failed to serialize Baritone setting value", e);
            return null;
        }
    }

    private Rotation fromProtoRotation(Baritone.BlockRotation rotation) {
        return switch (rotation) {
            case BLOCK_ROTATION_CLOCKWISE_90 -> Rotation.CLOCKWISE_90;
            case BLOCK_ROTATION_CLOCKWISE_180 -> Rotation.CLOCKWISE_180;
            case BLOCK_ROTATION_COUNTERCLOCKWISE_90 -> Rotation.COUNTERCLOCKWISE_90;
            default -> Rotation.NONE;
        };
    }

    private Baritone.BlockRotation toProtoRotation(Rotation rotation) {
        return switch (rotation) {
            case CLOCKWISE_90 -> Baritone.BlockRotation.BLOCK_ROTATION_CLOCKWISE_90;
            case CLOCKWISE_180 -> Baritone.BlockRotation.BLOCK_ROTATION_CLOCKWISE_180;
            case COUNTERCLOCKWISE_90 ->
                    Baritone.BlockRotation.BLOCK_ROTATION_COUNTERCLOCKWISE_90;
            default -> Baritone.BlockRotation.BLOCK_ROTATION_NONE;
        };
    }

    private Mirror fromProtoMirror(Baritone.BlockMirror mirror) {
        return switch (mirror) {
            case BLOCK_MIRROR_LEFT_RIGHT -> Mirror.LEFT_RIGHT;
            case BLOCK_MIRROR_FRONT_BACK -> Mirror.FRONT_BACK;
            default -> Mirror.NONE;
        };
    }

    private Baritone.BlockMirror toProtoMirror(Mirror mirror) {
        return switch (mirror) {
            case LEFT_RIGHT -> Baritone.BlockMirror.BLOCK_MIRROR_LEFT_RIGHT;
            case FRONT_BACK -> Baritone.BlockMirror.BLOCK_MIRROR_FRONT_BACK;
            default -> Baritone.BlockMirror.BLOCK_MIRROR_NONE;
        };
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
                .setMessageId(UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setBaritoneSettingUpdate(notification)
                .build();

        connection.sendMessage(message);
    }

    private void installPathEventListener() {
        if (pathEventListenerInstalled) return;

        try {
            IBaritone baritone = BaritoneAPI.getProvider().getPrimaryBaritone();
            baritone.getGameEventHandler().registerEventListener(new PathEventListenerAdapter());
            pathEventListenerInstalled = true;
            LOGGER.info("Registered Baritone path event listener");
        } catch (Exception e) {
            LOGGER.error("Failed to install path event listener", e);
        }
    }

    private class PathEventListenerAdapter implements IGameEventListener {
        @Override
        public void onTick(TickEvent event) {}

        @Override
        public void onPostTick(TickEvent event) {}

        @Override
        public void onPlayerUpdate(PlayerUpdateEvent event) {}

        @Override
        public void onSendChatMessage(ChatEvent event) {}

        @Override
        public void onPreTabComplete(TabCompleteEvent event) {}

        @Override
        public void onChunkEvent(ChunkEvent event) {}

        @Override
        public void onBlockChange(BlockChangeEvent event) {}

        @Override
        public void onRenderPass(RenderEvent event) {}

        @Override
        public void onWorldEvent(WorldEvent event) {}

        @Override
        public void onSendPacket(PacketEvent event) {}

        @Override
        public void onReceivePacket(PacketEvent event) {}

        @Override
        public void onPlayerRotationMove(RotationMoveEvent event) {}

        @Override
        public void onPlayerSprintState(SprintStateEvent event) {}

        @Override
        public void onBlockInteract(BlockInteractEvent event) {}

        @Override
        public void onPlayerDeath() {}

        @Override
        public void onPathEvent(PathEvent event) {
            handlePathEvent(event);
        }
    }

    private void pollBaritoneStatus() {
        try {
            if (client.player == null) {
                return;
            }

            IBaritone baritone = BaritoneAPI.getProvider().getPrimaryBaritone();
            IPathingBehavior pathingBehavior = baritone.getPathingBehavior();

            // Only send updates if actively pathing
            if (!pathingBehavior.isPathing()) {
                return;
            }

            Double currentEstimatedTicksToGoal = pathingBehavior.estimatedTicksToGoal().orElse(null);
            Double currentTicksRemainingInSegment = pathingBehavior.ticksRemainingInSegment().orElse(null);

            if (Objects.equals(currentEstimatedTicksToGoal, lastEstimatedTicksToGoal) &&
                Objects.equals(currentTicksRemainingInSegment, lastTicksRemainingInSegment)) {
                return;
            }

            lastEstimatedTicksToGoal = currentEstimatedTicksToGoal;
            lastTicksRemainingInSegment = currentTicksRemainingInSegment;

            sendCurrentStatus(lastPathEvent);
        } catch (Exception e) {
            LOGGER.debug("Error polling Baritone status: {}", e.getMessage());
        }
    }

    private void sendCurrentStatus(PathEvent event) {
        try {
            IBaritone baritone = BaritoneAPI.getProvider().getPrimaryBaritone();
            IPathingBehavior pathingBehavior = baritone.getPathingBehavior();
            IPathingControlManager controlManager = baritone.getPathingControlManager();

            boolean isPathing = pathingBehavior.isPathing();
            Goal goal = pathingBehavior.getGoal();
            String goalDescription = goal != null ? goal.toString() : null;

            BaritoneProcessStatusUpdate status = buildStatusUpdate(
                    event, isPathing, goalDescription,
                    controlManager.mostRecentInControl(), pathingBehavior
            );

            lastPathEvent = event;
            lastIsPathing = isPathing;
            lastGoalDescription = goalDescription;

            sendBaritoneProcessStatusUpdate(status);
            LOGGER.debug("Sent status update: event={}, ETA={}", event, status.getEstimatedTicksToGoal());
        } catch (Exception e) {
            LOGGER.error("Failed to send current status", e);
        }
    }

    private void handlePathEvent(PathEvent event) {
        try {
            LOGGER.debug("Baritone path event: {}", event);

            // Check if player is in world before proceeding
            if (client.player == null) {
                LOGGER.debug("Skipping path event {} - player not in world", event);
                return;
            }

            IBaritone baritone = BaritoneAPI.getProvider().getPrimaryBaritone();
            IPathingBehavior pathingBehavior = baritone.getPathingBehavior();

            // Get current state for duplicate check
            boolean isPathing = pathingBehavior.isPathing();
            Goal goal = pathingBehavior.getGoal();
            String goalDescription = goal != null ? goal.toString() : null;

            // Skip duplicate CANCELED events with no meaningful state change
            if (shouldSkipDuplicateCanceledEvent(event, isPathing, goalDescription)) {
                return;
            }

            // Send status update
            sendCurrentStatus(event);
            LOGGER.info("Sent path event update: {}", event);
        } catch (Exception e) {
            LOGGER.error("Failed to handle path event", e);
        }
    }

    private boolean shouldSkipDuplicateCanceledEvent(PathEvent event, boolean isPathing, String goalDescription) {
        if (event != PathEvent.CANCELED || lastPathEvent != PathEvent.CANCELED) {
            return false;
        }

        // Already sent a CANCELED event - only send another if something meaningful changed
        boolean stateUnchanged = !isPathing && !lastIsPathing &&
                                 Objects.equals(goalDescription, lastGoalDescription);

        if (stateUnchanged) {
            LOGGER.debug("Skipping duplicate CANCELED event");
            return true;
        }

        return false;
    }

    private BaritoneProcessStatusUpdate buildStatusUpdate(
            PathEvent event,
            boolean isPathing,
            String goalDescription,
            Optional<IBaritoneProcess> activeProcess,
            IPathingBehavior pathingBehavior
    ) {
        BaritoneProcessStatusUpdate.Builder builder = BaritoneProcessStatusUpdate.newBuilder()
                .setEventType(toProtoPathEventType(event))
                .setIsPathing(isPathing);

        if (goalDescription != null) {
            builder.setGoalDescription(goalDescription);
        }

        activeProcess.ifPresent(process -> {
            builder.setActiveProcess(BaritoneProcessInfo.newBuilder()
                    .setProcessName(process.getClass().getSimpleName())
                    .setDisplayName(process.displayName())
                    .setPriority(process.priority())
                    .setIsActive(process.isActive())
                    .setIsTemporary(process.isTemporary())
                    .build());
        });

        try {
            pathingBehavior.estimatedTicksToGoal().ifPresent(builder::setEstimatedTicksToGoal);
            pathingBehavior.ticksRemainingInSegment().ifPresent(builder::setTicksRemainingInSegment);
        } catch (Exception e) {
            LOGGER.debug("Could not get progress information: {}", e.getMessage());
        }

        return builder.build();
    }

    private PathEventType toProtoPathEventType(PathEvent event) {
        return switch (event) {
            case CALC_STARTED -> PathEventType.PATH_EVENT_CALC_STARTED;
            case CALC_FINISHED_NOW_EXECUTING -> PathEventType.PATH_EVENT_CALC_FINISHED_NOW_EXECUTING;
            case CALC_FAILED -> PathEventType.PATH_EVENT_CALC_FAILED;
            case NEXT_SEGMENT_CALC_STARTED -> PathEventType.PATH_EVENT_NEXT_SEGMENT_CALC_STARTED;
            case NEXT_SEGMENT_CALC_FINISHED -> PathEventType.PATH_EVENT_NEXT_SEGMENT_CALC_FINISHED;
            case CONTINUING_ONTO_PLANNED_NEXT -> PathEventType.PATH_EVENT_CONTINUING_ONTO_PLANNED_NEXT;
            case SPLICING_ONTO_NEXT_EARLY -> PathEventType.PATH_EVENT_SPLICING_ONTO_NEXT_EARLY;
            case AT_GOAL -> PathEventType.PATH_EVENT_AT_GOAL;
            case PATH_FINISHED_NEXT_STILL_CALCULATING -> PathEventType.PATH_EVENT_PATH_FINISHED_NEXT_STILL_CALCULATING;
            case NEXT_CALC_FAILED -> PathEventType.PATH_EVENT_NEXT_CALC_FAILED;
            case DISCARD_NEXT -> PathEventType.PATH_EVENT_DISCARD_NEXT;
            case CANCELED -> PathEventType.PATH_EVENT_CANCELED;
        };
    }

    private void sendBaritoneProcessStatusUpdate(BaritoneProcessStatusUpdate status) {
        Protocol.ClientToManagerMessage message = Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setBaritoneProcessStatus(status)
                .build();

        connection.sendMessage(message);
    }
}