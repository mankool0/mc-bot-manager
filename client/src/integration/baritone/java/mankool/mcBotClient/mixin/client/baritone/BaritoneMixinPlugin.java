package mankool.mcBotClient.mixin.client.baritone;

import net.fabricmc.loader.api.FabricLoader;
import org.objectweb.asm.tree.ClassNode;
import org.spongepowered.asm.mixin.extensibility.IMixinConfigPlugin;
import org.spongepowered.asm.mixin.extensibility.IMixinInfo;

import java.util.List;
import java.util.Set;

/**
 * Applies the Baritone mixins only when a Baritone mod is loaded. Without this gate, a jar built with
 * Baritone support fails in a game without it, because the target class is missing.
 */
public class BaritoneMixinPlugin implements IMixinConfigPlugin {
    private boolean baritonePresent;

    @Override
    public void onLoad(String mixinPackage) {
        FabricLoader loader = FabricLoader.getInstance();
        baritonePresent = loader.isModLoaded("baritone-meteor") || loader.isModLoaded("baritone");
    }

    @Override
    public boolean shouldApplyMixin(String targetClassName, String mixinClassName) {
        return baritonePresent;
    }

    @Override
    public String getRefMapperConfig() {
        return null;
    }

    @Override
    public void acceptTargets(Set<String> myTargets, Set<String> otherTargets) {
    }

    @Override
    public List<String> getMixins() {
        return null;
    }

    @Override
    public void preApply(String targetClassName, ClassNode targetClass, String mixinClassName, IMixinInfo mixinInfo) {
    }

    @Override
    public void postApply(String targetClassName, ClassNode targetClass, String mixinClassName, IMixinInfo mixinInfo) {
    }
}
