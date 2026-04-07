package mankool.mcBotClient.util;

import com.google.protobuf.ByteString;
import mankool.mcbot.protocol.Common;
import net.minecraft.client.Minecraft;
import net.minecraft.core.Holder;
import net.minecraft.core.NonNullList;
import net.minecraft.core.component.DataComponents;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.nbt.NbtOps;
import net.minecraft.nbt.Tag;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.component.ItemContainerContents;
import net.minecraft.world.item.enchantment.Enchantment;
import net.minecraft.world.item.enchantment.ItemEnchantments;
import net.minecraft.world.phys.Vec3;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;

public class ProtoUtil {

    public static Common.ItemStack buildItemStack(ItemStack itemStack, int slot) {
        Integer repairCost = itemStack.getComponents().get(DataComponents.REPAIR_COST);

        Common.ItemStack.Builder builder = Common.ItemStack.newBuilder()
            .setSlot(slot)
            .setItemId(itemStack.getItem().toString())
            .setCount(itemStack.getCount())
            .setDamage(itemStack.getDamageValue())
            .setMaxDamage(itemStack.getMaxDamage())
            .setDisplayName(itemStack.getHoverName().getString())
            .setRepairCost(repairCost != null ? repairCost : 0);

        addEnchantments(itemStack, builder);
        addFullNbt(itemStack, builder);
        addContainerItems(itemStack, builder);

        return builder.build();
    }

    /**
     * Serializes the full item NBT using ItemStack.save()
     *
     * The bytes are the raw compound payload (entries + TAG_End) without the
     * leading type byte or name string, matching what libnbt++ stream_reader
     * read_payload(Compound) expects.
     */
    private static void addFullNbt(ItemStack itemStack, Common.ItemStack.Builder builder) {
        try {
            Minecraft mc = Minecraft.getInstance();
            if (mc.level == null) return;

            var ops = mc.level.registryAccess().createSerializationContext(NbtOps.INSTANCE);
            var result = ItemStack.CODEC.encodeStart(ops, itemStack).result();
            if (result.isEmpty() || !(result.get() instanceof CompoundTag compound)) return;

            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            compound.write(new DataOutputStream(baos));
            builder.setNbt(ByteString.copyFrom(baos.toByteArray()));
        } catch (Exception e) {
            // Fall back to partial data - enchants and damage are still sent
        }
    }

    private static void addEnchantments(ItemStack itemStack, Common.ItemStack.Builder builder) {
        // Stored enchantments (enchanted books)
        ItemEnchantments storedEnchants = itemStack.get(DataComponents.STORED_ENCHANTMENTS);
        if (storedEnchants != null && !storedEnchants.isEmpty()) {
            for (var entry : storedEnchants.entrySet()) {
                Holder<Enchantment> holder = entry.getKey();
                int level = entry.getIntValue();
                holder.unwrapKey().ifPresent(key ->
                    builder.putEnchantments(VersionCompat.keyId(key), level));
            }
            return;
        }

        // Regular enchantments (tools, armor, etc.)
        ItemEnchantments enchants = itemStack.getEnchantments();
        if (!enchants.isEmpty()) {
            for (var entry : enchants.entrySet()) {
                Holder<Enchantment> holder = entry.getKey();
                int level = entry.getIntValue();
                holder.unwrapKey().ifPresent(key ->
                    builder.putEnchantments(VersionCompat.keyId(key), level));
            }
        }
    }

    private static void addContainerItems(ItemStack itemStack, Common.ItemStack.Builder builder) {
        ItemContainerContents container = itemStack.get(DataComponents.CONTAINER);
        if (container == null) {
            return;
        }
        NonNullList<ItemStack> contents = NonNullList.withSize(27, ItemStack.EMPTY);
        container.copyInto(contents);
        for (int i = 0; i < contents.size(); i++) {
            if (!contents.get(i).isEmpty()) {
                builder.addContainerItems(buildItemStack(contents.get(i), i));
            }
        }
    }

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