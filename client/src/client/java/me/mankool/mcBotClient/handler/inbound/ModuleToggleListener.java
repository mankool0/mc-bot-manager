package me.mankool.mcBotClient.handler.inbound;

import meteordevelopment.meteorclient.events.meteor.ActiveModulesChangedEvent;
import meteordevelopment.meteorclient.systems.modules.Module;
import meteordevelopment.meteorclient.systems.modules.Modules;
import meteordevelopment.orbit.EventHandler;

import java.util.HashMap;
import java.util.Map;

public class ModuleToggleListener {
    private final MeteorModuleHandler handler;
    private final Map<Module, Boolean> lastModuleStates = new HashMap<>();

    public ModuleToggleListener(MeteorModuleHandler handler) {
        this.handler = handler;

        for (Module module : Modules.get().getAll()) {
            lastModuleStates.put(module, module.isActive());
        }
    }

    @EventHandler
    private void onModulesChanged(ActiveModulesChangedEvent event) {
        for (Module module : Modules.get().getAll()) {
            boolean currentState = module.isActive();
            Boolean lastState = lastModuleStates.get(module);

            if (lastState == null || lastState != currentState) {
                lastModuleStates.put(module, currentState);
                handler.notifyModuleToggled(module);
            }
        }
    }
}