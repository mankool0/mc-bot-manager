#ifndef WINDOWLAYOUT_H
#define WINDOWLAYOUT_H

#include <QString>
#include <QStringList>
#include <optional>

#include "window.qpb.h"

// Fleet-wide auto-tiling settings for bot game windows (QSettings "BotWindows/*").
struct WindowLayoutSettings {
    bool enabled = false;
    QString monitor;        // GLFW monitor name as reported by the mod; empty = primary
    int columns = 3;
    int rows = 2;
    bool minimized = false; // Iconify after placing

    static WindowLayoutSettings load();
    void save() const;
};

// Turns a bot's grid cell into a SetWindowCommand. The cell is the bot's index in the manager's
// bot list, so every bot has a fixed slot: a crashed bot's cell stays empty until it relaunches
// and takes it back. With more bots than cells the numbering wraps, so windows overlap rather
// than spill off the monitor.
namespace WindowLayout {

// Frame rect for the cell on the settings' monitor, looked up in the monitors the bot reported.
// Returns nullopt if the bot reported no monitors at all.
std::optional<mankool::mcbot::protocol::SetWindowCommand> commandForCell(
    const WindowLayoutSettings &settings,
    const mankool::mcbot::protocol::WindowStateResponse &state,
    int cell);

}

#endif // WINDOWLAYOUT_H
