package mankool.mcBotClient.util;

import mankool.mcbot.protocol.Common;
import net.minecraft.core.Holder;
import net.minecraft.core.component.DataComponents;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.enchantment.Enchantment;
import net.minecraft.world.item.enchantment.ItemEnchantments;
import net.minecraft.world.phys.Vec3;

public class ProtoUtil {

    public static Common.ItemStack buildItemStack(ItemStack itemStack, int slot) {
        Common.ItemStack.Builder builder = Common.ItemStack.newBuilder()
            .setSlot(slot)
            .setItemId(itemStack.getItem().toString())
            .setCount(itemStack.getCount())
            .setDamage(itemStack.getDamageValue())
            .setMaxDamage(itemStack.getMaxDamage())
            .setDisplayName(itemStack.getHoverName().getString());

        addEnchantments(itemStack, builder);

        return builder.build();
    }

    private static void addEnchantments(ItemStack itemStack, Common.ItemStack.Builder builder) {
        // Stored enchantments (enchanted books)
        ItemEnchantments storedEnchants = itemStack.get(DataComponents.STORED_ENCHANTMENTS);
        if (storedEnchants != null && !storedEnchants.isEmpty()) {
            for (var entry : storedEnchants.entrySet()) {
                Holder<Enchantment> holder = entry.getKey();
                int level = entry.getIntValue();
                holder.unwrapKey().ifPresent(key ->
                    builder.addEnchantments(key.identifier().toString() + " " + level));
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
                    builder.addEnchantments(key.identifier().toString() + " " + level));
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