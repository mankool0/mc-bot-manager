#include "WindowLayout.h"

#include <QSettings>
#include <algorithm>

WindowLayoutSettings WindowLayoutSettings::load()
{
    QSettings settings("MCBotManager", "MCBotManager");
    WindowLayoutSettings s;
    s.enabled = settings.value("BotWindows/enabled", false).toBool();
    s.monitor = settings.value("BotWindows/monitor", "").toString();
    s.columns = std::max(1, settings.value("BotWindows/columns", 3).toInt());
    s.rows = std::max(1, settings.value("BotWindows/rows", 2).toInt());
    s.minimized = settings.value("BotWindows/minimized", false).toBool();
    return s;
}

void WindowLayoutSettings::save() const
{
    QSettings settings("MCBotManager", "MCBotManager");
    settings.setValue("BotWindows/enabled", enabled);
    settings.setValue("BotWindows/monitor", monitor);
    settings.setValue("BotWindows/columns", columns);
    settings.setValue("BotWindows/rows", rows);
    settings.setValue("BotWindows/minimized", minimized);
}

std::optional<mankool::mcbot::protocol::SetWindowCommand> WindowLayout::commandForCell(
    const WindowLayoutSettings &settings,
    const mankool::mcbot::protocol::WindowStateResponse &state,
    int cell)
{
    const auto monitors = state.monitors();
    if (monitors.isEmpty())
        return std::nullopt;

    const mankool::mcbot::protocol::MonitorInfo *target = nullptr;
    for (const auto &m : monitors) {
        if (!settings.monitor.isEmpty() && m.name() == settings.monitor) {
            target = &m;
            break;
        }
        if (settings.monitor.isEmpty() && m.primary())
            target = &m;
    }
    if (!target)
        target = &monitors.first();

    const int cols = std::max(1, settings.columns);
    const int rows = std::max(1, settings.rows);
    const int slot = cell % (cols * rows);
    const int col = slot % cols;
    const int row = slot / cols;
    const int cellW = target->workWidth() / cols;
    const int cellH = target->workHeight() / rows;

    mankool::mcbot::protocol::SetWindowCommand cmd;
    cmd.setMonitor(target->name());
    cmd.setX(col * cellW);
    cmd.setY(row * cellH);
    cmd.setWidth(std::max(1, cellW));
    cmd.setHeight(std::max(1, cellH));
    cmd.setMinimized(settings.minimized);
    return cmd;
}
