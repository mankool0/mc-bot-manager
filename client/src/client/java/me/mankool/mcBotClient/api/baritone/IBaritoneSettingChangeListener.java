package me.mankool.mcBotClient.api.baritone;

public interface IBaritoneSettingChangeListener {

    void mcBotClient$setChangeListener(SettingChangeCallback listener);
    SettingChangeCallback mcBotClient$getChangeListener();
    void mcBotClient$setValue(Object newValue);
    boolean mcBotClient$checkAndNotifyIfChanged();

    @FunctionalInterface
    interface SettingChangeCallback {
        void onSettingChanged(Object oldValue, Object newValue);
    }
}
