#ifndef WINDOWLAYOUT_H
#define WINDOWLAYOUT_H

#include <QSet>
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

// Grid cells for bot game windows. Cells are handed out among the connected bots, not taken from
// the list position directly: with more bots configured than cells, list positions wrap and two
// running bots would share a cell while another cell stays empty.
namespace WindowLayout {

// The cell a bot takes, given the cells other connected bots hold: the one it held before if still
// free (a relaunched bot gets its spot back), else its list position, else the lowest free cell.
// Only with every cell held does it share one, the one at its list position.
int pickCell(int cellCount, int previous, int listIndex, const QSet<int> &taken);

// Frame rect for the cell on the settings' monitor, looked up in the monitors the bot reported.
// Returns nullopt if the bot reported no monitors at all.
std::optional<mankool::mcbot::protocol::SetWindowCommand> commandForCell(
    const WindowLayoutSettings &settings,
    const mankool::mcbot::protocol::WindowStateResponse &state,
    int cell);

}

#endif // WINDOWLAYOUT_H
