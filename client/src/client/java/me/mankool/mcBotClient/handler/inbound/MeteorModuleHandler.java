package me.mankool.mcBotClient.handler.inbound;

import mankool.mcbot.protocol.Meteor.*;
import me.mankool.mcBotClient.connection.PipeConnection;
import meteordevelopment.meteorclient.settings.*;
import meteordevelopment.meteorclient.systems.modules.Module;
import meteordevelopment.meteorclient.systems.modules.Modules;
import meteordevelopment.meteorclient.utils.misc.Keybind;
import meteordevelopment.meteorclient.utils.render.color.SettingColor;
import net.minecraft.block.Block;
import net.minecraft.block.Blocks;
import net.minecraft.block.entity.BlockEntityType;
import net.minecraft.client.MinecraftClient;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.registry.BuiltinRegistries;
import net.minecraft.registry.Registries;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryKeys;
import org.joml.Vector3d;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.lang.reflect.Field;
import java.util.Collection;
import java.util.List;
import java.util.Objects;
import java.util.function.Consumer;
import java.util.stream.Collectors;

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

            String typeName = setting.getClass().getSimpleName();
            if (setting instanceof GenericSetting<?> genericSetting) {
                if (genericSetting.get() instanceof meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData) {
                    typeName = "ESPBlockData";
                }
            }

            LOGGER.info("Setting changed: {}.{} (type: {}) = {}", module.name, settingPath, typeName, setting.get());

            MeteorSettingValue value = toProtoValue(setting);
            if (value == null) {
                LOGGER.warn("Unable to serialize setting value for {}.{} (type: {})", module.name, settingPath, typeName);
                return;
            }

            LOGGER.info("Successfully serialized {}.{} as protobuf field: {}", module.name, settingPath, value.getValueCase());

            ModuleStateChanged notification = ModuleStateChanged.newBuilder()
                    .setModuleName(module.name)
                    .putChangedSettings(settingPath, value)
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
                if (request.hasCategoryFilter() && !module.category.toString().equalsIgnoreCase(request.getCategoryFilter())) {
                    continue;
                }

                ModuleInfo moduleInfo = toProtoModuleInfo(module);
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
                if (command.getEnabled() != module.isActive()) {
                    module.toggle();
                }
            }

            for (var entry : command.getSettingsMap().entrySet()) {
                String settingName = entry.getKey();
                MeteorSettingValue value = entry.getValue();

                if (!applyProtoSetting(module, settingName, value)) {
                    sendModuleConfigResponse(false, "Failed to apply setting: " + settingName, null);
                    return;
                }
            }

            ModuleInfo updatedInfo = toProtoModuleInfo(module);
            sendModuleConfigResponse(true, "Module configured successfully", updatedInfo);

        } catch (Exception e) {
            sendModuleConfigResponse(false, "Error: " + e.getMessage(), null);
        }
    }

    private ModuleInfo toProtoModuleInfo(Module module) {
        ModuleInfo.Builder info = ModuleInfo.newBuilder()
                .setName(module.name)
                .setCategory(module.category.toString())
                .setEnabled(module.isActive())
                .setDescription(module.description);

        for (SettingGroup group : module.settings.groups) {
            for (Setting<?> setting : group) {
                SettingInfo settingInfo = toProtoSettingInfo(setting, group.name);
                if (settingInfo != null) {
                    info.addSettings(settingInfo);
                }
            }
        }

        return info.build();
    }

    private SettingInfo toProtoSettingInfo(Setting<?> setting, String groupName) {
        try {
            SettingInfo.Builder builder = SettingInfo.newBuilder()
                    .setName(setting.name);

            // Set group name if not "General" (default group)
            if (groupName != null && !groupName.equals("General")) {
                builder.setGroupName(groupName);
            }

            // Set description if available
            if (setting.description != null && !setting.description.isEmpty()) {
                builder.setDescription(setting.description);
            }

            // Serialize current value
            MeteorSettingValue currentValue = toProtoValue(setting);
            if (currentValue != null) {
                builder.setCurrentValue(currentValue);
            }

            // Determine setting type and add type-specific info
            SettingInfo.SettingType type = toProtoSettingType(setting);
            builder.setType(type);

            // Add min/max for numeric settings and possible values for registry-backed settings
            switch (setting) {
                case IntSetting intSetting -> {
                    builder.setMinValue(intSetting.min);
                    builder.setMaxValue(intSetting.max);
                }
                case DoubleSetting doubleSetting -> {
                    builder.setMinValue(doubleSetting.min);
                    builder.setMaxValue(doubleSetting.max);
                }
                case EnumSetting<?> enumSetting -> {
                    for (Enum<?> value : enumSetting.get().getClass().getEnumConstants()) {
                        builder.addPossibleValues(value.name());
                    }
                }
                case BlockListSetting blockListSetting -> {
                    for (Block block : Registries.BLOCK) {
                        if (blockListSetting.filter == null || blockListSetting.filter.test(block)) {
                            builder.addPossibleValues(Registries.BLOCK.getId(block).toString());
                        }
                    }
                }
                case ItemListSetting itemListSetting -> {
                    for (net.minecraft.item.Item item : Registries.ITEM) {
                        if (itemListSetting.filter == null || itemListSetting.filter.test(item)) {
                            builder.addPossibleValues(Registries.ITEM.getId(item).toString());
                        }
                    }
                }
                case EntityTypeListSetting entityTypeListSetting -> {
                    for (net.minecraft.entity.EntityType<?> entityType : Registries.ENTITY_TYPE) {
                        if (entityTypeListSetting.filter == null || entityTypeListSetting.filter.test(entityType)) {
                            builder.addPossibleValues(Registries.ENTITY_TYPE.getId(entityType).toString());
                        }
                    }
                }
                case PacketListSetting packetListSetting -> {
                    for (Class<? extends net.minecraft.network.packet.Packet<?>> packet : meteordevelopment.meteorclient.utils.network.PacketUtils.getC2SPackets()) {
                        if (packetListSetting.filter == null || packetListSetting.filter.test(packet)) {
                            builder.addPossibleValues(meteordevelopment.meteorclient.utils.network.PacketUtils.getName(packet));
                        }
                    }
                    for (Class<? extends net.minecraft.network.packet.Packet<?>> packet : meteordevelopment.meteorclient.utils.network.PacketUtils.getS2CPackets()) {
                        if (packetListSetting.filter == null || packetListSetting.filter.test(packet)) {
                            builder.addPossibleValues(meteordevelopment.meteorclient.utils.network.PacketUtils.getName(packet));
                        }
                    }
                }
                case StatusEffectListSetting ignored -> {
                    for (net.minecraft.entity.effect.StatusEffect effect : Registries.STATUS_EFFECT) {
                        builder.addPossibleValues(Registries.STATUS_EFFECT.getId(effect).toString());
                    }
                }
                case ParticleTypeListSetting ignored -> {
                    for (net.minecraft.particle.ParticleType<?> particleType : Registries.PARTICLE_TYPE) {
                        builder.addPossibleValues(Registries.PARTICLE_TYPE.getId(particleType).toString());
                    }
                }
                case ModuleListSetting ignored -> {
                    for (Module module : Modules.get().getAll()) {
                        builder.addPossibleValues(module.name);
                    }
                }
                case EnchantmentListSetting ignored ->
                        BuiltinRegistries.createWrapperLookup()
                                .getOptional(RegistryKeys.ENCHANTMENT)
                                .ifPresent(enchantmentRegistry -> enchantmentRegistry.streamKeys().forEach(key -> builder.addPossibleValues(key.getValue().toString())));
                case StorageBlockListSetting ignored -> {
                    for (BlockEntityType<?> blockEntityType : StorageBlockListSetting.STORAGE_BLOCKS) {
                        net.minecraft.util.Identifier id = Registries.BLOCK_ENTITY_TYPE.getId(blockEntityType);
                        if (id != null) {
                            builder.addPossibleValues(id.toString());
                        }
                    }
                }
                case SoundEventListSetting ignored -> {
                    for (net.minecraft.sound.SoundEvent soundEvent : Registries.SOUND_EVENT) {
                        builder.addPossibleValues(Registries.SOUND_EVENT.getId(soundEvent).toString());
                    }
                }
                case ScreenHandlerListSetting ignored -> {
                    for (net.minecraft.screen.ScreenHandlerType<?> screenHandlerType : Registries.SCREEN_HANDLER) {
                        builder.addPossibleValues(Registries.SCREEN_HANDLER.getId(screenHandlerType).toString());
                    }
                }
                default -> {
                }
            }

            return builder.build();
        } catch (Exception e) {
            LOGGER.warn("Failed to extract setting info for: {}", setting.name, e);
            return null;
        }
    }

    private boolean applyProtoSetting(Module module, String settingPath, MeteorSettingValue protoValue) {
        try {
            String groupName = null;
            String settingName = settingPath;

            // TODO: TEST IF THIS IS EVEN NEEDED
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
                    return applyProtoValue(module.name, settingPath, setting, protoValue);
                }
            }

            LOGGER.error("Setting '{}' not found in module '{}'. Available groups: {}",
                    settingPath, module.name,
                    module.settings.groups.stream().map(g -> g.name).collect(java.util.stream.Collectors.joining(", ")));
            return false;
        } catch (Exception e) {
            LOGGER.error("Failed to apply setting {} to module {}", settingPath, module.name, e);
            return false;
        }
    }

    @SuppressWarnings("unchecked")
    private boolean applyProtoValue(String moduleName, String settingPath, Setting<?> setting, MeteorSettingValue protoValue) {
        try {
            LOGGER.debug("Applying value to {}.{}, setting type: {}, value type: {}",
                    moduleName, settingPath, setting.getClass().getSimpleName(), protoValue.getValueCase());

            switch (setting) {
                case BoolSetting boolSetting -> {
                    if (protoValue.hasBoolValue()) {
                        boolSetting.set(protoValue.getBoolValue());
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected BOOL but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case IntSetting intSetting -> {
                    if (protoValue.hasIntValue()) {
                        intSetting.set(protoValue.getIntValue());
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected INT but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case DoubleSetting doubleSetting -> {
                    if (protoValue.hasDoubleValue()) {
                        doubleSetting.set(protoValue.getDoubleValue());
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected DOUBLE but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case StringSetting stringSetting -> {
                    if (protoValue.hasStringValue()) {
                        stringSetting.set(protoValue.getStringValue());
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected STRING but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case EnumSetting<?> enumSetting -> {
                    if (!protoValue.hasStringValue()) {
                        LOGGER.error("Type mismatch for {}.{}: expected STRING (for ENUM) but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                    String enumName = protoValue.getStringValue();
                    Class<?> enumClass = enumSetting.get().getClass();
                    for (Enum<?> enumValue : (Enum<?>[]) enumClass.getEnumConstants()) {
                        if (enumValue.name().equalsIgnoreCase(enumName)) {
                            ((EnumSetting<Enum<?>>) enumSetting).set(enumValue);
                            return true;
                        }
                    }
                    LOGGER.error("Invalid enum value '{}' for {}.{}. Valid values: {}",
                            enumName, moduleName, settingPath,
                            java.util.Arrays.stream((Enum<?>[]) enumClass.getEnumConstants())
                                    .map(Enum::name)
                                    .collect(java.util.stream.Collectors.joining(", ")));
                    return false;
                }
                case ColorSetting colorSetting -> {
                    if (protoValue.hasColorValue()) {
                        RGBAColor color = protoValue.getColorValue();
                        SettingColor settingColor = colorSetting.get();
                        settingColor.r = color.getRed();
                        settingColor.g = color.getGreen();
                        settingColor.b = color.getBlue();
                        settingColor.a = color.getAlpha();
                        colorSetting.set(settingColor);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected COLOR but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case KeybindSetting keybindSetting -> {
                    if (protoValue.hasKeybindValue()) {
                        mankool.mcbot.protocol.Meteor.Keybind kb = protoValue.getKeybindValue();
                        keybindSetting.set(Keybind.fromKey(net.minecraft.client.util.InputUtil.fromTranslationKey(kb.getKeyName()).getCode()));
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected KEYBIND but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case BlockListSetting blockListSetting -> {
                    if (protoValue.hasBlockListValue()) {
                        List<Block> blocks = protoValue.getBlockListValue().getBlocksList().stream()
                                .map(name -> Registries.BLOCK.get(net.minecraft.util.Identifier.of(name)))
                                .filter(block -> block != Blocks.AIR)
                                .collect(Collectors.toList());
                        blockListSetting.set(blocks);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected BLOCK_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case ItemListSetting itemListSetting -> {
                    if (protoValue.hasItemListValue()) {
                        List<net.minecraft.item.Item> items = protoValue.getItemListValue().getItemsList().stream()
                                .map(name -> Registries.ITEM.get(net.minecraft.util.Identifier.of(name)))
                                .filter(item -> item != net.minecraft.item.Items.AIR)
                                .collect(Collectors.toList());
                        itemListSetting.set(items);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected ITEM_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case EntityTypeListSetting entityTypeListSetting -> {
                    if (protoValue.hasEntityTypeListValue()) {
                        java.util.Set<net.minecraft.entity.EntityType<?>> entities = protoValue.getEntityTypeListValue().getEntityTypesList().stream()
                                .map(name -> Registries.ENTITY_TYPE.get(net.minecraft.util.Identifier.of(name)))
                                .collect(Collectors.toSet());
                        entityTypeListSetting.set(entities);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected ENTITY_TYPE_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case StatusEffectListSetting statusEffectListSetting -> {
                    if (protoValue.hasStatusEffectListValue()) {
                        List<net.minecraft.entity.effect.StatusEffect> effects = protoValue.getStatusEffectListValue().getEffectsList().stream()
                                .map(name -> Registries.STATUS_EFFECT.get(net.minecraft.util.Identifier.of(name)))
                                .collect(Collectors.toList());
                        statusEffectListSetting.set(effects);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected STATUS_EFFECT_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case StringListSetting stringListSetting -> {
                    if (protoValue.hasStringListValue()) {
                        stringListSetting.set(protoValue.getStringListValue().getStringsList());
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected STRING_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case Vector3dSetting vector3dSetting -> {
                    if (protoValue.hasVector3DValue()) {
                        mankool.mcbot.protocol.Meteor.Vector3d vec = protoValue.getVector3DValue();
                        vector3dSetting.set(new Vector3d(vec.getX(), vec.getY(), vec.getZ()));
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected VECTOR3D but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case GenericSetting<?> genericSetting -> {
                    // Only handle ESPBlockData GenericSettings
                    if (genericSetting.get() instanceof meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData espData) {
                        if (protoValue.hasEspBlockDataValue()) {
                            fromProtoESPBlockData(protoValue.getEspBlockDataValue(), espData);
                            genericSetting.onChanged();
                        } else {
                            LOGGER.error("Type mismatch for {}.{}: expected ESP_BLOCK_DATA but got {}", moduleName, settingPath, protoValue.getValueCase());
                            return false;
                        }
                    } else {
                        LOGGER.error("Unsupported GenericSetting type for {}.{}: {}", moduleName, settingPath, genericSetting.get().getClass().getName());
                        return false;
                    }
                }
                case BlockDataSetting<?> blockDataSetting -> {
                    // Only handle ESPBlockData BlockDataSettings
                    // TODO: Untested so far
                    if (protoValue.hasBlockEspConfigMapValue()) {
                        try {
                            java.util.Map<Block, meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData> espMap =
                                    (java.util.Map<Block, meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData>) blockDataSetting.get();

                            espMap.clear();

                            BlockESPConfigMap configMap = protoValue.getBlockEspConfigMapValue();
                            for (var entry : configMap.getConfigsMap().entrySet()) {
                                Block block = Registries.BLOCK.get(net.minecraft.util.Identifier.of(entry.getKey()));
                                if (block != Blocks.AIR) {
                                    meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData espData =
                                            new meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData(
                                                    meteordevelopment.meteorclient.renderer.ShapeMode.Lines,
                                                    new SettingColor(0, 255, 200),
                                                    new SettingColor(0, 255, 200, 25),
                                                    true,
                                                    new SettingColor(0, 255, 200, 125)
                                            );
                                    fromProtoESPBlockData(entry.getValue(), espData);
                                    espMap.put(block, espData);
                                }
                            }
                            blockDataSetting.onChanged();
                        } catch (ClassCastException e) {
                            LOGGER.error("BlockDataSetting for {}.{} is not ESPBlockData type", moduleName, settingPath);
                            return false;
                        }
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected BLOCK_ESP_CONFIG_MAP but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case ParticleTypeListSetting particleTypeListSetting -> {
                    if (protoValue.hasParticleTypeListValue()) {
                        List<net.minecraft.particle.ParticleType<?>> particles = protoValue.getParticleTypeListValue().getParticlesList().stream()
                                .map(name -> Registries.PARTICLE_TYPE.get(net.minecraft.util.Identifier.of(name)))
                                .collect(Collectors.toList());
                        particleTypeListSetting.set(particles);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected PARTICLE_TYPE_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case ModuleListSetting moduleListSetting -> {
                    if (protoValue.hasModuleListValue()) {
                        List<Module> modules = protoValue.getModuleListValue().getModulesList().stream()
                                .map(name -> Modules.get().get(name))
                                .filter(Objects::nonNull)
                                .collect(Collectors.toList());
                        moduleListSetting.set(modules);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected MODULE_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case PacketListSetting packetListSetting -> {
                    if (protoValue.hasPacketListValue()) {
                        java.util.Set<Class<? extends net.minecraft.network.packet.Packet<?>>> packets = protoValue.getPacketListValue().getPacketsList().stream()
                                .map(meteordevelopment.meteorclient.utils.network.PacketUtils::getPacket)
                                .filter(Objects::nonNull)
                                .collect(Collectors.toSet());
                        packetListSetting.set(packets);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected PACKET_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case EnchantmentListSetting enchantmentListSetting -> {
                    if (protoValue.hasEnchantmentListValue()) {
                        java.util.Set<RegistryKey<Enchantment>> enchantments =
                                protoValue.getEnchantmentListValue().getEnchantmentsList().stream()
                                .map(id -> RegistryKey.of(
                                        RegistryKeys.ENCHANTMENT,
                                        net.minecraft.util.Identifier.of(id)))
                                .collect(Collectors.toSet());
                        enchantmentListSetting.set(enchantments);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected ENCHANTMENT_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case StorageBlockListSetting storageBlockListSetting -> {
                    if (protoValue.hasStorageBlockListValue()) {
                        List<BlockEntityType<?>> storageBlocks =
                                protoValue.getStorageBlockListValue().getStorageBlocksList().stream()
                                .map(name -> Registries.BLOCK_ENTITY_TYPE.get(net.minecraft.util.Identifier.of(name)))
                                .collect(Collectors.toList());
                        storageBlockListSetting.set(storageBlocks);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected STORAGE_BLOCK_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case SoundEventListSetting soundEventListSetting -> {
                    if (protoValue.hasSoundEventListValue()) {
                        List<net.minecraft.sound.SoundEvent> sounds = protoValue.getSoundEventListValue().getSoundsList().stream()
                                .map(name -> Registries.SOUND_EVENT.get(net.minecraft.util.Identifier.of(name)))
                                .collect(Collectors.toList());
                        soundEventListSetting.set(sounds);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected SOUND_EVENT_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                case ScreenHandlerListSetting screenHandlerListSetting -> {
                    if (protoValue.hasScreenHandlerListValue()) {
                        List<net.minecraft.screen.ScreenHandlerType<?>> handlers = protoValue.getScreenHandlerListValue().getHandlersList().stream()
                                .map(name -> Registries.SCREEN_HANDLER.get(net.minecraft.util.Identifier.of(name)))
                                .collect(Collectors.toList());
                        screenHandlerListSetting.set(handlers);
                    } else {
                        LOGGER.error("Type mismatch for {}.{}: expected SCREEN_HANDLER_LIST but got {}", moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
                default -> {
                    // Try to parse as string for unknown types
                    if (protoValue.hasStringValue()) {
                        LOGGER.debug("Using generic string parsing for {}.{} (type: {})", moduleName, settingPath, setting.getClass().getSimpleName());
                        setting.parse(protoValue.getStringValue());
                    } else {
                        LOGGER.error("Unsupported setting type {} for {}.{}, value type: {}",
                                setting.getClass().getSimpleName(), moduleName, settingPath, protoValue.getValueCase());
                        return false;
                    }
                }
            }
            return true;
        } catch (Exception e) {
            LOGGER.error("Failed to apply value for setting {}", setting.name, e);
            return false;
        }
    }

    private <T> SettingInfo.SettingType toProtoSettingType(Setting<T> setting) {
        return switch (setting) {
            case BoolSetting ignored -> SettingInfo.SettingType.BOOLEAN;
            case IntSetting ignored -> SettingInfo.SettingType.INTEGER;
            case DoubleSetting ignored -> SettingInfo.SettingType.DOUBLE;
            case StringSetting ignored -> SettingInfo.SettingType.STRING;
            case EnumSetting<?> ignored -> SettingInfo.SettingType.ENUM;
            case ColorSetting ignored -> SettingInfo.SettingType.COLOR;
            case KeybindSetting ignored -> SettingInfo.SettingType.KEYBIND;
            case Vector3dSetting ignored -> SettingInfo.SettingType.VECTOR3D;
            case BlockListSetting ignored -> SettingInfo.SettingType.BLOCK_LIST;
            case ItemListSetting ignored -> SettingInfo.SettingType.ITEM_LIST;
            case EntityTypeListSetting ignored -> SettingInfo.SettingType.ENTITY_TYPE_LIST;
            case StatusEffectListSetting ignored -> SettingInfo.SettingType.STATUS_EFFECT_LIST;
            case ParticleTypeListSetting ignored -> SettingInfo.SettingType.PARTICLE_TYPE_LIST;
            case ModuleListSetting ignored -> SettingInfo.SettingType.MODULE_LIST;
            case PacketListSetting ignored -> SettingInfo.SettingType.PACKET_LIST;
            case EnchantmentListSetting ignored -> SettingInfo.SettingType.ENCHANTMENT_LIST;
            case StorageBlockListSetting ignored -> SettingInfo.SettingType.STORAGE_BLOCK_LIST;
            case SoundEventListSetting ignored -> SettingInfo.SettingType.SOUND_EVENT_LIST;
            case ScreenHandlerListSetting ignored -> SettingInfo.SettingType.SCREEN_HANDLER_LIST;
            case StringListSetting ignored -> SettingInfo.SettingType.STRING_LIST;
            case GenericSetting<?> genericSetting -> {
                // Check if this is an ESPBlockData GenericSetting
                if (genericSetting.get() instanceof meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData) {
                    yield SettingInfo.SettingType.ESP_BLOCK_DATA;
                }
                yield SettingInfo.SettingType.GENERIC;
            }
            case BlockDataSetting<?> ignored -> SettingInfo.SettingType.BLOCK_ESP_CONFIG_MAP;
            default -> SettingInfo.SettingType.GENERIC;
        };
    }

    @SuppressWarnings("unchecked")
    private MeteorSettingValue toProtoValue(Setting<?> setting) {
        try {
            MeteorSettingValue.Builder builder = MeteorSettingValue.newBuilder();

            switch (setting) {
                case BoolSetting boolSetting -> builder.setBoolValue(boolSetting.get());
                case IntSetting intSetting -> builder.setIntValue(intSetting.get());
                case DoubleSetting doubleSetting -> builder.setDoubleValue(doubleSetting.get());
                case StringSetting stringSetting -> builder.setStringValue(stringSetting.get());
                case EnumSetting<?> enumSetting -> builder.setStringValue(enumSetting.get().name());
                case ColorSetting colorSetting -> {
                    SettingColor color = colorSetting.get();
                    builder.setColorValue(RGBAColor.newBuilder()
                            .setRed(color.r)
                            .setGreen(color.g)
                            .setBlue(color.b)
                            .setAlpha(color.a)
                            .build());
                }
                case KeybindSetting keybindSetting -> {
                    Keybind kb = keybindSetting.get();
                    builder.setKeybindValue(mankool.mcbot.protocol.Meteor.Keybind.newBuilder()
                            .setKeyName(net.minecraft.client.util.InputUtil.Type.KEYSYM.createFromCode(kb.getValue()).getTranslationKey())
                            .build());
                }
                case BlockListSetting blockListSetting -> {
                    List<String> blockIds = blockListSetting.get().stream()
                            .map(block -> Registries.BLOCK.getId(block).toString())
                            .collect(Collectors.toList());
                    builder.setBlockListValue(BlockList.newBuilder().addAllBlocks(blockIds).build());
                }
                case ItemListSetting itemListSetting -> {
                    List<String> itemIds = itemListSetting.get().stream()
                            .map(item -> Registries.ITEM.getId(item).toString())
                            .collect(Collectors.toList());
                    builder.setItemListValue(ItemList.newBuilder().addAllItems(itemIds).build());
                }
                case EntityTypeListSetting entityTypeListSetting -> {
                    List<String> entityIds = entityTypeListSetting.get().stream()
                            .map(type -> Registries.ENTITY_TYPE.getId(type).toString())
                            .collect(Collectors.toList());
                    builder.setEntityTypeListValue(EntityTypeList.newBuilder().addAllEntityTypes(entityIds).build());
                }
                case StatusEffectListSetting statusEffectListSetting -> {
                    List<String> effectIds = statusEffectListSetting.get().stream()
                            .map(effect -> Registries.STATUS_EFFECT.getId(effect).toString())
                            .collect(Collectors.toList());
                    builder.setStatusEffectListValue(StatusEffectList.newBuilder().addAllEffects(effectIds).build());
                }
                case ParticleTypeListSetting particleTypeListSetting -> {
                    List<String> particleIds = particleTypeListSetting.get().stream()
                            .map(particle -> Registries.PARTICLE_TYPE.getId(particle).toString())
                            .collect(Collectors.toList());
                    builder.setParticleTypeListValue(ParticleTypeList.newBuilder().addAllParticles(particleIds).build());
                }
                case ModuleListSetting moduleListSetting -> {
                    List<String> moduleNames = moduleListSetting.get().stream()
                            .map(module -> module.name)
                            .collect(Collectors.toList());
                    builder.setModuleListValue(ModuleList.newBuilder().addAllModules(moduleNames).build());
                }
                case PacketListSetting packetListSetting -> {
                    List<String> packetNames = packetListSetting.get().stream()
                            .map(meteordevelopment.meteorclient.utils.network.PacketUtils::getName)
                            .collect(Collectors.toList());
                    builder.setPacketListValue(PacketList.newBuilder().addAllPackets(packetNames).build());
                }
                case EnchantmentListSetting enchantmentListSetting -> {
                    List<String> enchantmentIds = enchantmentListSetting.get().stream()
                            .map(key -> key.getValue().toString())
                            .collect(Collectors.toList());
                    builder.setEnchantmentListValue(EnchantmentList.newBuilder().addAllEnchantments(enchantmentIds).build());
                }
                case StorageBlockListSetting storageBlockListSetting -> {
                    List<String> blockIds = storageBlockListSetting.get().stream()
                            .map(blockEntityType -> Registries.BLOCK_ENTITY_TYPE.getId(blockEntityType).toString())
                            .collect(Collectors.toList());
                    builder.setStorageBlockListValue(StorageBlockList.newBuilder().addAllStorageBlocks(blockIds).build());
                }
                case SoundEventListSetting soundEventListSetting -> {
                    List<String> soundIds = soundEventListSetting.get().stream()
                            .map(sound -> Registries.SOUND_EVENT.getId(sound).toString())
                            .collect(Collectors.toList());
                    builder.setSoundEventListValue(SoundEventList.newBuilder().addAllSounds(soundIds).build());
                }
                case ScreenHandlerListSetting screenHandlerListSetting -> {
                    List<String> handlerIds = screenHandlerListSetting.get().stream()
                            .map(handler -> Registries.SCREEN_HANDLER.getId(handler).toString())
                            .collect(Collectors.toList());
                    builder.setScreenHandlerListValue(ScreenHandlerList.newBuilder().addAllHandlers(handlerIds).build());
                }
                case StringListSetting stringListSetting -> builder.setStringListValue(MeteorStringList.newBuilder()
                        .addAllStrings(stringListSetting.get())
                        .build());
                case Vector3dSetting vector3dSetting -> {
                    Vector3d vec = vector3dSetting.get();
                    builder.setVector3DValue(mankool.mcbot.protocol.Meteor.Vector3d.newBuilder()
                            .setX(vec.x)
                            .setY(vec.y)
                            .setZ(vec.z)
                            .build());
                }
                case GenericSetting<?> genericSetting -> {
                    // Only handle ESPBlockData GenericSettings
                    if (genericSetting.get() instanceof meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData espData) {
                        builder.setEspBlockDataValue(toProtoESPBlockData(espData));
                    } else {
                        LOGGER.debug("Skipping non-ESP GenericSetting: {}", setting.name);
                        return null;
                    }
                }
                case BlockDataSetting<?> blockDataSetting -> {
                    // Only handle ESPBlockData BlockDataSettings
                    try {
                        java.util.Map<Block, meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData> espMap =
                                (java.util.Map<Block, meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData>) blockDataSetting.get();

                        BlockESPConfigMap.Builder mapBuilder = BlockESPConfigMap.newBuilder();
                        for (var entry : espMap.entrySet()) {
                            String blockId = Registries.BLOCK.getId(entry.getKey()).toString();
                            mapBuilder.putConfigs(blockId, toProtoESPBlockData(entry.getValue()));
                        }
                        builder.setBlockEspConfigMapValue(mapBuilder.build());
                    } catch (ClassCastException e) {
                        LOGGER.debug("Skipping non-ESP BlockDataSetting: {}", setting.name);
                        return null;
                    }
                }
                default -> {
                    LOGGER.warn("Unsupported setting type for serialization: {}", setting.getClass().getSimpleName());
                    return null;
                }
            }

            return builder.build();
        } catch (Exception e) {
            LOGGER.error("Failed to serialize setting {}", setting.name, e);
            return null;
        }
    }

    private ESPBlockData toProtoESPBlockData(meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData espData) {
        ESPBlockData.ShapeMode protoShapeMode = switch (espData.shapeMode) {
            case Lines -> ESPBlockData.ShapeMode.LINES;
            case Sides -> ESPBlockData.ShapeMode.SIDES;
            case Both -> ESPBlockData.ShapeMode.BOTH;
        };

        return ESPBlockData.newBuilder()
                .setShapeMode(protoShapeMode)
                .setLineColor(RGBAColor.newBuilder()
                        .setRed(espData.lineColor.r)
                        .setGreen(espData.lineColor.g)
                        .setBlue(espData.lineColor.b)
                        .setAlpha(espData.lineColor.a)
                        .build())
                .setSideColor(RGBAColor.newBuilder()
                        .setRed(espData.sideColor.r)
                        .setGreen(espData.sideColor.g)
                        .setBlue(espData.sideColor.b)
                        .setAlpha(espData.sideColor.a)
                        .build())
                .setTracer(espData.tracer)
                .setTracerColor(RGBAColor.newBuilder()
                        .setRed(espData.tracerColor.r)
                        .setGreen(espData.tracerColor.g)
                        .setBlue(espData.tracerColor.b)
                        .setAlpha(espData.tracerColor.a)
                        .build())
                .build();
    }

    private void fromProtoESPBlockData(ESPBlockData protoData, meteordevelopment.meteorclient.systems.modules.render.blockesp.ESPBlockData espData) {
        espData.shapeMode = switch (protoData.getShapeMode()) {
            case LINES -> meteordevelopment.meteorclient.renderer.ShapeMode.Lines;
            case SIDES -> meteordevelopment.meteorclient.renderer.ShapeMode.Sides;
            case BOTH -> meteordevelopment.meteorclient.renderer.ShapeMode.Both;
            case UNRECOGNIZED -> {
                LOGGER.warn("Unrecognized ShapeMode, defaulting to Lines");
                yield meteordevelopment.meteorclient.renderer.ShapeMode.Lines;
            }
        };

        RGBAColor lineColor = protoData.getLineColor();
        espData.lineColor.r = lineColor.getRed();
        espData.lineColor.g = lineColor.getGreen();
        espData.lineColor.b = lineColor.getBlue();
        espData.lineColor.a = lineColor.getAlpha();

        RGBAColor sideColor = protoData.getSideColor();
        espData.sideColor.r = sideColor.getRed();
        espData.sideColor.g = sideColor.getGreen();
        espData.sideColor.b = sideColor.getBlue();
        espData.sideColor.a = sideColor.getAlpha();

        espData.tracer = protoData.getTracer();

        RGBAColor tracerColor = protoData.getTracerColor();
        espData.tracerColor.r = tracerColor.getRed();
        espData.tracerColor.g = tracerColor.getGreen();
        espData.tracerColor.b = tracerColor.getBlue();
        espData.tracerColor.a = tracerColor.getAlpha();
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

        LOGGER.info("sendModuleStateChanged: {}", message);
        connection.sendMessage(message);
    }
}