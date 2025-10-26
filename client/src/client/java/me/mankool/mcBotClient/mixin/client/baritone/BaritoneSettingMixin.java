package me.mankool.mcBotClient.mixin.client.baritone;

import baritone.api.Settings;
import me.mankool.mcBotClient.api.baritone.IBaritoneSettingChangeListener;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.util.Objects;

@Mixin(value = Settings.Setting.class, remap = false)
public abstract class BaritoneSettingMixin implements IBaritoneSettingChangeListener {

    @Shadow
    public Object value;

    @Unique
    private SettingChangeCallback mcBotClient$changeListener;

    @Unique
    private Object mcBotClient$lastKnownValue;

    @Unique
    private Object mcBotClient$valueBeforeReset;

    @Override
    public void mcBotClient$setChangeListener(SettingChangeCallback listener) {
        this.mcBotClient$changeListener = listener;
        // Initialize last known value when listener is set
        this.mcBotClient$lastKnownValue = value;
    }

    @Override
    public SettingChangeCallback mcBotClient$getChangeListener() {
        return this.mcBotClient$changeListener;
    }

    @Unique
    public void mcBotClient$setValue(Object newValue) {
        Object oldValue = this.value;
        this.value = newValue;
        this.mcBotClient$lastKnownValue = newValue;

        if (mcBotClient$changeListener != null) {
            mcBotClient$changeListener.onSettingChanged(oldValue, newValue);
        }
    }

    /**
     * Check if value changed since last check and notify listener if so
     * @return true if value changed
     */
    @Unique
    public boolean mcBotClient$checkAndNotifyIfChanged() {
        if (mcBotClient$changeListener == null) {
            return false;
        }

        try {
            boolean changed = !Objects.equals(mcBotClient$lastKnownValue, value);

            if (changed) {
                Object oldValue = mcBotClient$lastKnownValue;
                mcBotClient$lastKnownValue = value;

                try {
                    mcBotClient$changeListener.onSettingChanged(oldValue, value);
                } catch (Exception e) {
                    // Silently ignore listener exceptions to avoid breaking polling
                }
                return true;
            }

            return false;
        } catch (Exception e) {
            // Don't update lastKnownValue on error - retry next poll
            return false;
        }
    }

    @Inject(method = "reset", at = @At("HEAD"), remap = false)
    private void onResetBefore(CallbackInfo ci) {
        mcBotClient$valueBeforeReset = value;
    }

    @Inject(method = "reset", at = @At("TAIL"), remap = false)
    private void onResetAfter(CallbackInfo ci) {
        mcBotClient$lastKnownValue = value;
        if (mcBotClient$changeListener != null) {
            mcBotClient$changeListener.onSettingChanged(mcBotClient$valueBeforeReset, value);
        }
        mcBotClient$valueBeforeReset = null;
    }
}
