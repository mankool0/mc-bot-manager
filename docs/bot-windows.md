# Bot Windows

The client mod changes how each bot's game window is created so that launching bots does not
interrupt whatever you are doing, and so that the windows can be told apart. The manager can then
tile them (see below). The Prism hook does the same for Prism Launcher's own windows.

## What the mod does

- **Window class**: every bot window is created with the window class `mcbot-<account>`
  (lower-cased account name). This is the `app_id` on Wayland and the `WM_CLASS` on X11, and is
  what window rules and tiling tools match on. Windows and macOS have no equivalent; there the
  window is identified by its process id.
- **Title**: the account name is appended to the title, e.g.
  `Minecraft 1.21.11 - Multiplayer (3rd-party) [BotName]`.
- **No focus on create**: the window is created with GLFW's `FOCUSED` and `FOCUS_ON_SHOW` hints
  off, so the game never asks for focus itself. Windows honours that outright. On X11 and XWayland
  the window manager has the final say over freshly mapped windows, and some (KWin at its default
  focus-stealing level) still activate them - see the KDE rule below. Native Wayland compositors
  ignore the hints entirely.
- **Shown in place**: the window is created hidden and stays that way until the manager has sent
  its placement, which is what maps it. A new bot therefore appears straight in its cell on the
  bot monitor (or, with tiling off, wherever the window manager puts it) instead of landing on the
  main screen first and being moved a moment later. The game loads behind the scenes meanwhile;
  the manager still shows the bot starting. Should the manager never answer, the window shows
  itself after 10 seconds.
- **X11 over Wayland**: on a Wayland desktop the game runs under XWayland rather than as a native
  Wayland client, because Wayland does not let applications position their own windows and the
  manager's tiling would be a no-op. Minecraft 26.1+ already prefers X11 on its own; the mod extends
  that to 1.21.x.

JVM arguments (Prism Launcher -> instance -> Settings -> Java arguments) to change the defaults:

| Argument | Effect |
|---|---|
| `-Dmcbot.window.focus=true` | Keep GLFW's default behaviour where a new window takes focus |
| `-Dmcbot.window.deferShow=false` | Map the window as soon as the game creates it instead of waiting for the manager's placement |
| `-Dmcbot.window.nativeWayland=true` | Let GLFW pick native Wayland on 1.21.x. Window placement will not work |

The window class and title are always applied.

## Prism Launcher

Prism opens windows of its own: the main window when the manager starts it, and on every bot
launch a progress dialog plus, if Prism's *Show console while the game is running* option is on,
the instance console. With the Prism hook enabled (Settings -> Prism Launcher -> **Enable Prism
hook**) the hook library marks every top-level Prism window as *show without activating* before
it appears. Qt turns that into `_NET_WM_USER_TIME = 0` on X11, a no-activate show on Windows and
no activation request on Wayland, so Prism does not ask for focus when it opens a window.

The same limits as for the game windows apply: the window manager decides in the end. KWin on
Wayland at its default focus-stealing level activates every new window regardless, and on X11 Qt
asks the window manager to activate a modal dialog (the launch progress dialog) once it is
mapped, which KWin at its default level grants. See the KDE section below for what helps where.

Prism's windows carry the application id `org.prismlauncher.PrismLauncher` (the `app_id` on
Wayland; the X11 class is `prismlauncher`).

To keep Prism off the screen altogether, enable **Keep Prism windows minimized** next to the hook
setting (takes effect the next time the manager starts Prism). Every window Prism opens then
starts minimized: the main window, the launch progress dialog, the instance console, and also any
error or login prompt, so check the taskbar if a launch seems stuck. Bring Prism up from the
taskbar when you need it; on X11 and Windows the manager's *Launch Prism Launcher* action restores
it as well, on Wayland the compositor only lets it flag the taskbar entry. Prism's output is in the
manager's log either way.

Set `MCBM_PRISM_FOCUS=1` in the manager's environment to keep Qt's default behaviour. Prism
inherits it from the manager, Flatpak included. Without the hook Prism is left alone entirely.

## Auto-tiling from the manager

Settings -> Bots -> **Game Windows**:

- **Tile bot windows on connect** - when a bot connects, the manager moves and resizes its window
  into its cell of a grid on the chosen monitor. The window is still hidden at that point and
  appears directly in the cell. A bot's cell is its position in the bot list
  (first bot = top-left, then row by row), so every bot has a fixed slot: a crashed bot leaves
  its cell empty and takes it back when it relaunches, and nothing else moves. With more bots
  than cells the numbering wraps and windows overlap.
- **Monitor** - a monitor name as the game reports it. The list holds the names reported by online
  bots plus the desktop's own screen names; "Primary monitor" works without knowing any.
- **Columns / Rows** - the grid. 3 x 2 on a 1920x1080 work area gives 640x540 windows.
- **Minimize after placing** - iconify each window once it is in its cell.

Changing the settings re-tiles every online bot immediately, as does removing a bot from the list
(the bots after it move up a cell). Scripts get the same control through
[`bot.window()` and `bot.set_window()`](api/bot.md#game-window).

Placement needs a platform where applications may move their own windows: Windows, X11 and
XWayland. On native Wayland (1.21.x with `-Dmcbot.window.nativeWayland=true`, or GNOME without
XWayland) the manager can still resize and minimize, and logs a warning about the rest.

## KDE Plasma on Wayland

KWin decides whether a newly mapped window is activated, for XWayland windows too. If bot windows
still grab focus, add a window rule once and it applies to every bot:

1. System Settings -> Window Management -> Window Rules -> Add New.
2. Window class (application): **Regular Expression** `^mcbot-`.
   Match whole window class: No.
3. Add Property -> **Focus stealing prevention** -> Force -> **Extreme**.
4. Apply.

Bot windows now open behind whatever you are working in. The game runs under XWayland, so the
manager's auto-tiling works as on any X11 desktop; KWin rules with **Screen**, **Size**, or
**Position** properties (*Apply Initially*) are an alternative for static layouts, one rule per bot
matching the exact class `mcbot-<account>`.

Prism is a different case, because it runs as a native Wayland client, and for those KWin only
consults the per-window rule once the *global* level (System Settings -> Window Management ->
Window Behavior -> **Focus stealing prevention**) is at least **Medium**. At the default **Low** it
activates every new Wayland window before looking at rules, so a Prism rule alone changes nothing
there (the game windows are XWayland windows, which take the X11 path where the rule does work).
The options:

- **Keep Prism windows minimized** in the manager (see above) sidesteps activation entirely: a
  window that asks to start minimized is never activated. This works at any level.
- Or raise the global level to **Medium**, which only affects windows that open without a fresh
  activation token (applications you start yourself still get focus), and add a rule for Prism:
  window class **Regular Expression** `(?i)prismlauncher`, **Focus stealing prevention** -> Force
  -> **High**. Prism's windows then open under whatever you are working in, on the screen KWin
  picks. Give the same rule a **Screen** property (*Apply Initially*) pointing at the bot monitor
  to keep them off the main screen.

## Other compositors

- **GNOME (Wayland)**: no window rules and no public placement API; GNOME applies its own
  focus-stealing heuristics and may show a "window is ready" notification instead of raising the
  window.
- **Sway / i3**: `no_focus [app_id="^mcbot-"]` keeps focus where it is;
  `for_window [app_id="^mcbot-"] move to output DP-1` and friends handle placement
  (`class` instead of `app_id` on i3). `no_focus [app_id="^org.prismlauncher.PrismLauncher$"]`
  does the same for Prism.
- **X11 window managers**: match `WM_CLASS` `mcbot-<account>` (and `prismlauncher`) in the WM's
  rules; tools like `xdotool search --class mcbot-` and `wmctrl` also see the windows.
