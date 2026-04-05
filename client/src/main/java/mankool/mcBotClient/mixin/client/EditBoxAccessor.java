package mankool.mcBotClient.mixin.client;

import net.minecraft.client.gui.components.EditBox;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(EditBox.class)
public interface EditBoxAccessor {
    @Accessor("isEditable")
    boolean isEditable_();
}
