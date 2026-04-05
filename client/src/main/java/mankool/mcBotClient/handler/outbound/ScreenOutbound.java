package mankool.mcBotClient.handler.outbound;

import mankool.mcBotClient.connection.PipeConnection;
import mankool.mcBotClient.mixin.client.AbstractSignEditScreenAccessor;
import mankool.mcBotClient.mixin.client.EditBoxAccessor;
import net.minecraft.client.gui.screens.multiplayer.ServerSelectionList;
import net.minecraft.client.gui.screens.worldselection.WorldSelectionList;
import net.minecraft.world.level.storage.LevelSummary;
import net.minecraft.client.gui.screens.inventory.AbstractSignEditScreen;
import mankool.mcBotClient.util.ProtoUtil;
import mankool.mcbot.protocol.Protocol;
import mankool.mcbot.protocol.Screen;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.components.AbstractSelectionList;
import net.minecraft.client.gui.components.events.ContainerEventHandler;
import net.minecraft.client.gui.components.events.GuiEventListener;
import net.minecraft.client.gui.layouts.LayoutElement;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.world.inventory.Slot;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;

public class ScreenOutbound extends BaseOutbound {
    private static final Logger LOGGER = LoggerFactory.getLogger(ScreenOutbound.class);

    private static final AtomicInteger screenIdCounter = new AtomicInteger(0);

    private String previousScreenClass = "";
    private String currentScreenId = "";
    private int lastDumpHash = 0;

    private Screen.ScreenDump lastScreenDump = null;

    public record ClickTarget(int centerX, int centerY, boolean active) {}

    public ScreenOutbound(Minecraft client, PipeConnection connection) {
        super(client, connection);
    }

    public String getCurrentScreenId() {
        return currentScreenId;
    }

    public ClickTarget getWidgetClickTarget(int index) {
        if (lastScreenDump == null) return null;
        for (Screen.GuiWidget widget : lastScreenDump.getWidgetsList()) {
            if (widget.getIndex() == index) {
                return new ClickTarget(
                    widget.getX() + widget.getWidth() / 2,
                    widget.getY() + widget.getHeight() / 2,
                    widget.getActive()
                );
            }
        }
        return null;
    }

    @Override
    protected void onClientTick(Minecraft client) {
        String currentScreenClass = client.screen != null ? client.screen.getClass().getName() : "";

        if (!currentScreenClass.equals(previousScreenClass)) {
            lastScreenDump = null;
            lastDumpHash = 0;
            if (currentScreenClass.isEmpty()) {
                currentScreenId = "";
                sendScreenDump(null);
            } else {
                currentScreenId = String.valueOf(screenIdCounter.incrementAndGet());
                sendScreenDump(client.screen);
            }
            previousScreenClass = currentScreenClass;
        } else if (client.screen != null) {
            Screen.ScreenDump dump = buildScreenDump(client.screen);
            int hash = dump.hashCode();
            if (hash != lastDumpHash) {
                lastDumpHash = hash;
                sendDump(dump);
            }
        }
    }

    public void sendScreenDump(net.minecraft.client.gui.screens.Screen mcScreen) {
        if (mcScreen == null) {
            previousScreenClass = "";
            currentScreenId = "";
            lastDumpHash = 0;
            lastScreenDump = null;
            connection.sendMessage(Protocol.ClientToManagerMessage.newBuilder()
                .setMessageId(UUID.randomUUID().toString())
                .setTimestamp(System.currentTimeMillis())
                .setScreen(Screen.ScreenDump.newBuilder().build())
                .build());
            return;
        }

        previousScreenClass = mcScreen.getClass().getName();
        Screen.ScreenDump dump = buildScreenDump(mcScreen);
        lastDumpHash = dump.hashCode();
        sendDump(dump);
    }

    private void sendDump(Screen.ScreenDump dump) {
        connection.sendMessage(Protocol.ClientToManagerMessage.newBuilder()
            .setMessageId(UUID.randomUUID().toString())
            .setTimestamp(System.currentTimeMillis())
            .setScreen(dump)
            .build());
    }

    private Screen.ScreenDump buildScreenDump(net.minecraft.client.gui.screens.Screen mcScreen) {
        Screen.ScreenDump.Builder dump = Screen.ScreenDump.newBuilder();
        dump.setScreenId(currentScreenId);
        dump.setScreenClass(mcScreen.getClass().getName());
        dump.setWidth(mcScreen.width);
        dump.setHeight(mcScreen.height);

        try {
            var titleComponent = mcScreen.getTitle();
            if (titleComponent != null) {
                dump.setTitle(titleComponent.getString());
            }
        } catch (Exception e) {
            LOGGER.debug("Could not get screen title: {}", e.getMessage());
        }

        Set<GuiEventListener> focusedPath = buildFocusedPath(mcScreen);

        try {
            List<? extends GuiEventListener> children = mcScreen.children();
            AtomicInteger idx = new AtomicInteger(0);
            for (GuiEventListener listener : children) {
                collectWidgets(listener, dump, idx, focusedPath);
            }
        } catch (Exception e) {
            LOGGER.error("Failed to extract screen widgets: {}", e.getMessage());
        }

        // For sign edit screens, expose each line as a SignLine widget with its current text
        if (mcScreen instanceof AbstractSignEditScreen) {
            try {
                String[] messages = ((AbstractSignEditScreenAccessor) mcScreen).getMessages();
                if (messages != null) {
                    int idx2 = dump.getWidgetsCount();
                    for (String message : messages) {
                        int i = idx2++;
                        dump.addWidgets(Screen.GuiWidget.newBuilder()
                                .setIndex(i)
                                .setWidgetType("SignLine")
                                .setClassName("SignLine")
                                .setX(0).setY(0).setWidth(0).setHeight(0)
                                .setActive(false).setVisible(true)
                                .setEditValue(message != null ? message : "")
                                .build());
                    }
                }
            } catch (Exception e) {
                LOGGER.debug("Failed to read sign messages: {}", e.getMessage());
            }
        }

        if (mcScreen instanceof AbstractContainerScreen<?> containerScreen) {
            try {
                for (Slot slot : containerScreen.getMenu().slots) {
                    Screen.GuiSlot.Builder slotBuilder = Screen.GuiSlot.newBuilder()
                        .setIndex(slot.index)
                        .setX(slot.x)
                        .setY(slot.y)
                        .setActive(slot.isActive());

                    if (!slot.getItem().isEmpty()) {
                        slotBuilder.setItem(ProtoUtil.buildItemStack(slot.getItem(), slot.index));
                    } else {
                        slotBuilder.setItem(ProtoUtil.buildItemStack(net.minecraft.world.item.ItemStack.EMPTY, slot.index));
                    }

                    dump.addGuiSlots(slotBuilder.build());
                }
            } catch (Exception e) {
                LOGGER.error("Failed to extract container slots: {}", e.getMessage());
            }
        }

        lastScreenDump = dump.build();
        return lastScreenDump;
    }

    private Set<GuiEventListener> buildFocusedPath(net.minecraft.client.gui.screens.Screen mcScreen) {
        Set<GuiEventListener> path = new HashSet<>();
        GuiEventListener current = mcScreen.getFocused();
        while (current != null && path.add(current)) {
            if (current instanceof ContainerEventHandler container) {
                current = container.getFocused();
            } else {
                break;
            }
        }
        return path;
    }

    private void collectWidgets(GuiEventListener listener, Screen.ScreenDump.Builder dump, AtomicInteger idx, Set<GuiEventListener> focusedPath) {
        // Selection lists track their selected entry via getSelected(), not getFocused()
        if (listener instanceof AbstractSelectionList<?> list) {
            GuiEventListener sel = list.getSelected();
            if (sel != null) focusedPath.add(sel);
        }

        if (listener instanceof ContainerEventHandler container) {
            try {
                List<? extends GuiEventListener> children = container.children();
                if (!children.isEmpty()) {
                    for (GuiEventListener child : children) {
                        collectWidgets(child, dump, idx, focusedPath);
                    }
                    return;
                }
            } catch (Exception e) {
                LOGGER.debug("Failed to recurse into container: {}", e.getMessage());
            }
        }

        if (listener instanceof AbstractWidget widget) {
            dump.addWidgets(buildGuiWidget(idx.getAndIncrement(), widget, focusedPath.contains(listener)));
        } else if (listener instanceof LayoutElement element) {
            try {
                String text = "";
                if (listener instanceof ServerSelectionList.OnlineServerEntry serverEntry) {
                    ServerData data = serverEntry.getServerData();
                    text = data.name + " (" + data.ip + ")";
                } else if (listener instanceof WorldSelectionList.WorldListEntry worldEntry) {
                    LevelSummary summary = worldEntry.getLevelSummary();
                    text = summary.getLevelName() + " (" + summary.getLevelId() + ")";
                }
                dump.addWidgets(Screen.GuiWidget.newBuilder()
                    .setIndex(idx.getAndIncrement())
                    .setWidgetType("ListEntry")
                    .setClassName(listener.getClass().getName())
                    .setX(element.getX())
                    .setY(element.getY())
                    .setWidth(element.getWidth())
                    .setHeight(element.getHeight())
                    .setActive(true)
                    .setVisible(true)
                    .setText(text)
                    .setSelected(focusedPath.contains(listener))
                    .build());
            } catch (Exception e) {
                LOGGER.debug("Failed to build ListEntry widget: {}", e.getMessage());
            }
        }
    }

    private Screen.GuiWidget buildGuiWidget(int index, AbstractWidget widget, boolean selected) {
        String simpleType = widget.getClass().getSimpleName();
        String widgetType = switch (widget) {
            case Button b -> "Button";
            case EditBox e -> "EditBox";
            default -> simpleType.isEmpty() ? widget.getClass().getName() : simpleType;
        };

        Screen.GuiWidget.Builder builder = Screen.GuiWidget.newBuilder()
            .setIndex(index)
            .setWidgetType(widgetType)
            .setClassName(widget.getClass().getName())
            .setX(widget.getX())
            .setY(widget.getY())
            .setWidth(widget.getWidth())
            .setHeight(widget.getHeight())
            .setActive(widget.active)
            .setVisible(widget.visible)
            .setSelected(selected);

        try {
            var msg = widget.getMessage();
            if (msg != null) {
                builder.setText(msg.getString());
            }
        } catch (Exception e) {
            LOGGER.debug("Could not get widget message: {}", e.getMessage());
        }

        if (widget instanceof EditBox editBox) {
            try {
                EditBoxAccessor accessor = (EditBoxAccessor) editBox;
                builder.setEditValue(editBox.getValue());
                builder.setEditEditable(accessor.isEditable_());
            } catch (Exception e) {
                LOGGER.debug("Could not access EditBox fields: {}", e.getMessage());
            }
        }

        return builder.build();
    }
}
