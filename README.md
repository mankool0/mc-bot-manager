<p align="center">
<img src="manager/icons/mc-bot-manager.svg" width="25%"/>
</p>

<h1 align="center">MC Bot Manager</h1>

![MC Bot Manager](docs/screenshots/manager.png)

A desktop application for managing and automating multiple Minecraft clients. Each client runs a Fabric mod that connects back to the manager, which can then control it, and run Python scripts against it.

## Architecture

Two components communicate over Protocol Buffers via Unix domain sockets:

- **Client** (`/client/`) - Fabric mod (Java) that runs inside Minecraft, captures game state, and executes commands
- **Manager** (`/manager/`) - Qt/C++ desktop application for controlling bots and running scripts

## Requirements

**Manager:**
- CMake 3.16+
- Qt6 (Widgets, Network, Protobuf, Concurrent, WebEngineWidgets)
- [esbuild](https://esbuild.github.io/) (must be on `PATH`, used to bundle the script editor)
- Python 3 with development headers (optional - auto-downloaded via [python-build-standalone](https://github.com/astral-sh/python-build-standalone) if not found)
- pybind11, libnbtplusplus, Monaco Editor, and PrismLauncher headers are fetched automatically by CMake

**Client:**
- JDK 21+
- Minecraft 1.21.11 with Fabric Loader
- [Meteor Client](https://meteorclient.com/)
- [Baritone](https://github.com/cabaletta/baritone) (for pathfinding)

## Building

**Manager:**
```bash
cd manager
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This also builds `libprismhook.so` / `libprismhook_core.so` for Prism Launcher integration. The PrismLauncher source is fetched automatically by CMake to provide the headers needed to compile the hook.

**Client:**
```bash
cd client
./gradlew build
```

Place the resulting `.jar` from `client/build/libs/` into your Minecraft mods folder alongside Meteor Client and Baritone.

## Prism Launcher Integration

The manager integrates with [Prism Launcher](https://prismlauncher.org/) to launch and manage Minecraft instances directly from the UI. It communicates with Prism via a hook library (`libprismhook.so`) that is preloaded into the Prism process.

Configure the hook in the manager settings by pointing it at your Prism Launcher executable.

### Flatpak users

If you installed Prism Launcher via Flatpak, you need to grant it access to the Unix socket that the manager uses for IPC. The socket lives in `$XDG_RUNTIME_DIR/minecraft_manager`, which corresponds to `xdg-run/minecraft_manager` in Flatpak's filesystem namespace. By default Flatpak sandboxes access to this path, so Prism cannot reach the socket and launching bots will fail silently.

Run this once to grant the permission:

```bash
flatpak override --user --filesystem=xdg-run/minecraft_manager org.prismlauncher.PrismLauncher
```

## Scripting

Bots are automated using Python scripts in the `scripts/` directory. The manager embeds a Python interpreter and exposes APIs for bot control, inventory, world interaction, and crafting.

See the [documentation](https://mankool0.github.io/mc-bot-manager/) for the full scripting API and examples.
